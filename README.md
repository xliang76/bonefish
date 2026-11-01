Bonefish
========

[![CI](https://github.com/xliang76/bonefish/actions/workflows/ci.yml/badge.svg)](https://github.com/xliang76/bonefish/actions/workflows/ci.yml)

Bonefish is an open-source C++ WAMP application router that enables building applications from loosely-coupled components. It currently supports the WAMP Basic Profile and RawSocket Transport as part of the Advanced Profile. Bonefish is capable of hosting multiple routers each with their own realm and associated set of websocket and/or rawsocket endpoints.

For more information on WAMP, see http://wamp-proto.org/

## Contribute

See [CONTRIBUTING.md](CONTRIBUTING.md)

## Dependencies

You need the following installed:

- A C++17-compatible compiler toolchain (minimum supported versions: gcc-11, clang-13;
  i.e. the compilers shipped with Ubuntu 22.04 and newer)
- CMake 3.16 or newer

No other system libraries: everything else (standalone Asio, websocketpp,
msgpack-c, rapidjson, json-msgpack, cppcodec) is vendored as git submodules
under `third-party/`.

## Getting the Code

```
$ git clone git@github.com:xliang76/bonefish.git
$ cd bonefish && git submodule update --init
```

Deliberately no `--recursive`: the pinned third-party checkouts carry their
own nested submodules that are dev-only and not needed to build bonefish
(see [GIT-OWNERSHIP.md](GIT-OWNERSHIP.md)).

## Build

Bonefish uses CMake as its build system.

To build out of the source tree (preferred):

```
$ mkdir bonefish-build && cd bonefish-build
$ cmake ../bonefish
$ make
$ make install
```

If you have the `ninja` build tool available, invoke `cmake` with the `-GNinja` option and then use `ninja` instead of `make`.

No dependency search is needed: all third-party code is vendored under
`third-party/` and picked up automatically (header-only + standalone Asio).

## Tests

The CTest suite covers unit tests and end-to-end daemon tests:

```
$ cd bonefish-build && ctest
```

The end-to-end tests require python3 with the `msgpack` and `websockets`
modules. Benchmarks (`build/bench/`) are built but not registered as tests;
see [bench/BASELINE.md](bench/BASELINE.md).

### Deploy

You have the option to use bonefish as both an embedded library or as a standalone WAMP server.

#### Embedded

To understand how to integrate with bonefish as an embedded library an example has been provided:

```
examples/integration.cpp
```

To debug bonefish when it is an embedded library there is a global setting that can be turned on:

```
bonefish::trace::set_enabled(bool enabled)
```

This will enable debug tracing which is currently sent to the console. It is up to the embedder to select an appropriate mechanism for enabling and disabling debugging such as a preprocessor macro or environment variable.

#### Standalone
Apart from using it as an embedded library, you can also run bonefish as a standalone WAMP server:

```
daemon/bonefish --realm "default" --websocket-port 9999 --rawsocket-port 8888
```

To debug bonefish when it is run in standalone mode you can supply the optional command line switch:

```
daemon/bonefish --realm "default" --websocket-port 9999 --rawsocket-port 8888 --debug
```

This will enable debug tracing which is currently sent to the console.

### Options

- **shared** - if ON, bonefish will be built as a shared library. OFF implies it will be built as a static library (default).
- **stdlib** - when building with Clang, you can choose libc++ (default) or libstdc++.
- **sanitizers** - if ON, builds with AddressSanitizer + UndefinedBehaviorSanitizer (used by the CI sanitizer job).

## Third Party Libraries

Bonefish relies on the following library projects, all vendored as git
submodules under `https://github.com/xliang76/` (header-only usage; no Boost):

- [**json-msgpack**](https://github.com/xliang76/json-msgpack)
- [**msgpack-c**](https://github.com/xliang76/msgpack-c)
- [**RapidJSON**](https://github.com/xliang76/rapidjson)
- [**cppcodec**](https://github.com/xliang76/cppcodec)
- [**WebSocket++**](https://github.com/xliang76/websocketpp)
- [**Asio**](https://github.com/xliang76/asio) (standalone mode, replaces Boost.Asio)

## License

```
Copyright 2015 Topology LP.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

   http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
```
