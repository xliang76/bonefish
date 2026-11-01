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

C++20 + CMake >= 3.16 is the ported baseline: covered by GCC 11 (jammy) and up.
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
2. Local fallback (no root needed): download official jammy
   `g++-11`/`gcc-11`/`libstdc++-11-dev` debs, `dpkg -x` them to a prefix, and
   configure through a small toolchain file pointing `CMAKE_CXX_COMPILER` at
   the extracted g++-11; run the full ctest suite with that prefix's
   `libstdc++` on `LD_LIBRARY_PATH`.

## ABI / compatibility checks

- Ported binaries must load on stock 22.04: scan for the highest required
  GLIBCXX symbol version and require `<= GLIBCXX_3.4.29`:
  `objdump -T build/daemon/bonefish | grep -o 'GLIBCXX_[0-9.]*' | sort -Vu | tail -1`
- Keep `CXX_EXTENSIONS OFF` (no GNU-only extensions creeping in).

## Out of scope unless requested

Debian packaging (deb build), ARM images, clang matrix (works locally but CI
stays GCC-only to match the two distros' defaults).
