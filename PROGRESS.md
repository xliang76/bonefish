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
- 2026-11-01: **P2 steps 2a'-2f' complete** (commits `316113c` `45222ac`
  `8b99f1b` `ee5a36d` `df417d6` + CLI/networking commits). The tree now
  builds and tests fully green against vendored standalone Asio
  (`third-party/asio`, xliang76/asio @ `asio-1-30-2`, new submodule) with
  websocketpp re-pinned 0.7.0 -> 0.8.2 (xliang76 fork master `1b11fd3`,
  first fork commit with `ASIO_STANDALONE` support; the old pin is a direct
  ancestor). Zero non-comment `boost` references remain in src/daemon/
  examples/test/bench. Same-session A/B benchmark: pubsub 197k vs 183k
  msg/s (faster, +7% - outside the +/-5% band in the good direction,
  noted for P4), rpc p50 30.6 vs 31.7 us and 512KiB payload 815 vs 808
  MB/s within gate. Remaining: 2g' (drop find_package(Boost), C++20 bump,
  binary-level Boost audit).
- 2026-11-01: **P2 complete (2g'); Boost fully removed.** All Boost is out
  of the build system (find_package/BOOST_COMPONENTS/Boost_LIBRARIES/
  Boost_INCLUDE_DIRS/Boost_USE_STATIC_LIBS across 7 CMakeLists); the tree
  configures and builds with no BOOST_ROOT anywhere and passes 4/4 ctest.
  Audit: 0 boost symbols in libbonefish.a and daemon (`nm -C`), 0 in
  `strings`, `ldd` is libstdc++/libgcc/libc/libm only. Spot bench vs the
  boost baseline held the gate (pubsub +10.6% faster). **C++20 stays
  deferred** (documented deviation): websocketpp 0.8.2's template-id ctor/
  dtor headers are the sole blocker, no fetchable xliang76 commit fixes it;
  a 13-line patch proving the rest of the tree is C++20-clean is recorded
  in BOOST-REMOVAL.md 2g' - needs an approved push to xliang76/websocketpp.

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
      constructor in `logger/basic.hpp`). C++17 compiles clean. Later at 2g'
      the bump still did not land: boost::format/0.7 are gone but 0.8.2 kept
      the template-id ctor/dtor problem - see the 2g' deviation below.
      Verify DONE: fresh configure + full build on g++-13 (`-std=c++17`
      confirmed in flags.make), all 4 ctest cases green incl. e2e. DONE 2026-11-01.
- [x] Delete `.travis.yml` (dead service), replace README badge + build section,
      README clone/dependency text matches new state (xliang76 URLs, C++17
      toolchain, CMake 3.16, non-recursive submodule init, ctest section).
      DONE 2026-11-01. A GitHub Actions badge returns with P3.

## Phase P2 - Boost -> STL port (each step keeps ctest green)

Order revised before P2 started - see `BOOST-REMOVAL.md` "Ordered steps
(revised)" for the authoritative checklist and rationale (Boost 1.65 pins
`boost::system::error_code` apart from `std::error_code`, so the EC rename
and timer fix folded into the networking step 2f').

- [x] 2a' `boost::bind` -> lambdas (daemon). DONE (`316113c`).
- [x] 2b' boost futures/promise -> `std::future`/`std::promise` (native/).
      DONE (`45222ac`; public typedef change - breaking-change notice).
- [x] 2c' trace: variadic-template std formatter replaces boost::format +
      BOOST_PP; all chained call sites rewritten. DONE (`8b99f1b`).
- [x] 2d' wamp_uri: hand-rolled char-class validator replaces regex
      (parity table green, ~40x faster). DONE (`df417d6`).
- [x] 2e' daemon CLI: hand-rolled parser replaces program_options
      (A/B parity across 19 shapes). DONE.
- [x] 2f' networking: vendored standalone Asio (`third-party/asio`,
      xliang76/asio @ asio-1-30-2) + websocketpp re-pinned 0.7 -> 0.8.2
      (fork master `1b11fd3`); `boost/asio/*`->`asio/*`, `io_service`->
      `io_context`, `deadline_timer`->`steady_timer` + `expires_after`
      (monotonic-clock fix), EC -> `std::error_code` everywhere,
      `ASIO_STANDALONE`. WS `no_delay` moved to the post-accept hook
      (0.8.2 fires socket-init before the socket opens under modern
      Asio's lazy fd creation; EBADF otherwise). Full ctest green;
      same-session A/B bench: pubsub +7% faster, rpc p50 and large
      payload within gate. DONE.
- [x] 2g' (= old 2h): remove `find_package(Boost)`, BOOST_COMPONENTS,
      leftover `${Boost_LIBRARIES}` links; C++20 bump attempt. DONE:
      fresh configure without any `-DBOOST_ROOT` (zero Boost cache
      entries), full `-Wall -Werror` build, 4/4 ctest green; final audit
      `nm -C` libbonefish.a + daemon = 0 boost symbols, `strings` daemon
      = 0, `ldd` only libstdc++/libgcc/libc/libm; spot bench vs boost
      baseline: pubsub +10.6% (faster), rpc p50 +3.5% and 512KiB -0.4%
      (within gate). **C++20 deviation:** NOT bumped - websocketpp 0.8.2
      template-id ctors/dtors are ill-formed in C++20 and no fetchable
      xliang76 commit contains the fix (fork has not synced zaphoyd
      `develop`). Rest of tree verified C++20-clean with a 13-line
      header patch (saved as `websocketpp-cpp20-fix` in BOOST-REMOVAL.md
      2g' notes). Unblock = push patch to xliang76/websocketpp (owner
      approval) + re-pin + set C++20.

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
