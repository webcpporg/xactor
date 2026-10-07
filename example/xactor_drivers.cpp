// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/xactor.hpp>

#include <cstddef>
#include <iostream>
#include <variant>

namespace xactor = webcpp::xactor;

namespace {

struct serve {
    xactor::actor_ref to{};
};

struct ball {
    int rally = 0;
};

using message = std::variant<serve, ball>;

// tag::player[]
/** Returns the ball until the rally reaches 4. */
class player final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (const auto* served = std::get_if<serve>(&cause.payload)) {
            return turn.send(served->to, ball{.rally = 1});
        }
        const int rally = std::get<ball>(cause.payload).rally;
        if (rally == 4) {
            return {};
        }
        return turn.reply(ball{.rally = rally + 1});
    }
};

// end::player[]

/** Two players and a serve from the host; returns the server, or 0 on failure. */
xactor::actor_ref set_up(xactor::scheduler<message>& system) {
    const xactor::result<xactor::actor_ref> left = xactor::create_actor<player>(system);
    const xactor::result<xactor::actor_ref> right = xactor::create_actor<player>(system);
    if (!left.has_value() || !right.has_value()) {
        return {};
    }
    if (!system.deliver(*left, serve{.to = *right}, xactor::correlation_id{.value = 1})
             .has_value()) {
        return {};
    }
    return *left;
}

void print(const xactor::envelope_log& log) {
    for (const xactor::logged_envelope& entry : log.entries()) {
        std::cout << "  #" << entry.sequence.value << " to " << entry.to.value << " from "
                  << entry.from.value << ", fuel " << entry.remaining_fuel << '\n';
    }
}

}  // namespace

int main() {
    // tag::test_driver[]
    xactor::scheduler<message> stepped(xactor::budgets{.fuel = 100});
    if (set_up(stepped).value == 0) {
        return 1;
    }
    xactor::test_driver<message> one_at_a_time(stepped);
    xactor::envelope_log stepped_log;
    std::size_t steps = 0;
    while (true) {
        const xactor::result<bool> ran = one_at_a_time.step(stepped_log);
        if (!ran.has_value()) {
            return 1;
        }
        if (!*ran) {
            break;
        }
        ++steps;
    }
    std::cout << "test_driver: " << steps << " steps\n";
    print(stepped_log);
    // end::test_driver[]

    // tag::fifo_driver[]
    xactor::scheduler<message> turned(xactor::budgets{.fuel = 100});
    if (set_up(turned).value == 0) {
        return 1;
    }
    xactor::fifo_driver<message> fifo(turned);
    xactor::envelope_log turned_log;
    const xactor::result<std::size_t> first = fifo.run_turn(2, &turned_log);
    if (!first.has_value()) {
        return 1;
    }
    std::cout << "fifo_driver: run_turn(2) delivered " << *first << ", idle " << std::boolalpha
              << turned.idle() << '\n';
    const xactor::result<std::size_t> rest = fifo.run_until_idle(&turned_log);
    if (!rest.has_value()) {
        return 1;
    }
    std::cout << "fifo_driver: run_until_idle delivered " << *rest << ", idle " << turned.idle()
              << '\n';
    std::cout << "the two logs are equal: " << (stepped_log == turned_log) << '\n';
    // end::fifo_driver[]
    return 0;
}
