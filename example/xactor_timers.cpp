// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/xactor.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <variant>

namespace xactor = webcpp::xactor;

namespace {

struct start {};

struct delay {};

struct cancel {};

struct boiled {};

struct whistle {};

using message = std::variant<start, delay, cancel, boiled, whistle>;

// tag::kettle[]
/** Boils 100 ms after start and whistles 120 ms after it; delay and cancel move the boil. */
class kettle final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            const xactor::result<void> armed = turn.wake_at(turn.now() + 100, boiled{}, "boil");
            if (!armed.has_value()) {
                return armed;
            }
            return turn.wake_at(turn.now() + 120, whistle{});
        }
        if (std::holds_alternative<delay>(cause.payload)) {
            // The same key: this timer replaces the one start armed.
            return turn.wake_at(turn.now() + 150, boiled{}, "boil");
        }
        if (std::holds_alternative<cancel>(cause.payload)) {
            turn.cancel_timer("boil");
            return {};
        }
        if (std::holds_alternative<boiled>(cause.payload)) {
            std::cout << "boiled at " << turn.now() << '\n';
            return {};
        }
        std::cout << "whistle at " << turn.now() << '\n';
        return {};
    }
};

// end::kettle[]

/** Prints how many timers wait and the earliest deadline. */
void print_timers(const xactor::scheduler<message>& system) {
    const std::optional<std::uint64_t> next = system.next_deadline();
    std::cout << "timers " << system.pending_timers() << ", next deadline ";
    if (next.has_value()) {
        std::cout << *next << '\n';
    } else {
        std::cout << "none\n";
    }
}

/** Delivers one message from the host in its own execution and runs the system idle. */
bool deliver(xactor::scheduler<message>& system, xactor::actor_ref to, message payload,
             std::uint64_t execution) {
    if (!system.deliver(to, payload, xactor::correlation_id{.value = execution}).has_value()) {
        return false;
    }
    xactor::fifo_driver<message> driver(system);
    return driver.run_until_idle(nullptr).has_value();
}

}  // namespace

int main() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 100});
    const xactor::result<xactor::actor_ref> boiling = xactor::create_actor<kettle>(system);
    if (!boiling.has_value()) {
        return 1;
    }
    xactor::fifo_driver<message> driver(system);

    // tag::arm[]
    if (!deliver(system, *boiling, start{}, 1)) {
        return 1;
    }
    print_timers(system);
    if (!deliver(system, *boiling, delay{}, 2)) {
        return 1;
    }
    print_timers(system);
    // end::arm[]

    // tag::tick[]
    if (!system.clock_tick(130, xactor::correlation_id{.value = 3}).has_value()) {
        return 1;
    }
    if (!driver.run_until_idle(nullptr).has_value()) {
        return 1;
    }
    print_timers(system);
    const xactor::result<bool> released =
        system.release_next(200, xactor::correlation_id{.value = 4});
    if (!released.has_value() || !*released) {
        return 1;
    }
    if (!driver.run_until_idle(nullptr).has_value()) {
        return 1;
    }
    print_timers(system);
    // end::tick[]

    // tag::cancel[]
    if (!deliver(system, *boiling, start{}, 5) || !deliver(system, *boiling, cancel{}, 6)) {
        return 1;
    }
    print_timers(system);
    if (!system.clock_tick(400, xactor::correlation_id{.value = 7}).has_value()) {
        return 1;
    }
    if (!driver.run_until_idle(nullptr).has_value()) {
        return 1;
    }
    print_timers(system);
    // end::cancel[]
    return 0;
}
