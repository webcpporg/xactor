# xactor

xactor is a header-only, deterministic actor system for C++20, one of the
libraries of [webcpp](https://github.com/webcpporg/webcpp).

An actor is a logic that handles one message at a time, in a turn that lets
it send and reply, spawn and stop children, arm timers and finish with an
output. The scheduler gives each actor its messages in an order that depends
on the order they were sent alone, so two runs, or two drivers, deliver the
same messages the same way and write the same envelope log. Every message
caused by one delivery from the host is paid from that delivery's fuel, and
time moves only when the host brings it.

One header, `<webcpp/xactor.hpp>`, includes the whole library, whose names
are in the namespace `webcpp::xactor`. It needs Boost's headers and nothing
to build or link. It runs natively, on wasm32-wasip2 and on wasm32-wasip3; a
WASI build leaves out `asio_driver`, the one part that needs Boost.Asio.

## Building and testing

xactor is developed inside the webcpp superproject, as a Boost library is
developed inside Boost:

    git clone --recursive https://github.com/webcpporg/webcpp
    cd webcpp
    b2 libs/xactor/test libs/xactor/example

builds the tests and the examples natively, runs every example and compares
its output with the `.expected` file beside it.
`toolset=clang-wasip2 testing.launcher=wasmtime` and
`toolset=clang-wasip3 testing.launcher=wasmtime` do the same for WASI, with
the toolsets that `user-config.jam` registers against wasi-sdk. A b2 project
uses xactor through `/webcpp/xactor//xactor`.

## Documentation

xactor's page, with its API reference, is published at
<https://webcpporg.github.io/webcpp/libs/xactor/>; `b2 libs/xactor/doc`
builds it into `doc/html/index.html`.

## License

Distributed under the [Boost Software License, Version 1.0](LICENSE_1_0.txt).
