# BOOST-REMOVAL.md - Goal 3: STL instead of Boost

Constraint: functionality AND performance must be preserved. Acceptance is
mechanical: the P0 test/benchmark harness (PROGRESS.md) run before/after.

## Measured audit (current tree, 2026-11-01)

`grep` of `src daemon examples test`:

| Boost facility | uses | where | replacement | notes |
|---|---|---|---|---|
| Boost.Asio (`boost::asio::*`) | 77 syms / 27 includes | all transports, daemon | **standalone Asio** (`asio::io_context`, `steady_timer`, `ASIO_STANDALONE`) | only non-STL-able facility - see decision 1 in ARCHITECTURE.md |
| `boost::system::error_code` | 28, 7 headers | rawsocket/dealer/daemon | `std::error_code` | Asio's EC *is* std's; mechanical rename |
| `boost::future/promise` | 4 + 2 | `native/` only | `std::future/promise` | no `.then`/`when_all` in tree (grep-verified), so std is sufficient |
| `boost::copy_exception/current_exception` | 4 + 2 | `native/native_server_impl.cpp` | `set_exception(std::make_exception_ptr(...))`, `std::current_exception` | |
| `boost::posix_time::milliseconds` | 1 | `dealer/wamp_dealer_invocation.hpp` timer | `std::chrono::milliseconds` + `steady_timer::expires_after` | monotonic clock: small intentional fix, documented |
| `boost::bind` + `_1,_2` | 3 | `daemon/daemon.cpp` | lambdas | identical call semantics when captures explicit |
| `boost::format` + BOOST_PP varargs | 2 + 3 includes | `trace/trace.hpp` | variadic templates + `ostringstream` | keep `%N%` placeholder semantics |
| `boost::regex` | 1 file | `utility/wamp_uri.cpp` (called from every message marshal/unmarshal) | **hand-rolled character-class validator** | `std::regex` (libstdc++) is markedly slower - would violate the perf goal |
| `boost::program_options` | 1 file | `daemon/daemon_options.cpp` | hand-rolled parser (~10 flags) | same flags/defaults/exit codes; e2e CLI cases gate it |
| `boost/exception/exception.hpp` | 1 include | native | remove (unused after future port) | verify at port time |
| Boost components in CMake | program_options, system, thread (+regex for GCC<4.9) | root CMakeLists | deleted (`thread` was only needed for boost futures) | final audit: no `boost` string in any CMakeLists; `nm`/`ldd` clean |

## Ordered steps (revised 2026-11-01, before P2 started)

Original order assumed `boost::system::error_code` was a mechanical rename
("Asio's EC *is* std's"). TRUE only for standalone Asio (and Boost >= 1.69),
but the baseline pins **Boost 1.65.1**, where `boost::system::error_code` is
a distinct type with no `std::error_code` interop and `deadline_timer` has
no `expires_after`. Renaming EC (2a) or the invocation timer (2b) in
isolation would not compile against the Asio engine still under
construction. Revised order, each step keeps ctest green:

- [x] 2a': `boost::bind` -> lambdas (`daemon/daemon.cpp`).
- [x] 2b': `boost::future/promise/copy_exception/current_exception` ->
      `std::` (`native/` only; drop `boost/exception/exception.hpp`).
- [x] 2c': `trace.hpp`: BOOST_PP varargs -> variadic templates,
      `boost::format` -> std-only formatter; keep `%N%` semantics
      (`test/unit/test_trace.cpp` gates).
- [x] 2d': `wamp_uri`: `boost::regex` -> hand-rolled char-class validator
      (parity table in `test/unit/test_wamp_uri.cpp` gates;
      `bench/uri_bench` must not regress).
- [x] 2e': daemon CLI: `boost::program_options` -> hand-rolled parser
      (e2e CLI matrix gates).
- [x] 2f' (big): networking step, absorbing old 2a + 2b-timer: add
      `third-party/asio` submodule (xliang76/asio, pin 1.30.x), re-pin
      websocketpp to a standalone-Asio-capable commit, then ALL Asio
      boundaries at once: `boost/asio/*` -> `asio/*`, `io_service` ->
      `io_context`, `deadline_timer` -> `steady_timer` +
      `expires_after(std::chrono::milliseconds)` (the documented
      monotonic-clock fix), `boost::system::error_code` ->
      `std::error_code` (28 uses / 7 headers), `ASIO_STANDALONE`.
- [ ] 2g' (= old 2h): remove `find_package(Boost)`, `BOOST_COMPONENTS`,
      `${Boost_LIBRARIES}` links. Final audit: `grep -rn boost src daemon
      examples test` empty; `nm -C libbonefish.a | grep -ci boost` = 0;
      `ldd` daemon free of Boost.

Remaining facilities not in the list (verified absent): none other beyond
`boost/preprocessor` (trace, 2c') and the Boost.Asio engine (2f').

## Performance guardrails

1. Benchmark baseline recorded from the **as-is** build before any port commit
   (`bench/BASELINE.md`): pub/sub msg rate, RPC latency p50/p99, 512 KiB
   payload throughput, `is_valid_uri` micro-bench.
2. Acceptance window: within +/- 5% on the same hardware/compiler; URI
   validation must not regress (expect a win from the hand-rolled validator).
3. Boost.Asio vs standalone Asio: same codebase (Boost copy is a packaging of
   standalone), same epoll/epoll_reactor engine -> no expected delta; the
   benchmarks prove it, they are not assumed.
4. Keep `-O2` Release for all benchmark runs; run on both target distros
   (PLATFORM.md) before accepting.

## Functional-preservation gates

- Unit: exact accept/reject table for all four URI patterns (captured from the
  current regex behavior BEFORE replacing it); all 24 message classes wire code
  + field order + msgpack round-trip; trace formatter output strings.
- E2E: RawSocket TCP + UDS, WebSocket both subprotocols, full pub/sub + RPC,
  large payloads, daemon CLI matrix (`--help`, unknown flag exit code,
  `--no-msgpack`/`--no-json`), embedded integration example.
- ASan/UBSan build runs the same e2e suite; any sanitizer report fails CI.

## Breaking-change notice (documented in README at P5)

Public headers (`native/native_endpoint.hpp`, connector, servers) will expose
`asio::` types instead of `boost::asio::` - source-incompatible for embedders
who referenced Boost types directly. Acceptable for this fork; call it out in
the release note.
