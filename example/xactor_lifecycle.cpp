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
#include <vector>

namespace xactor = webcpp::xactor;

namespace {

struct start {};

struct work {
    int value = 0;
};

struct answer {
    int value = 0;
};

using message = std::variant<start, work, answer>;

// tag::children[]
/** Finishes with twice the value of its first work: it is then done, with an output. */
class doubler final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (const auto* asked = std::get_if<work>(&cause.payload)) {
            return turn.finish(answer{.value = asked->value * 2});
        }
        return {};
    }
};

/** Fails on its first work: it returns an error, and ends with the status error. */
class failing final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& /*turn*/,
                                const xactor::envelope<message>& cause) override {
        if (std::holds_alternative<work>(cause.payload)) {
            return xactor::failure<void>(xactor::errc::invalid_argument);
        }
        return {};
    }
};

/** Waits for work and never ends by itself. */
class idle final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& /*turn*/,
                                const xactor::envelope<message>& /*cause*/) override {
        return {};
    }
};

// end::children[]

// tag::supervisor[]
/** On start, spawns a doubler, a failing child and an idle one, and gives each work. */
class supervisor final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (!std::holds_alternative<start>(cause.payload)) {
            return {};
        }
        const xactor::result<xactor::actor_ref> first = turn.spawn_child<doubler>();
        if (!first.has_value()) {
            return {boost::system::in_place_error, first.error()};
        }
        const xactor::result<xactor::actor_ref> second = turn.spawn_child<failing>();
        if (!second.has_value()) {
            return {boost::system::in_place_error, second.error()};
        }
        const xactor::result<xactor::actor_ref> third = turn.spawn_child<idle>();
        if (!third.has_value()) {
            return {boost::system::in_place_error, third.error()};
        }
        for (const xactor::actor_ref child : {*first, *second, *third}) {
            const xactor::result<void> sent = turn.send(child, work{.value = 21});
            if (!sent.has_value()) {
                return sent;
            }
        }
        return {};
    }
};

// end::supervisor[]

const char* spelling(xactor::status value) {
    switch (value) {
        case xactor::status::active: return "active";
        case xactor::status::done: return "done";
        case xactor::status::error: return "error";
        case xactor::status::stopped: return "stopped";
    }
    return "unknown";
}

/** Prints the status of the actors at the addresses 1 to `count`. */
void print_statuses(const xactor::scheduler<message>& system, std::uint32_t count) {
    for (std::uint32_t address = 1; address <= count; ++address) {
        const xactor::result<xactor::status> current =
            system.status_of(xactor::actor_ref{.value = address});
        if (current.has_value()) {
            std::cout << "actor " << address << ": " << spelling(*current) << '\n';
        }
    }
}

}  // namespace

int main() {
    // tag::run[]
    xactor::scheduler<message> system(xactor::budgets{.fuel = 100});
    const xactor::result<xactor::actor_ref> root = xactor::create_actor<supervisor>(system);
    if (!root.has_value()) {
        return 1;
    }
    if (!system.deliver(*root, start{}, xactor::correlation_id{.value = 1}).has_value()) {
        return 1;
    }
    xactor::fifo_driver<message> driver(system);
    if (!driver.run_until_idle(nullptr).has_value()) {
        return 1;
    }
    print_statuses(system, 4);
    const message* output = system.output_of(xactor::actor_ref{.value = 2});
    if (output == nullptr) {
        return 1;
    }
    std::cout << "actor 2 finished with " << std::get<answer>(*output).value << '\n';
    std::cout << "actor 3 failed with " << system.error_of(xactor::actor_ref{.value = 3}).message()
              << '\n';
    // end::run[]

    // tag::stop[]
    const xactor::result<std::vector<xactor::actor_ref>> stopped = system.stop(*root);
    if (!stopped.has_value()) {
        return 1;
    }
    std::cout << "stopped:";
    for (const xactor::actor_ref actor : *stopped) {
        std::cout << ' ' << actor.value;
    }
    std::cout << '\n';
    print_statuses(system, 4);
    if (!system.deliver(*root, start{}, xactor::correlation_id{.value = 2}).has_value()) {
        return 1;
    }
    const xactor::result<std::size_t> delivered = driver.run_until_idle(nullptr);
    if (!delivered.has_value()) {
        return 1;
    }
    std::cout << "delivered to a stopped actor: " << *delivered << '\n';
    // end::stop[]
    return 0;
}
