# Benchmark Baseline (P0)

Reference numbers for the **Boost-based baseline tree** (unmodified `master`
sources, branch `UDQ4XL` build state at P0 completion). These are the fixed
points for the P4 performance gate in `PROGRESS.md`: every Boost-removal
phase must re-measure and stay inside **±5%** on throughput metrics and
**p50 latency**; p99/max are noisy on shared machines and are tracked as
trend, not as a hard gate.

## Environment

- Host: single dev machine (loopback only), Ubuntu, GCC 13, CMake 3.22.6
- Boost 1.65.1 prebuilt (`/tmp/opencode/boost`), Release build
  (`-DCMAKE_BUILD_TYPE=Release`) plus the warning suppressions in
  `BASELINE_FLAGS.txt` (identical flags are used for the ported tree so the
  A/B comparison is fair)
- Client and daemon run on the same host over loopback TCP

## How to measure

```sh
# configure + build (see AGENTS.md for the toolchain setup)
$CMAKE -S . -B build -DCMAKE_BUILD_TYPE=Release -DBOOST_ROOT=... \
     -DCMAKE_CXX_FLAGS="$(cat BASELINE_FLAGS.txt)"   # adapt path
$CMAKE --build build -j

# micro-benchmark (in-process, no daemon needed)
build/bench/uri_bench

# end-to-end (needs a daemon on a rawsocket port)
build/daemon/bonefish --realm default --rawsocket-port 8888 &
build/bench/rawsocket_bench --port 8888
```

`rawsocket_bench` defaults: 100 000 pub/sub messages, 20 000 sequential
RPCs, 256 × 512 KiB large payloads. `uri_bench` averages 20 000 passes over
a 12-URI corpus per flag mode.

## Baseline numbers

Median of two consecutive runs:

| Metric                        | run 1   | run 2   | baseline  |
|-------------------------------|---------|---------|-----------|
| pubsub_msgs_per_sec           | 187 301 | 185 170 | **186 200** |
| rpc_latency_us mean           | 32.93   | 31.46   | **32.2**  |
| rpc_latency_us p50            | 33.28   | 31.64   | **32.5**  |
| rpc_latency_us p99            | 54.34   | 41.12   | 47 (trend)|
| large_payload_mbps            | 963.3   | 956.3   | **960**   |
| uri_relaxed_ns_per_op         | 576.5   | 554.4   | **565**   |
| uri_relaxed_empty_ns_per_op   | 614.8   | 596.6   | **605**   |
| uri_strict_ns_per_op          | 480.2   | 476.0   | **478**   |
| uri_strict_empty_ns_per_op    | 519.5   | 503.0   | **511**   |

Notes

- `uri_*` measures the `std::regex` implementation currently in
  `src/bonefish/utility/wamp_uri.cpp`. The hand-rolled validator that
  replaces it (`BOOST-REMOVAL.md` step 2c) is expected to be *faster*, not
  slower — the parity table in `test/unit/test_wamp_uri.cpp` locks
  semantics, this benchmark locks that the speedup is real and nothing
  regressed elsewhere.
- Run-to-run spread observed here: ~1% on throughput, up to ~5% on
  mean/p50 latency; re-measure twice per gate check and compare medians.
