// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests the fuel of an execution: everything one host delivery causes is paid
// from one reservoir, whichever actor pays, and what cannot be paid for is
// refused with resource_limit. The messages are the test's own.

#include <webcpp/xactor.hpp>

#include <boost/core/lightweight_test.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "require.hpp"

namespace xactor = webcpp::xactor;

using webcpp::test::require;

namespace {

struct start {};

struct again {};

using message = std::variant<start, again>;

/** What a worker records for a test to assert. */
struct record {
    std::uint64_t refused = 0;
    boost::system::error_code last_error;
    std::vector<std::uint32_t> fuel_seen;
};

/**
 Sends `sends` messages to itself on every turn, after paying `cost` units
 for its own work; a refused send is counted and the turn goes on.
*/
class worker final : public xactor::actor_logic<message> {
public:
    worker(std::uint32_t sends, std::uint32_t cost, record& kept)
        : sends_(sends), cost_(cost), kept_(kept) {}

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        kept_.fuel_seen.push_back(turn.fuel());
        if (cost_ != 0) {
            const xactor::result<void> paid = turn.spend(cost_);
            if (!paid.has_value()) {
                kept_.last_error = paid.error();
                return paid;
            }
        }
        for (std::uint32_t sent = 0; sent < sends_; ++sent) {
            const xactor::result<void> delivered = turn.send(cause.to, again{});
            if (!delivered.has_value()) {
                ++kept_.refused;
                kept_.last_error = delivered.error();
            }
        }
        return {};
    }

private:
    std::uint32_t sends_;
    std::uint32_t cost_;
    record& kept_;
};

xactor::actor_ref spawn_worker(xactor::scheduler<message>& system, std::uint32_t sends,
                               std::uint32_t cost, record& kept) {
    const xactor::result<xactor::actor_ref> spawned =
        system.spawn(std::make_unique<worker>(sends, cost, kept));
    require(BOOST_TEST(spawned.has_value()));
    return *spawned;
}

void start_in(xactor::scheduler<message>& system, xactor::actor_ref actor,
              std::uint64_t execution) {
    require(BOOST_TEST(
        system.deliver(actor, start{}, xactor::correlation_id{.value = execution}).has_value()));
}

std::size_t run(xactor::scheduler<message>& system, xactor::envelope_log& log) {
    xactor::fifo_driver driver(system);
    const xactor::result<std::size_t> delivered = driver.run_until_idle(&log);
    require(BOOST_TEST(delivered.has_value()));
    return *delivered;
}

const boost::system::error_code resource_limit =
    xactor::make_error_code(xactor::errc::resource_limit);

void a_chain_of_messages_stops_when_its_execution_s_fuel_runs_out() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 5});
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 1, 0, kept);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 6U);
    BOOST_TEST_EQ(kept.refused, 1U);
    BOOST_TEST_EQ(kept.last_error, resource_limit);
    BOOST_TEST(system.status_of(actor).value() == xactor::status::active);
}

void messages_sent_side_by_side_share_their_execution_s_fuel() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 6});
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 2, 0, kept);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 7U);
    BOOST_TEST_EQ(kept.refused, 8U);
    BOOST_TEST_EQ(kept.last_error, resource_limit);
}

void an_actor_pays_for_its_own_work_from_its_execution_s_fuel() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 10});
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 1, 3, kept);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 3U);
    BOOST_TEST(system.status_of(actor).value() == xactor::status::error);
    BOOST_TEST_EQ(system.error_of(actor), resource_limit);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 1}), 2U);
}

void an_actor_sees_the_fuel_its_execution_has_left() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 4});
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 1, 0, kept);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    run(system, log);
    const std::vector<std::uint32_t> expected{4, 3, 2, 1, 0};
    BOOST_TEST_ALL_EQ(kept.fuel_seen.begin(), kept.fuel_seen.end(), expected.begin(),
                      expected.end());
    std::vector<std::uint32_t> recorded;
    for (const xactor::logged_envelope& entry : log.entries()) {
        recorded.push_back(entry.remaining_fuel);
    }
    BOOST_TEST_ALL_EQ(recorded.begin(), recorded.end(), expected.begin(), expected.end());
}

void each_execution_has_fuel_of_its_own() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 3});
    record kept_a;
    record kept_b;
    const xactor::actor_ref a = spawn_worker(system, 1, 0, kept_a);
    const xactor::actor_ref b = spawn_worker(system, 1, 0, kept_b);
    start_in(system, a, 1);
    start_in(system, b, 2);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 8U);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 1}), 0U);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 2}), 0U);
}

void a_host_delivery_costs_no_fuel() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 2});
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 0, 0, kept);
    start_in(system, actor, 1);
    start_in(system, actor, 1);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 3U);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 1}), 2U);
}

// Invariant: an execution not yet opened has the whole budget, whatever
// other executions have spent (doc: #xactor-invariant-9).
void an_execution_not_yet_opened_has_the_whole_budget() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 3});
    const xactor::correlation_id unopened{.value = 7};
    BOOST_TEST_EQ(system.fuel_of(unopened), 3U);
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 1, 0, kept);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    run(system, log);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 1}), 0U);
    BOOST_TEST_EQ(system.fuel_of(unopened), 3U);
}

// Invariant: nothing refills an execution: a later delivery with its
// correlation shares what it has left (doc: #xactor-invariant-6).
void a_later_delivery_shares_what_its_execution_has_left() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 2});
    record kept;
    const xactor::actor_ref actor = spawn_worker(system, 1, 0, kept);
    start_in(system, actor, 1);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 3U);
    start_in(system, actor, 1);
    BOOST_TEST_EQ(run(system, log), 1U);
    BOOST_TEST_EQ(kept.refused, 2U);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 1}), 0U);
}

/** What a refuser's calls returned, and the fuel left after them. */
struct refusals {
    xactor::result<void> reply_to_the_host;
    xactor::result<void> send_to_address_zero;
    xactor::result<void> send_past_the_last_actor;
    std::uint32_t fuel_left = 0;
};

/**
 On a message from the host, replies to it and sends to two addresses that
 name no actor, and records what each call returned.
*/
class refuser final : public xactor::actor_logic<message> {
public:
    explicit refuser(refusals& kept) : kept_(kept) {}

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& /*cause*/) override {
        kept_.reply_to_the_host = turn.reply(again{});
        kept_.send_to_address_zero = turn.send(xactor::actor_ref{.value = 0}, again{});
        kept_.send_past_the_last_actor = turn.send(xactor::actor_ref{.value = 2}, again{});
        kept_.fuel_left = turn.fuel();
        return {};
    }

private:
    refusals& kept_;
};

/** Handles nothing. */
class idle_logic final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& /*turn*/,
                                const xactor::envelope<message>& /*cause*/) override {
        return {};
    }
};

/** Each call a pricer made, and the fuel it cost. */
using price_list = std::vector<std::pair<std::string, std::uint32_t>>;

/**
 Asked once with `again`, makes each call of its turn and records what each
 cost, ending with stop or finish as told; it replies to its asker.
*/
class pricer final : public xactor::actor_logic<message> {
public:
    pricer(bool stops, price_list& prices) : stops_(stops), prices_(prices) {}

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (asked_ || !std::holds_alternative<again>(cause.payload)) {
            return {};
        }
        asked_ = true;
        const auto priced = [&turn, this](std::string name, const auto& call) {
            const std::uint32_t before = turn.fuel();
            call();
            prices_.emplace_back(std::move(name), before - turn.fuel());
        };
        priced("send", [&turn] { static_cast<void>(turn.send(turn.self(), again{})); });
        priced("reply", [&turn] { static_cast<void>(turn.reply(again{})); });
        priced("wake_at", [&turn] { static_cast<void>(turn.wake_at(10, again{})); });
        priced("wake_at with a key",
               [&turn] { static_cast<void>(turn.wake_at(10, again{}, "key")); });
        priced("spend", [&turn] { static_cast<void>(turn.spend(3)); });
        xactor::actor_ref child{};
        priced("spawn_child", [&turn, &child] { child = turn.spawn_child<idle_logic>().value(); });
        priced("stop_child", [&turn, &child] { static_cast<void>(turn.stop_child(child)); });
        priced("cancel_timer", [&turn] { turn.cancel_timer("key"); });
        priced("self, parent, now and fuel", [&turn] {
            static_cast<void>(turn.self());
            static_cast<void>(turn.parent());
            static_cast<void>(turn.now());
            static_cast<void>(turn.fuel());
        });
        if (stops_) {
            priced("stop", [&turn] { static_cast<void>(turn.stop()); });
        } else {
            priced("finish", [&turn] { static_cast<void>(turn.finish(again{})); });
        }
        return {};
    }

private:
    bool stops_;
    price_list& prices_;
    bool asked_ = false;
};

/** On start, asks the actor it was given with `again`. */
class asker final : public xactor::actor_logic<message> {
public:
    explicit asker(xactor::actor_ref asked) : asked_(asked) {}

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            return turn.send(asked_, again{});
        }
        return {};
    }

private:
    xactor::actor_ref asked_;
};

// Invariant: a reply to a message from the host, which has no sender, and a
// send to an address that names no actor are invalid_argument, and neither
// is paid (doc: #xactor-invariant-10).
void a_reply_to_the_host_and_a_send_to_no_actor_are_refused_unpaid() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 5});
    refusals kept;
    const xactor::result<xactor::actor_ref> actor = xactor::create_actor<refuser>(system, kept);
    if (!BOOST_TEST(actor.has_value())) {
        return;
    }
    start_in(system, *actor, 1);
    xactor::envelope_log log;
    BOOST_TEST_EQ(run(system, log), 1U);
    const boost::system::error_code invalid_argument =
        xactor::make_error_code(xactor::errc::invalid_argument);
    for (const xactor::result<void>* refused :
         {&kept.reply_to_the_host, &kept.send_to_address_zero, &kept.send_past_the_last_actor}) {
        if (!BOOST_TEST(!refused->has_value())) {
            return;
        }
        BOOST_TEST_EQ(refused->error(), invalid_argument);
    }
    BOOST_TEST_EQ(kept.fuel_left, 5U);
    BOOST_TEST_EQ(system.fuel_of(xactor::correlation_id{.value = 1}), 5U);
}

// Invariant: inside a turn, send, reply and wake_at cost one unit, spend(n)
// n units, and every other call nothing (doc: #xactor-invariant-7).
void each_call_of_a_turn_costs_what_it_says() {
    for (const bool stops : {false, true}) {
        xactor::scheduler<message> system(xactor::budgets{.fuel = 100});
        price_list prices;
        const xactor::result<xactor::actor_ref> priced =
            xactor::create_actor<pricer>(system, stops, prices);
        if (!BOOST_TEST(priced.has_value())) {
            return;
        }
        const xactor::result<xactor::actor_ref> asking =
            xactor::create_actor<asker>(system, *priced);
        if (!BOOST_TEST(asking.has_value())) {
            return;
        }
        start_in(system, *asking, 1);
        xactor::envelope_log log;
        run(system, log);
        const price_list expected{
            {"send", 1},
            {"reply", 1},
            {"wake_at", 1},
            {"wake_at with a key", 1},
            {"spend", 3},
            {"spawn_child", 0},
            {"stop_child", 0},
            {"cancel_timer", 0},
            {"self, parent, now and fuel", 0},
            {stops ? "stop" : "finish", 0},
        };
        BOOST_TEST(prices == expected);
    }
}

}  // namespace

int main() {
    a_chain_of_messages_stops_when_its_execution_s_fuel_runs_out();
    messages_sent_side_by_side_share_their_execution_s_fuel();
    an_actor_pays_for_its_own_work_from_its_execution_s_fuel();
    an_actor_sees_the_fuel_its_execution_has_left();
    each_execution_has_fuel_of_its_own();
    a_host_delivery_costs_no_fuel();
    an_execution_not_yet_opened_has_the_whole_budget();
    a_later_delivery_shares_what_its_execution_has_left();
    a_reply_to_the_host_and_a_send_to_no_actor_are_refused_unpaid();
    each_call_of_a_turn_costs_what_it_says();
    return boost::report_errors();
}
