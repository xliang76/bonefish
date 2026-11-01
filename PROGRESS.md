# PROGRESS.md - Bonefish upgrade program

Three goals: (1) all code + submodules under the `xliang76` GitHub account,
(2) first-class support for Ubuntu 22.04 and 24.04, (3) replace Boost with the
STL where possible without losing functionality or performance.

Branch policy: all work happens on this working branch (`UDQ4XL`), kept
mergeable with `master`. Do **not** read, touch, or merge the branch
`feature/modernize-cpp17` (owner instruction).

## Status log

- 2026-11-01: Cleaned `UDQ4XL` to pristine `master` state; environment audit
  done (Ubuntu 24.04.5 host, g++-13.3 only, no cmake on PATH, no container
  runtime); Boost usage measured on the current tree; xliang76 repos verified
  reachable; program docs written. Implementation not started.
- 2026-11-01: **P0 complete.** Guardrail harness landed: 3 unit suites +
  e2e driver (20 cases) + sanitizer-log scan wired into CTest (all green on
  the unmodified baseline; sabotage check confirms the e2e suite fails a
  fake daemon), benchmark harness (`bench/`) measured twice with baseline
  recorded in `bench/BASELINE.md`. New tooling notes: the CMake build needs
  `-DPython3_EXECUTABLE=<python with msgpack+websockets>` for the e2e test
  and warning suppressions for legacy headers (AGENTS.md build snippet).
- 2026-11-01: **P1 complete** (see P1 checklist for details). `.gitmodules`
  now xliang76-only (pins fetch-proven, fresh clone verified non-recursive);
  root CMakeLists modernized (3.16 / VERSION / C++17 / dead branches removed;
  C++20 deferred to P2h because boost::format + websocketpp 0.7 hard-break
  C++20); `.travis.yml` deleted; README rewritten to current state. Full
  ctest green on the modernized build (`/tmp/opencode/p1-build`).
  `BASELINE_FLAGS.txt` rewritten to raw flags so the AGENTS.md
  `$(cat ...)` recipe works.

## Phase P0 - Guardrails first (no code change)

- [ ] Fresh clone + `git submodule update --init` resolves ONLY from
      `github.com/xliang76/*` on the pushed branch (pins verified reachable on
      the forks locally; full check needs the P1 commits pushed, and the
      nested-submodule caveat in GIT-OWNERSHIP.md resolved).
- [x] Baseline build green on this host (Ubuntu 24.04, g++ 13) using the
      pre-Boost-1.65.1 baseline documented in AGENTS.md.
- [x] Unit tests (new, CTest): message wire formats (codes + field order +
      msgpack round-trip), `wamp_uri` validation table, trace formatter.
      Verify: `ctest` passes against unmodified code.
- [x] E2E harness (new, CTest): launch built daemon; drive RawSocket TCP +
      UDS and WebSocket JSON/msgpack; full HELLO/SUBSCRIBE/PUBLISH/EVENT +
      REGISTER/CALL/INVOCATION/YIELD flows; 512 KiB payload; `--help`, bad
      flag, `--no-msgpack`/`--no-json` behavior; embedded example binary runs.
      Verify: green against baseline; a deliberately broken daemon fails it.
- [x] Benchmark harness (new): pub/sub msg rate, RPC round-trip latency,
      512 KiB throughput, `is_valid_uri` micro-benchmark. Record numbers from
      the as-is baseline build (Release, -O2) in `bench/BASELINE.md`.
      Verify: two consecutive runs differ < 5%.

## Phase P1 - Ownership + build modernization

- [x] P1a: `.gitmodules` URLs -> `https://github.com/xliang76/<name>.git` for
      all five submodules. Verify: pins fetch from the forks and a fresh
      clone + `git submodule update --init` (non-recursive, see the nested
      caveat in GIT-OWNERSHIP.md) resolves xliang76-only. DONE 2026-11-01.
- [x] CMake: `cmake_minimum_required(3.16)`, `project(bonefish VERSION 0.1.0
      LANGUAGES CXX)`, `CMAKE_CXX_STANDARD 17` + `CXX_EXTENSIONS OFF`, keep
      `-Wall -Werror`, drop the GCC<4.9/`USE_BOOST_REGEX` branch and
      `-std=c++11`. `enable_testing()` + `test/unit` landed with P0.
      **Deviation:** C++20 is NOT set here because the baseline tree's
      boost::format and websocketpp 0.7 headers hard-break under `-std=c++20`
      (verified: `allocator::allocate(n,hint)` removal; template-id
      constructor in `logger/basic.hpp`). C++17 compiles clean; bump to 20
      in P2h when those dependencies are gone.
      Verify DONE: fresh configure + full build on g++-13 (`-std=c++17`
      confirmed in flags.make), all 4 ctest cases green incl. e2e. DONE 2026-11-01.
- [x] Delete `.travis.yml` (dead service), replace README badge + build section,
      README clone/dependency text matches new state (xliang76 URLs, C++17
      toolchain, CMake 3.16, non-recursive submodule init, ctest section).
      DONE 2026-11-01. A GitHub Actions badge returns with P3.

## Phase P2 - Boost -> STL port (each step keeps ctest green)

Ordered so the build never needs Boost and legacy at the same time except 2a.

- [ ] 2a: `boost::system::error_code` -> `std::error_code` (28 uses, 7 headers).
- [ ] 2b: `boost::bind` -> lambdas; `boost::posix_time` -> `std::chrono`.
- [ ] 2c: `boost::future/promise/copy_exception` -> `std::` equivalents in
      `native/` (no `.then`/`when_all` in tree - verified by grep).
- [ ] 2d: `trace.hpp`: BOOST_PP varargs -> variadic templates; `boost::format`
      -> small std-only formatter. NOTE: GCC 11 (22.04) libstdc++ has no
      `<format>`; do not use `std::format`.
- [ ] 2e: `wamp_uri`: replace `boost::regex` with hand-written validator
      (4 patterns; keep exact accept/reject table from P0 unit tests).
- [ ] 2f: daemon CLI: `boost::program_options` -> hand-rolled parser, same
      flags/defaults/exit codes (P0 e2e CLI cases are the gate).
- [ ] 2g: networking: Boost.Asio -> standalone Asio.
      - Add `third-party/asio` submodule (xliang76/asio fork, pin a 1.30.x tag).
      - Bump `third-party/websocketpp` to a standalone-Asio-capable pin.
      - `io_service`->`io_context`, `deadline_timer`->`steady_timer`,
        `ASIO_STANDALONE`, includes `boost/asio/*` -> `asio/*`.
- [ ] 2h: remove `find_package(Boost)`, BOOST_COMPONENTS, USE_BOOST_REGEX.
      Verify (final Boost audit): `grep -rn boost src daemon examples test`
      empty; `nm -C libbonefish.a | grep -ci boost` = 0; `ldd` daemon has no
      boost libs.

## Phase P3 - Platform validation (22.04 + 24.04)

See PLATFORM.md for the matrix and methods.

- [ ] GitHub Actions workflow: ubuntu-22.04/24.04 x Debug/Release + ASan/UBSan
      job with sanitizer-output scan of daemon stderr. Verify: green run on
      xliang76 (Actions must be enabled in repo settings).
- [ ] ABI floor: daemon/test binaries require at most `GLIBCXX_3.4.29`
      (22.04 runtime) - `objdump -T` scan.
- [ ] Native 22.04 run: Actions runner is the primary; local fallback is the
      jammy-g++-11 deb-extraction recipe in PLATFORM.md (no docker here).

## Phase P4 - Performance gate (goal 3 acceptance)

- [ ] Rebuild ported tree Release on 24.04; re-run bench harness; compare to
      `bench/BASELINE.md`. Acceptance: throughput/latency within +/- 5%;
      `is_valid_uri` >= baseline (hand-rolled validator should beat regex).
- [ ] Repeat on 22.04 (runner artifact or jammy toolchain prefix).
- [ ] Record results here + in `bench/RESULTS.md`.

## Phase P5 - Finalize

- [ ] Docs: README badges/commands, AGENTS.md refresh, stale `test/smoke/*.py`
      replaced or removed (superseded by e2e harness).
- [ ] Merge working branch to `master` (owner decision: PR vs direct), tag.

## Risks / watch items

- **Pre-existing baseline defect (found 2026-11-01 during 2c'):** running the
  daemon with `--debug` while RawSocket clients stream messages stalls the
  RawSocket acceptor after the initial connections (reproduced byte-identical
  on the untouched baseline daemon and the ported tree, so it is a 2015-era
  bug, not a port regression). The e2e suite never hits it (it enables
  `--debug` only for CLI-behavior cases, not for traffic). Out of scope for
  the Boost removal; revisit as a standalone daemon fix afterward.
- websocketpp pin bump is the riskiest dependency move: WebSocket handshake +
  subprotocol negotiation must be fully e2e-covered before merging 2f'.
- `std::regex` regression (mitigated by 2d') and `<format>` absence on 22.04
  (mitigated by the std-only formatter in 2c') are the two portability traps
  already identified.
- This container has no docker/podman and no root: native 22.04 proof leans on
  GitHub Actions runners or the deb-extraction toolchain recipe.
- GitHub forks keep a "forked from" link to the parent repo (cosmetic +
  network linkage); detaching requires GitHub support or new-repo + mirror
  push. See GIT-OWNERSHIP.md.

## Decision log

- Keep Asio as a vendored library instead of an (unexistent) STL equivalent -
  rationale in ARCHITECTURE.md decision 1.
- Hand-rolled URI validator instead of `std::regex` - decision 2.
- C++20 baseline (GCC 11 / Clang 14 minimum) covers both target distros.
