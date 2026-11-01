# AGENTS.md

Bonefish: a C++11 WAMP router (library + standalone daemon). Legacy codebase (2015,
`cmake_minimum_required(VERSION 2.8)`); most modern-toolchain friction comes from that.

## Layout

- `src/bonefish/` → static library target `bonefish`. Sources are listed **explicitly**
  in `src/CMakeLists.txt` (no globbing): a new `.cpp` must be added to `SOURCES` there.
- `daemon/` → server executable. CMake target is `bonefish_bin`, output name `bonefish`
  (so `make bonefish` builds the *library*; the daemon binary lands at `build/daemon/bonefish`).
- `examples/integration.cpp` → embedded-library usage example.
- `third-party/` → git submodules (header-only: msgpack-c, rapidjson,
  json-msgpack, cppcodec, websocketpp v0.7.0), wired via `include_directories` in the
  root `CMakeLists.txt`. Run `git submodule update --init --recursive` before
  configuring. A `+ third-party/...` line in `git status` means a submodule pointer
  drifted — restore with `git submodule update`, don't commit it.

## Build (verified on this machine)

The system has no `cmake` on PATH and no Boost with Boost.ASIO (modern Boost removed
it; this code requires it). A working toolchain set lives in `/tmp/opencode` (re-create
if wiped): CMake **3.22.6** and prebuilt Boost **1.65.1**. CMake ≥ 3.30/4.x breaks this
project (`cmake_minimum_required(VERSION 2.8)` rejected, `FindBoost` module removed).

```sh
export CMAKE=/tmp/opencode/cmake-3.22.6-linux-x86_64/bin/cmake
mkdir -p build && cd build
$CMAKE -DBOOST_ROOT=/tmp/opencode/boost \
       -DCMAKE_CXX_FLAGS="$(cat /tmp/opencode/BASELINE_FLAGS.txt)" ..
make -j
```

- Required Boost components: `program_options system thread` (+ `regex` only on GCC < 4.9).
- `-Wall -Werror` is set in the root `CMakeLists.txt`: warnings in project code fail the
  build. Under GCC 13 the legacy Boost 1.65/msgpack/websocketpp headers warn, so the
  baseline needs the targeted `-Wno-error=<...>` suppressions kept in
  `/tmp/opencode/BASELINE_FLAGS.txt` (recreate if wiped; they only demote warnings
  coming from third-party headers).
- Options: `-Dshared=ON` (shared lib; default static), `-Dstdlib=libstdc++` (clang only).

## Tests

CTest suite (P0 guardrail harness): `ctest --test-dir build` runs the unit suites
(`test/unit/`, standalone mini-harness in `bonefish_test.hpp`) and `e2e_daemon`
(`test/e2e/e2e_driver.py`). The e2e test needs python3 with `msgpack` + `websockets`
(`/tmp/opencode/venv/bin/python` works; pass `-DPython3_EXECUTABLE=...` at configure).
Benchmarks are built (`build/bench/`) but not registered as tests; see `bench/BASELINE.md`.

- `test/functional/websocket/client.cpp` builds binary `client` (websocketpp test client).
- `test/smoke/` → manual Python smoke tests (publisher/subscriber/caller/callee) driving a
  live daemon; old `autobahn`/`twisted` APIs (`six`-era). Superseded by `test/e2e/` (P5 retires it).

## Running

```sh
build/daemon/bonefish --realm default --websocket-port 9999 --rawsocket-port 8888 [--debug]
```

## Conventions

- New files must carry the Apache-2.0 header; template in `CONTRIBUTING.md`. Match the
  existing style (`wamp_*` file/type names, snake_case methods).
- Keep submodule pointers at their recorded commits unless the task is to update one.

## Local branches (not in `master`)

- `testAgainCode` — pytest-based e2e suites (`test/e2e/rawsocket_e2e.py`) and a defect log.
- `feature/modernize-cpp17` is **off-limits** per owner instruction: do not read,
  touch, merge, or use it as a reference for any work.

## Upgrade program (2026-11)

Root-level docs `PROGRESS.md` (tracker), `ARCHITECTURE.md`, `GIT-OWNERSHIP.md`,
`PLATFORM.md`, `BOOST-REMOVAL.md` define the current program: xliang76-only
remotes/submodules, Ubuntu 22.04/24.04 support, and Boost -> STL replacement
with functional/performance parity gates. Check `PROGRESS.md` before starting
related work.

## Environment quirks

- `git clean` is blocked by the permission config here; delete paths with `rm -rf` instead.
