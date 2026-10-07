// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/xactor.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <variant>

namespace xactor = webcpp::xactor;

namespace {

struct start {};

struct hop {};

using message = std::variant<start, hop>;

// tag::relay[]
/** Sends itself a hop on every turn, for as long as the execution can pay. */
class relay final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& /*cause*/) override {
        std::cout << "relay: fuel " << turn.fuel() << '\n';
        const xactor::result<void> sent = turn.send(turn.self(), hop{});
        if (!sent.has_value()) {
            std::cout << "relay: refused, " << sent.error().message() << '\n';
        }
        return {};
    }
};

// end::relay[]

// tag::worker[]
/** Pays 2 units for its own work on every turn, then sends itself a hop. */
class worker final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& /*cause*/) override {
        const xactor::result<void> paid = turn.spend(2);
        if (!paid.has_value()) {
            return paid;
        }
        return turn.send(turn.self(), hop{});
    }
};

// end::worker[]

const char* spelling(xactor::status value) {
    switch (value) {
        case xactor::status::active: return "active";
        case xactor::status::done: return "done";
        case xactor::status::error: return "error";
        case xactor::status::stopped: return "stopped";
    }
    return "unknown";
}

}  // namespace

int main() {
    // tag::chain[]
    xactor::scheduler<message> system(xactor::budgets{.fuel = 3});
    const xactor::result<xactor::actor_ref> relaying = xactor::create_actor<relay>(system);
    if (!relaying.has_value()) {
        return 1;
    }
    const xactor::correlation_id first{.value = 1};
    if (!system.deliver(*relaying, start{}, first).has_value()) {
        return 1;
    }
    xactor::fifo_driver<message> driver(system);
    const xactor::result<std::size_t> delivered = driver.run_until_idle(nullptr);
    if (!delivered.has_value()) {
        return 1;
    }
    std::cout << "delivered " << *delivered << ", relay is "
              << spelling(system.status_of(*relaying).value()) << '\n';
    // end::chain[]

    // tag::executions[]
    const xactor::correlation_id second{.value = 2};
    if (!system.deliver(*relaying, start{}, second).has_value()) {
        return 1;
    }
    const xactor::result<std::size_t> again = driver.run_until_idle(nullptr);
    if (!again.has_value()) {
        return 1;
    }
    std::cout << "execution 1 has " << system.fuel_of(first) << " left, execution 2 has "
              << system.fuel_of(second) << " left\n";
    // end::executions[]

    // tag::spend[]
    const xactor::result<xactor::actor_ref> working = xactor::create_actor<worker>(system);
    if (!working.has_value()) {
        return 1;
    }
    const xactor::correlation_id third{.value = 3};
    if (!system.deliver(*working, start{}, third).has_value()) {
        return 1;
    }
    const xactor::result<std::size_t> worked = driver.run_until_idle(nullptr);
    if (!worked.has_value()) {
        return 1;
    }
    std::cout << "worker is " << spelling(system.status_of(*working).value()) << " ("
              << system.error_of(*working).message() << "), execution 3 has "
              << system.fuel_of(third) << " left\n";
    // end::spend[]
    return 0;
}
