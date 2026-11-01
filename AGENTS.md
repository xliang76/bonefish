# AGENTS.md

Bonefish: a C++17 WAMP router (library + standalone daemon), zero Boost — the
platform layer is standalone Asio + STL (2026-11 upgrade program; it began as a
2015 C++11/Boost codebase, modernized through P0–P4, see PROGRESS.md).

## Layout

- `src/bonefish/` → static library target `bonefish`. Sources are listed **explicitly**
  in `src/CMakeLists.txt` (no globbing): a new `.cpp` must be added to `SOURCES` there.
- `daemon/` → server executable. CMake target is `bonefish_bin`, output name `bonefish`
  (so `make bonefish` builds the *library*; the daemon binary lands at `build/daemon/bonefish`).
- `examples/integration.cpp` → embedded-library usage example.
- `third-party/` → git submodules (header-only: msgpack-c, rapidjson,
  json-msgpack, cppcodec, websocketpp 0.8.2; plus standalone Asio 1.30.2),
  wired via `include_directories` in the
  root `CMakeLists.txt`. Run `git submodule update --init` (deliberately NOT
  `--recursive`: nested submodules inside the third-party pins are their own
  dev-only trees on non-xliang76 URLs; see GIT-OWNERSHIP.md) before configuring.
  A `+ third-party/...` line in `git status` means a submodule pointer
  drifted — restore with `git submodule update`, don't commit it.

## Build (verified on this machine)

The build needs no system dependencies beyond a C++17 compiler (Boost is fully
gone since P2). This machine has no `cmake` on PATH; the working toolchain set
lives in `/tmp/opencode` (re-create if wiped): CMake **3.22.6**. CMake >= 3.16
works now (`cmake_minimum_required(VERSION 3.16)` since P1). The jammy
(g++-11) cross-toolchain lives at `build/scratch/jammy/sysroot` (PLATFORM.md
recipe).

```sh
export CMAKE=/tmp/opencode/cmake-3.22.6-linux-x86_64/bin/cmake
mkdir -p build && cd build
$CMAKE -DCMAKE_CXX_FLAGS="$(cat /tmp/opencode/BASELINE_FLAGS.txt)" ..
make -j
```

- `-Wall -Werror` is set in the root `CMakeLists.txt`: warnings in project code fail the
  build. Under GCC 13 the vendored msgpack/websocketpp/Asio headers warn, so
  local builds pass the targeted `-Wno-error=<...>` suppressions kept in
  `/tmp/opencode/BASELINE_FLAGS.txt` (recreate if wiped; they only demote warnings
  coming from third-party headers; CI passes the same list via `-DCMAKE_CXX_FLAGS`).
- Options: `-Dshared=ON` (shared lib; default static), `-Dstdlib=libstdc++` (clang only),
  `-Dsanitizers=ON` (ASan+UBSan; lowercase matters - CMake `option()` names are
  case-sensitive and the uppercase spelling is silently ignored).

## Tests

CTest suite (P0 guardrail harness): `ctest --test-dir build` runs the unit suites
(`test/unit/`, standalone mini-harness in `bonefish_test.hpp`) and `e2e_daemon`
(`test/e2e/e2e_driver.py`). The e2e test needs python3 with `msgpack` + `websockets`
(`/tmp/opencode/venv/bin/python` works; pass `-DPython3_EXECUTABLE=...` at configure).
Benchmarks are built (`build/bench/`) but not registered as tests; see `bench/BASELINE.md`.

- `test/functional/websocket/client.cpp` builds binary `client` (websocketpp test client).
- `test/smoke/` was retired in P5 (autobahn/`six`-era manual scripts, superseded
  by `test/e2e/`).

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
`PLATFORM.md`, `BOOST-REMOVAL.md` define the program: xliang76-only
remotes/submodules, Ubuntu 22.04/24.04 support, and Boost -> STL replacement
with functional/performance parity gates. P0-P4 + the local side of P3 are
complete (see PROGRESS.md status log); P5 remains blocked only on owner
approvals (push for CI, merge/tag, websocketpp C++20 patch). Check
`PROGRESS.md` before starting related work.

## Environment quirks

- `git clean` is blocked by the permission config here; delete paths with `rm -rf` instead.
