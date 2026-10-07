// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// tag::includes[]
#include <webcpp/xactor.hpp>

#include <boost/mpl/vector.hpp>
#include <boost/msm/back/state_machine.hpp>
#include <boost/msm/front/functor_row.hpp>
#include <boost/msm/front/state_machine_def.hpp>

#include <array>
#include <iostream>
#include <type_traits>
#include <variant>

namespace xactor = webcpp::xactor;
namespace msm = boost::msm;
namespace mpl = boost::mpl;
// end::includes[]

namespace {

struct coin {};

struct push {};

struct alarm {};

struct refund {};

struct visit {
    xactor::actor_ref turnstile{};
};

using message = std::variant<visit, coin, push, alarm, refund>;

// tag::event[]
/** What the machine receives for a message: the message, and the turn it arrived in. */
template <class Event>
struct on {
    const Event& event;
    xactor::turn<message>& turn;
};

// end::event[]

// tag::chart[]
/** Replies alarm to a push the turnstile does not let through. */
struct sound_alarm {
    template <class Machine, class Source, class Target>
    void operator()(const on<push>& received, Machine& machine, Source& /*source*/,
                    Target& /*target*/) {
        machine.outcome = received.turn.reply(alarm{});
    }
};

/** Replies refund to a coin the turnstile does not need. */
struct give_refund {
    template <class Machine, class Source, class Target>
    void operator()(const on<coin>& received, Machine& machine, Source& /*source*/,
                    Target& /*target*/) {
        machine.outcome = received.turn.reply(refund{});
    }
};

/** A turnstile: a coin unlocks it, a push goes through and locks it again. */
struct turnstile_chart : msm::front::state_machine_def<turnstile_chart> {
    struct locked : msm::front::state<> {
        template <class Event, class Machine>
        void on_entry(const Event& /*event*/, Machine& /*machine*/) {
            std::cout << "turnstile: locked\n";
        }
    };

    struct unlocked : msm::front::state<> {
        template <class Event, class Machine>
        void on_entry(const Event& /*event*/, Machine& /*machine*/) {
            std::cout << "turnstile: unlocked\n";
        }
    };

    using initial_state = locked;
    using none = msm::front::none;

    // clang-format off
    struct transition_table : mpl::vector<
        msm::front::Row<locked,   on<coin>, unlocked, none,        none>,
        msm::front::Row<locked,   on<push>, none,     sound_alarm, none>,
        msm::front::Row<unlocked, on<push>, locked,   none,        none>,
        msm::front::Row<unlocked, on<coin>, none,     give_refund, none>
    > {};
    // clang-format on

    /** A message the turnstile has no transition for is ignored. */
    template <class Machine, class Event>
    void no_transition(const Event& /*event*/, Machine& /*machine*/, int /*state*/) {}

    xactor::result<void> outcome;
};

// end::chart[]

// tag::logic[]
/** A static actor: its behaviour is the turnstile's MSM machine, fixed when it is compiled. */
class turnstile final : public xactor::actor_logic<message> {
public:
    turnstile() { machine_.start(); }

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        machine_.outcome = {};
        std::visit(
            [&](const auto& received) {
                machine_.process_event(
                    on<std::decay_t<decltype(received)>>{.event = received, .turn = turn});
            },
            cause.payload);
        return machine_.outcome;
    }

private:
    msm::back::state_machine<turnstile_chart> machine_;
};

// end::logic[]

// tag::visitor[]
/** Puts a coin in, pushes twice, and puts two coins in; prints what comes back. */
class visitor final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (const auto* visiting = std::get_if<visit>(&cause.payload)) {
            const std::array<message, 5> plan{coin{}, push{}, push{}, coin{}, coin{}};
            for (const message& sent : plan) {
                const xactor::result<void> delivered = turn.send(visiting->turnstile, sent);
                if (!delivered.has_value()) {
                    return delivered;
                }
            }
            return {};
        }
        if (std::holds_alternative<alarm>(cause.payload)) {
            std::cout << "visitor: alarm\n";
        }
        if (std::holds_alternative<refund>(cause.payload)) {
            std::cout << "visitor: refund\n";
        }
        return {};
    }
};

// end::visitor[]

}  // namespace

int main() {
    // tag::run[]
    xactor::scheduler<message> system(xactor::budgets{.fuel = 100});
    const xactor::result<xactor::actor_ref> gate = xactor::create_actor<turnstile>(system);
    const xactor::result<xactor::actor_ref> guest = xactor::create_actor<visitor>(system);
    if (!gate.has_value() || !guest.has_value()) {
        return 1;
    }
    if (!system.deliver(*guest, visit{.turnstile = *gate}, xactor::correlation_id{.value = 1})
             .has_value()) {
        return 1;
    }
    xactor::fifo_driver<message> driver(system);
    if (!driver.run_until_idle(nullptr).has_value()) {
        return 1;
    }
    // end::run[]
    return 0;
}
