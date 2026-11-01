# PLATFORM.md - Goal 2: Ubuntu 22.04 and 24.04

## Target matrix

| | Ubuntu 22.04 LTS (jammy) | Ubuntu 24.04 LTS (noble) |
|---|---|---|
| default GCC | 11.4 (`gcc-12` also offered) | 13.3 (`gcc-14` available) |
| default CMake | 3.22 | 3.28 |
| glibc | 2.35 | 2.39 |
| libstdc++ GLIBCXX floor | 3.4.30 (runtime also ships 12.x) | 3.4.32 |
| system Boost (UNUSED after P2) | 1.74 | 1.83 |
| Python test deps | python3-msgpack, python3-websockets | same |

C++20 is the current standard (flipped 2026-11 after the websocketpp fix; set
in root CMakeLists.txt). The bump was previously blocked because websocketpp
0.8.2's template-id constructors/destructors are ill-formed under `-std=c++20`;
the 13-line fix now lives in the xliang76 fork (PR #1) and the submodule pins
commit f49d256 (0.8.2 + fix). Everything builds clean at `-std=c++20` with
`-Wall -Werror` on GCC 11 and GCC 13 (see BOOST-REMOVAL.md 2g' for the full
history). CMake >= 3.16 throughout: covered by GCC 11 (jammy) and up.
Do NOT use `std::format`, `std::span`-dependent or concepts features that GCC 11
lacks without checking - jammy libstdc++ 11 has no `<format>`.

Boost.Asio note (why goal 2 pushes goal 3): system Boost 1.74/1.83 still ship
Boost.Asio, but it is deprecated since Boost 1.82 and slated for removal from
Boost; depending on it is a dead end on future LTS images.

## Dev host facts (this machine)

- Ubuntu 24.04.5, g++-13 only; no `cmake` on PATH; no docker/podman; no root.
- Working baseline toolchain (pre-port builds):
  `CMAKE=/tmp/opencode/cmake-3.22.6-linux-x86_64/bin/cmake` and
  `-DBOOST_ROOT=/tmp/opencode/boost` (Boost 1.65.1 prebuilt there).
- CMake 4.x rejects this repo's `cmake_minimum_required(VERSION 2.8)` and its
  `FindBoost` is gone; after P1c raises the minimum to 3.16, modern CMake works.

## Build + test commands (post-port target state)

```
sudo apt-get install -y g++ cmake git python3-msgpack python3-websockets
git clone --recursive https://github.com/xliang76/bonefish.git && cd bonefish
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure
```

## 22.04 validation methods (no docker here)

1. Primary: GitHub Actions `runs-on: ubuntu-22.04` (native runner). Workflow
   `.github/workflows/ci.yml`: 22.04/24.04 x Debug/Release + ASan/UBSan job on
   24.04 scanning captured daemon stderr for sanitizer reports.
2. Local fallback (no root needed, verified 2026-11-01 under
   `build/scratch/jammy`): download official jammy debs and `dpkg -x` each as
   a standalone command into one sysroot prefix - `cpp-11 gcc-11 g++-11
   libgcc-11-dev libstdc++-11-dev` (gcc-11 11.4.0-1ubuntu1~22.04.3),
   `libstdc++6` + `libgcc-s1` (gcc-12 12.3.0, jammy's runtime), `libc6` +
   `libc6-dev` (glibc 2.35-0ubuntu3.15) and `linux-libc-dev` (UAPI headers).
   Configure with `-DCMAKE_CXX_COMPILER=$P/usr/bin/g++-11` and `--sysroot=$P`
   in `CMAKE_CXX_FLAGS`, then run the full ctest suite with
   `LD_LIBRARY_PATH=$P/usr/lib/x86_64-linux-gnu` (jammy libstdc++ ceiling is
   GLIBCXX_3.4.30, so a passing run also proves the jammy ABI floor).

## ABI / compatibility checks

- Measured at P3 (2026-11-01): the daemon built here (noble, GCC 13) needs
  `GLIBCXX_3.4.32`, pulled in unconditionally by `std::ios_base_library_init`
  (any `<iostream>` user, GCC >= 12). A noble-built binary therefore cannot
  meet a "loads on stock jammy" floor without `-static-libstdc++`; shipping
  binaries across distros is a packaging concern (out of scope below), so
  the floor guard is anchored where it is meaningful:
  - CI's ubuntu-22.04 job FAILS if its daemon requires more than
    `GLIBCXX_3.4.30` (jammy's libstdc++6 ceiling) - catches toolchain drift;
  - the ubuntu-24.04 job only reports its floor.
  `objdump -T build/daemon/bonefish | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail -1`
- Keep `CXX_EXTENSIONS OFF` (no GNU-only extensions creeping in).

## Out of scope unless requested

Debian packaging (deb build), ARM images, clang matrix (works locally but CI
stays GCC-only to match the two distros' defaults).
