// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests xactor's guarantees (doc: #xactor-guarantees): delivery order, the
// drivers, timers and the test driver. The messages are the test's own, so
// these tests prove the library needs nothing of its users'; the fuel of an
// execution is specified by fuel_test.cpp.
//
// The Asio driver exists where drivers.hpp declares it, outside WASI, and is
// tested there; a WASI build checks every other driver.

#include <webcpp/xactor.hpp>

#include <boost/core/lightweight_test.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "require.hpp"

namespace xactor = webcpp::xactor;

using webcpp::test::require;

namespace {

// The test's own messages: a start, and a line of text with its number.
struct start {};

struct line {
    std::string text;
    std::uint64_t number = 0;
};

using message = std::variant<start, line>;
using actor_logic = xactor::actor_logic<message>;
using turn = xactor::turn<message>;
using envelope = xactor::envelope<message>;
using scheduler = xactor::scheduler<message>;

constexpr xactor::correlation_id one_execution{1};
constexpr xactor::budgets plenty{.fuel = 1000};

// Records every line it is handed, in the order it was handed them.
class recorder final : public actor_logic {
public:
    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        static_cast<void>(turn);
        if (const auto* received = std::get_if<line>(&cause.payload)) {
            seen.push_back(received->text);
        }
        return {};
    }

    std::vector<std::string> seen;
};

// Appends its own label and the line it was handed to a log shared with the
// other actors, so one vector holds the global delivery order.
class labelled_recorder final : public actor_logic {
public:
    labelled_recorder(std::string label, std::vector<std::string>& shared)
        : label_(std::move(label)), shared_(shared) {}

    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        static_cast<void>(turn);
        if (const auto* received = std::get_if<line>(&cause.payload)) {
            shared_.push_back(label_ + received->text);
        }
        return {};
    }

private:
    std::string label_;
    std::vector<std::string>& shared_;
};

// Arms two timers on its first turn and records every timer that fires.
class timer_arming_actor final : public actor_logic {
public:
    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            static_cast<void>(turn.wake_at(20, line{.text = "late", .number = 20}));
            static_cast<void>(turn.wake_at(10, line{.text = "early", .number = 10}));
            static_cast<void>(turn.wake_at(10, line{.text = "also early", .number = 11}));
            return {};
        }
        if (const auto* received = std::get_if<line>(&cause.payload)) {
            fired.push_back(received->text);
        }
        return {};
    }

    std::vector<std::string> fired;
};

// Arms keyed timers on start: "first" under "k" replaced by "second", and
// "other" under "j" cancelled. Records every timer that fires, and the time
// its turn sees.
class keyed_timer_actor final : public actor_logic {
public:
    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            static_cast<void>(turn.wake_at(10, line{.text = "first"}, "k"));
            static_cast<void>(turn.wake_at(20, line{.text = "second"}, "k"));
            static_cast<void>(turn.wake_at(15, line{.text = "other"}, "j"));
            turn.cancel_timer("j");
            return {};
        }
        if (const auto* received = std::get_if<line>(&cause.payload)) {
            fired.push_back(received->text);
            seen_now.push_back(turn.now());
        }
        return {};
    }

    std::vector<std::string> fired;
    std::vector<std::uint64_t> seen_now;
};

// On start, arms "kept" under the key "k"; on the line "rearm", spends the
// execution's fuel and then tries to replace it; records every other line.
class rearming_actor final : public actor_logic {
public:
    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            return turn.wake_at(10, line{.text = "kept"}, "k");
        }
        const std::string& text = std::get<line>(cause.payload).text;
        if (text != "rearm") {
            fired.push_back(text);
            return {};
        }
        static_cast<void>(turn.spend(turn.fuel()));
        rearmed = turn.wake_at(20, line{.text = "replacement"}, "k");
        return {};
    }

    std::vector<std::string> fired;
    xactor::result<void> rearmed;
};

// How an actor that armed a timer ends its first turn.
enum class ending { finish, fail, finish_then_arm };

// On start, arms a timer, then ends as told.
class ending_actor final : public actor_logic {
public:
    explicit ending_actor(ending how) : how_(how) {}

    xactor::result<void> handle(turn& turn, const envelope& /*cause*/) override {
        if (how_ == ending::finish_then_arm) {
            static_cast<void>(turn.finish(line{.text = "done"}));
            late_timer = turn.wake_at(5, line{.text = "late"});
            return {};
        }
        static_cast<void>(turn.wake_at(5, line{.text = "armed"}));
        if (how_ == ending::finish) {
            return turn.finish(line{.text = "done"});
        }
        return xactor::failure<void>(xactor::errc::resource_limit);
    }

    xactor::result<void> late_timer;

private:
    ending how_;
};

// Invariant: envelopes in one mailbox are handled in the order they were pushed.
void fifo_order_delivers_n_posts_in_order() {
    scheduler system(plenty);
    auto owned = std::make_unique<recorder>();
    recorder& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }

    constexpr std::uint64_t post_count = 32;
    for (std::uint64_t index = 0; index < post_count; ++index) {
        const xactor::result<void> delivered = system.deliver(
            *address, line{.text = std::to_string(index), .number = index}, one_execution);
        if (!BOOST_TEST(delivered.has_value())) {
            return;
        }
    }

    std::uint64_t deliveries = 0;
    while (true) {
        const xactor::result<bool> ran = system.run_one(nullptr);
        if (!BOOST_TEST(ran.has_value())) {
            return;
        }
        if (!*ran) {
            break;
        }
        ++deliveries;
    }

    BOOST_TEST_EQ(deliveries, post_count);
    BOOST_TEST_EQ(observed.seen.size(), post_count);
    for (std::uint64_t index = 0; index < post_count; ++index) {
        BOOST_TEST_EQ(observed.seen[index], std::to_string(index));
    }
    BOOST_TEST(system.idle());
}

// Invariant: run_one takes the front runnable actor and re-queues it at the
// back while its mailbox is not empty, so delivery is FIFO across actors and
// not only within one mailbox. A single actor cannot show this: its mailbox is
// the only thing in the runnable queue, and any queue discipline looks alike.
void fifo_order_holds_across_actors() {
    scheduler system(plenty);
    std::vector<std::string> order;
    const xactor::result<xactor::actor_ref> first =
        system.spawn(std::make_unique<labelled_recorder>("a", order));
    const xactor::result<xactor::actor_ref> second =
        system.spawn(std::make_unique<labelled_recorder>("b", order));
    if (!BOOST_TEST(first.has_value())) {
        return;
    }
    if (!BOOST_TEST(second.has_value())) {
        return;
    }

    // Both mailboxes are filled before anything runs, so the delivery order is
    // decided by the runnable queue rather than by the arrival of new work.
    for (const xactor::actor_ref address : {*first, *second}) {
        for (std::uint64_t index = 1; index <= 2; ++index) {
            static_cast<void>(system.deliver(
                address, line{.text = std::to_string(index), .number = index}, one_execution));
        }
    }

    xactor::fifo_driver driver(system);
    const xactor::result<std::size_t> delivered = driver.run_until_idle(nullptr);
    if (!BOOST_TEST(delivered.has_value())) {
        return;
    }
    BOOST_TEST_EQ(*delivered, 4U);

    const std::vector<std::string> expected{"a1", "b1", "a2", "b2"};
    BOOST_TEST_ALL_EQ(order.begin(), order.end(), expected.begin(), expected.end());
}

#ifndef __wasi__

// Invariant: both drivers produce the same envelope log for the same
// deliveries, which is what lets a WASI build carry the FIFO driver alone.
void both_drivers_produce_the_same_envelope_log() {
    const auto run_with_fifo = [] {
        scheduler system(plenty);
        auto owned = std::make_unique<recorder>();
        const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
        xactor::envelope_log log;
        for (std::uint64_t index = 0; index < 8; ++index) {
            static_cast<void>(system.deliver(
                *address, line{.text = std::to_string(index), .number = index}, one_execution));
        }
        xactor::fifo_driver driver(system);
        static_cast<void>(driver.run_until_idle(&log));
        return log;
    };

    const auto run_with_asio = [] {
        scheduler system(plenty);
        auto owned = std::make_unique<recorder>();
        const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
        xactor::envelope_log log;
        for (std::uint64_t index = 0; index < 8; ++index) {
            static_cast<void>(system.deliver(
                *address, line{.text = std::to_string(index), .number = index}, one_execution));
        }
        xactor::asio_driver driver(system);
        static_cast<void>(driver.run_until_idle(&log));
        return log;
    };

    const xactor::envelope_log from_fifo = run_with_fifo();
    const xactor::envelope_log from_asio = run_with_asio();
    BOOST_TEST_EQ(from_fifo.entries().size(), 8U);
    BOOST_TEST(from_fifo == from_asio);
}

#endif  // __wasi__

// Invariant: clock_tick fires every timer whose deadline has passed, in
// (deadline, sequence) order, and nothing fires between ticks.
void clock_tick_fires_timers_in_deadline_then_sequence_order() {
    scheduler system(plenty);
    auto owned = std::make_unique<timer_arming_actor>();
    timer_arming_actor& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }

    static_cast<void>(system.deliver(*address, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));

    // Nothing fires without a tick, however long the driver runs.
    BOOST_TEST(observed.fired.empty());

    // A tick before the first deadline releases nothing.
    static_cast<void>(system.clock_tick(9, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST(observed.fired.empty());

    // A tick at the deadline releases the two entries of that deadline, in the
    // order they were armed, and leaves the later one alone.
    static_cast<void>(system.clock_tick(10, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    if (!BOOST_TEST_EQ(observed.fired.size(), 2U)) {
        return;
    }
    BOOST_TEST_EQ(observed.fired[0], "early");
    BOOST_TEST_EQ(observed.fired[1], "also early");

    static_cast<void>(system.clock_tick(25, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    if (!BOOST_TEST_EQ(observed.fired.size(), 3U)) {
        return;
    }
    BOOST_TEST_EQ(observed.fired[2], "late");
}

// Invariant: a test_driver hands its actor exactly one envelope per step.
void test_driver_delivers_exactly_one_message_per_step() {
    scheduler system(plenty);
    auto owned = std::make_unique<recorder>();
    const recorder& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }

    xactor::test_driver driver(system);
    for (std::uint64_t index = 0; index < 3; ++index) {
        static_cast<void>(system.deliver(
            *address, line{.text = std::to_string(index), .number = index}, one_execution));
    }

    BOOST_TEST(observed.seen.empty());
    for (std::uint64_t index = 0; index < 3; ++index) {
        const xactor::result<bool> stepped = driver.step();
        if (!BOOST_TEST(stepped.has_value())) {
            return;
        }
        BOOST_TEST(*stepped);
        BOOST_TEST_EQ(observed.seen.size(), index + 1);
    }

    const xactor::result<bool> idle_step = driver.step();
    if (!BOOST_TEST(idle_step.has_value())) {
        return;
    }
    BOOST_TEST(!*idle_step);
    BOOST_TEST_EQ(observed.seen.size(), 3U);
}

// Invariant: a keyed timer replaces the same actor's timer with that key, a
// cancelled one never fires, and the actor's turn sees the time the tick
// brought.
void a_keyed_timer_replaces_the_one_with_its_key_and_a_cancelled_one_never_fires() {
    scheduler system(plenty);
    auto owned = std::make_unique<keyed_timer_actor>();
    keyed_timer_actor& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*address, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.pending_timers(), 1U);

    static_cast<void>(system.clock_tick(30, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    if (!BOOST_TEST_EQ(observed.fired.size(), 1U)) {
        return;
    }
    BOOST_TEST_EQ(observed.fired.front(), "second");
    BOOST_TEST_EQ(observed.seen_now.front(), 30U);
    BOOST_TEST_EQ(system.pending_timers(), 0U);
}

// Invariant: two actors' timers under one key are two timers.
void keys_belong_to_their_actor() {
    scheduler system(plenty);
    auto first = std::make_unique<keyed_timer_actor>();
    auto second = std::make_unique<keyed_timer_actor>();
    const keyed_timer_actor& first_observed = *first;
    const keyed_timer_actor& second_observed = *second;
    const xactor::result<xactor::actor_ref> one = system.spawn(std::move(first));
    const xactor::result<xactor::actor_ref> two = system.spawn(std::move(second));
    if (!BOOST_TEST(one.has_value() && two.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*one, start{}, one_execution));
    static_cast<void>(system.deliver(*two, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.pending_timers(), 2U);
    static_cast<void>(system.clock_tick(20, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(first_observed.fired.size(), 1U);
    BOOST_TEST_EQ(second_observed.fired.size(), 1U);
}

// Invariant: an actor that stops loses its timers.
void a_retired_actor_s_timers_are_dropped() {
    scheduler system(plenty);
    auto owned = std::make_unique<timer_arming_actor>();
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*address, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.pending_timers(), 3U);
    static_cast<void>(system.stop(*address));
    BOOST_TEST_EQ(system.pending_timers(), 0U);
}

// Invariant: now is 0 before any tick, and the time of the last tick after.
void now_is_the_last_clock_tick() {
    scheduler system(plenty);
    BOOST_TEST_EQ(system.now(), 0U);
    static_cast<void>(system.clock_tick(42, one_execution));
    BOOST_TEST_EQ(system.now(), 42U);
    static_cast<void>(system.clock_tick(7, one_execution));
    BOOST_TEST_EQ(system.now(), 7U);
}

// Invariant: a keyed wake_at refused for fuel changes nothing: the timer
// under its key stays armed and fires.
void a_refused_keyed_timer_leaves_the_one_with_its_key() {
    scheduler system(xactor::budgets{.fuel = 5});
    auto owned = std::make_unique<rearming_actor>();
    const rearming_actor& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*address, start{}, one_execution));
    static_cast<void>(
        system.deliver(*address, line{.text = "rearm"}, xactor::correlation_id{.value = 2}));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(observed.rearmed.error(), xactor::make_error_code(xactor::errc::resource_limit));
    static_cast<void>(system.clock_tick(10, xactor::correlation_id{.value = 3}));
    static_cast<void>(driver.run_until_idle(nullptr));
    if (!BOOST_TEST_EQ(observed.fired.size(), 1U)) {
        return;
    }
    BOOST_TEST_EQ(observed.fired.front(), "kept");
}

// Invariant: finishing and failing drop the actor's timers, as stopping does.
void finishing_and_failing_drop_the_actor_s_timers() {
    for (const ending how : {ending::finish, ending::fail}) {
        scheduler system(plenty);
        const xactor::result<xactor::actor_ref> address =
            system.spawn(std::make_unique<ending_actor>(how));
        if (!BOOST_TEST(address.has_value())) {
            return;
        }
        static_cast<void>(system.deliver(*address, start{}, one_execution));
        xactor::fifo_driver driver(system);
        static_cast<void>(driver.run_until_idle(nullptr));
        BOOST_TEST(*system.status_of(*address) != xactor::status::active);
        BOOST_TEST_EQ(system.pending_timers(), 0U);
    }
}

// Invariant: an actor that is no longer active arms no timer and pays
// nothing for trying.
void a_retired_actor_arms_no_timer() {
    scheduler system(plenty);
    auto owned = std::make_unique<ending_actor>(ending::finish_then_arm);
    const ending_actor& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*address, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(observed.late_timer.error(),
                  xactor::make_error_code(xactor::errc::invalid_argument));
    BOOST_TEST_EQ(system.pending_timers(), 0U);
    BOOST_TEST_EQ(system.fuel_of(one_execution), plenty.fuel);
}

// On start, arms three timers out of deadline order.
class unordered_timers_actor final : public actor_logic {
public:
    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            static_cast<void>(turn.wake_at(30, line{.text = "late"}));
            static_cast<void>(turn.wake_at(10, line{.text = "early"}));
            static_cast<void>(turn.wake_at(20, line{.text = "middle"}));
        }
        return {};
    }
};

// Invariant: next_deadline is the earliest armed deadline, whatever the order
// the timers were armed in, none without a timer; a replaced or cancelled
// keyed timer counts no more.
void next_deadline_is_the_earliest_armed_timer() {
    scheduler system(plenty);
    BOOST_TEST(!system.next_deadline().has_value());
    const xactor::result<xactor::actor_ref> unordered =
        system.spawn(std::make_unique<unordered_timers_actor>());
    if (!BOOST_TEST(unordered.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*unordered, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.next_deadline().value(), 10U);
    static_cast<void>(system.clock_tick(10, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.next_deadline().value(), 20U);
    static_cast<void>(system.clock_tick(30, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST(!system.next_deadline().has_value());

    scheduler keyed(plenty);
    const xactor::result<xactor::actor_ref> address =
        keyed.spawn(std::make_unique<keyed_timer_actor>());
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    static_cast<void>(keyed.deliver(*address, start{}, one_execution));
    xactor::fifo_driver keyed_driver(keyed);
    static_cast<void>(keyed_driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(keyed.next_deadline().value(), 20U);
}

// Invariant: release_next releases one due timer at a time, the earliest
// first, and says when none is due.
void release_next_releases_one_due_timer_at_a_time() {
    scheduler system(plenty);
    auto owned = std::make_unique<timer_arming_actor>();
    const timer_arming_actor& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*address, start{}, one_execution));
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    std::vector<std::size_t> heard;
    while (system.release_next(15, one_execution).value()) {
        static_cast<void>(driver.run_until_idle(nullptr));
        heard.push_back(observed.fired.size());
    }
    const std::vector<std::size_t> one_by_one{1, 2};
    BOOST_TEST_ALL_EQ(heard.begin(), heard.end(), one_by_one.begin(), one_by_one.end());
    const std::vector<std::string> fired{"early", "also early"};
    BOOST_TEST_ALL_EQ(observed.fired.begin(), observed.fired.end(), fired.begin(), fired.end());
    BOOST_TEST_EQ(system.now(), 15U);
    BOOST_TEST_EQ(system.pending_timers(), 1U);
}

// Invariant: deliver refuses an address that names no actor, and changes
// nothing (doc: #xactor-scheduler).
void deliver_refuses_an_address_that_names_no_actor() {
    scheduler system(plenty);
    if (!BOOST_TEST(system.spawn(std::make_unique<recorder>()).has_value())) {
        return;
    }
    for (const std::uint32_t nowhere : {0U, 2U}) {
        const xactor::result<void> refused =
            system.deliver(xactor::actor_ref{.value = nowhere}, start{}, one_execution);
        if (!BOOST_TEST(!refused.has_value())) {
            return;
        }
        BOOST_TEST_EQ(refused.error(), xactor::make_error_code(xactor::errc::invalid_argument));
    }
    BOOST_TEST(system.idle());
}

// Invariant: run_turn(n) delivers at most n messages, fewer when none is
// left, and run_until_idle returns only once none is
// (doc: #xactor-invariant-14).
void run_turn_delivers_at_most_its_budget() {
    scheduler system(plenty);
    auto owned = std::make_unique<recorder>();
    const recorder& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    const auto deliver_lines = [&system, &address](std::uint64_t count) {
        for (std::uint64_t index = 0; index < count; ++index) {
            require(BOOST_TEST(
                system.deliver(*address, line{.text = std::to_string(index)}, one_execution)
                    .has_value()));
        }
    };
    deliver_lines(5);
    xactor::fifo_driver driver(system);
    BOOST_TEST_EQ(driver.run_turn(2, nullptr).value(), 2U);
    BOOST_TEST_EQ(observed.seen.size(), 2U);
    BOOST_TEST(!system.idle());
    BOOST_TEST_EQ(driver.run_turn(0, nullptr).value(), 0U);
    BOOST_TEST_EQ(driver.run_turn(5, nullptr).value(), 3U);
    BOOST_TEST(system.idle());
    deliver_lines(2);
    BOOST_TEST_EQ(driver.run_until_idle(nullptr).value(), 2U);
    BOOST_TEST(system.idle());
    BOOST_TEST_EQ(observed.seen.size(), 7U);
}

// On start, arms "first" under the key "k", then "plain" without a key, then
// "replacement" under "k" again, all due at 10; records every timer that
// fires.
class rearming_in_place_actor final : public actor_logic {
public:
    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            static_cast<void>(turn.wake_at(10, line{.text = "first"}, "k"));
            static_cast<void>(turn.wake_at(10, line{.text = "plain"}));
            static_cast<void>(turn.wake_at(10, line{.text = "replacement"}, "k"));
            return {};
        }
        fired.push_back(std::get<line>(cause.payload).text);
        return {};
    }

    std::vector<std::string> fired;
};

// On start, sends a line to the actor it was given.
class forwarder final : public actor_logic {
public:
    explicit forwarder(xactor::actor_ref to) : to_(to) {}

    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            return turn.send(to_, line{.text = "forwarded", .number = 1});
        }
        return {};
    }

private:
    xactor::actor_ref to_;
};

// Invariant: a replacement timer takes a new arming order, so among the
// timers of one deadline it fires after those armed before it
// (doc: #xactor-invariant-11).
void a_replacement_timer_takes_a_new_arming_order() {
    scheduler system(plenty);
    auto owned = std::make_unique<rearming_in_place_actor>();
    const rearming_in_place_actor& observed = *owned;
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(*address, start{}, one_execution).has_value())) {
        return;
    }
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.pending_timers(), 2U);
    static_cast<void>(system.clock_tick(10, one_execution));
    static_cast<void>(driver.run_until_idle(nullptr));
    const std::vector<std::string> fired{"plain", "replacement"};
    BOOST_TEST_ALL_EQ(observed.fired.begin(), observed.fired.end(), fired.begin(), fired.end());
}

// Invariant: a timer that clock_tick or release_next releases belongs to
// that call's execution, comes from its own actor, and costs nothing to
// release (doc: #xactor-invariant-13).
void a_released_timer_belongs_to_the_releasing_execution_for_free() {
    scheduler system(xactor::budgets{.fuel = 3});
    const xactor::result<xactor::actor_ref> address =
        system.spawn(std::make_unique<unordered_timers_actor>());
    if (!BOOST_TEST(address.has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(*address, start{}, one_execution).has_value())) {
        return;
    }
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(nullptr));
    BOOST_TEST_EQ(system.fuel_of(one_execution), 0U);

    const xactor::correlation_id ticked{.value = 2};
    const xactor::correlation_id released{.value = 3};
    xactor::envelope_log log;
    static_cast<void>(system.clock_tick(10, ticked));
    static_cast<void>(driver.run_until_idle(&log));
    if (!BOOST_TEST(system.release_next(20, released).value())) {
        return;
    }
    static_cast<void>(driver.run_until_idle(&log));

    const std::vector<xactor::logged_envelope> expected{
        {
            .sequence = {.value = 2},
            .to = *address,
            .from = *address,
            .correlation = ticked,
            .remaining_fuel = 3,
            .payload_index = 1,
        },
        {
            .sequence = {.value = 3},
            .to = *address,
            .from = *address,
            .correlation = released,
            .remaining_fuel = 3,
            .payload_index = 1,
        },
    };
    BOOST_TEST(log.entries() == expected);
    BOOST_TEST_EQ(system.fuel_of(ticked), 3U);
    BOOST_TEST_EQ(system.fuel_of(released), 3U);
}

// Invariant: the log records, for each message delivered and before its
// actor handles it, the envelope's sequence, receiver, sender and execution,
// the fuel its execution had, and which alternative of the message it was
// (doc: #xactor-invariant-16).
void the_log_records_each_delivery_as_it_was_made() {
    scheduler system(xactor::budgets{.fuel = 7});
    const xactor::result<xactor::actor_ref> receiver = system.spawn(std::make_unique<recorder>());
    if (!BOOST_TEST(receiver.has_value())) {
        return;
    }
    const xactor::result<xactor::actor_ref> sender =
        system.spawn(std::make_unique<forwarder>(*receiver));
    if (!BOOST_TEST(sender.has_value())) {
        return;
    }
    const xactor::correlation_id execution{.value = 5};
    if (!BOOST_TEST(system.deliver(*sender, start{}, execution).has_value())) {
        return;
    }
    xactor::envelope_log log;
    xactor::fifo_driver driver(system);
    static_cast<void>(driver.run_until_idle(&log));

    const std::vector<xactor::logged_envelope> expected{
        {
            .sequence = {.value = 1},
            .to = *sender,
            .from = {.value = 0},
            .correlation = execution,
            .remaining_fuel = 7,
            .payload_index = 0,
        },
        {
            .sequence = {.value = 2},
            .to = *receiver,
            .from = *sender,
            .correlation = execution,
            .remaining_fuel = 6,
            .payload_index = 1,
        },
    };
    BOOST_TEST(log.entries() == expected);
}

// A message that can be moved, but neither copied nor default-constructed.
struct ticket {
    explicit ticket(std::uint64_t number) : number(std::make_unique<std::uint64_t>(number)) {}

    std::unique_ptr<std::uint64_t> number;
};

using ticket_message = std::variant<ticket>;

// Handed a ticket above zero, arms a keyed timer, due one after the time its
// turn sees, that brings the ticket one below; handed the ticket zero,
// finishes with it.
class ticket_counter final : public xactor::actor_logic<ticket_message> {
public:
    xactor::result<void> handle(xactor::turn<ticket_message>& turn,
                                const xactor::envelope<ticket_message>& cause) override {
        const std::uint64_t number = *std::get<ticket>(cause.payload).number;
        if (number == 0) {
            return turn.finish(ticket{0});
        }
        return turn.wake_at(turn.now() + 1, ticket{number - 1}, "next");
    }
};

// Invariant: the scheduler, its turns, its drivers and its log ask of a
// message only to be a std::variant that can be moved: this one can be
// neither copied nor default-constructed (doc: #xactor-invariant-28).
void a_message_need_only_be_a_variant_that_can_be_moved() {
    xactor::scheduler<ticket_message> system(xactor::budgets{.fuel = 10});
    const xactor::result<xactor::actor_ref> counter = xactor::create_actor<ticket_counter>(system);
    if (!BOOST_TEST(counter.has_value())) {
        return;
    }
    if (!BOOST_TEST(
            system.deliver(*counter, ticket{3}, xactor::correlation_id{.value = 1}).has_value())) {
        return;
    }
    xactor::envelope_log log;
    xactor::fifo_driver fifo(system);
    BOOST_TEST_EQ(fifo.run_until_idle(&log).value(), 1U);

    static_cast<void>(system.clock_tick(1, xactor::correlation_id{.value = 2}));
#ifndef __wasi__
    xactor::asio_driver asio(system);
    const xactor::result<std::size_t> ticked = asio.run_until_idle(&log);
#else
    // No Asio driver on WASI (drivers.hpp): the FIFO one delivers in its place.
    const xactor::result<std::size_t> ticked = fifo.run_until_idle(&log);
#endif
    BOOST_TEST_EQ(ticked.value(), 1U);

    if (!BOOST_TEST(system.release_next(2, xactor::correlation_id{.value = 3}).value())) {
        return;
    }
    xactor::test_driver one_at_a_time(system);
    BOOST_TEST(one_at_a_time.step(log).value());

    static_cast<void>(system.clock_tick(3, xactor::correlation_id{.value = 4}));
    BOOST_TEST_EQ(fifo.run_turn(5, &log).value(), 1U);

    BOOST_TEST(system.status_of(*counter).value() == xactor::status::done);
    const ticket_message* output = system.output_of(*counter);
    if (!BOOST_TEST_NE(output, nullptr)) {
        return;
    }
    BOOST_TEST_EQ(*std::get<ticket>(*output).number, 0U);
    BOOST_TEST_EQ(log.entries().size(), 4U);
}

// Answers whoever sent it a ticket with the ticket one above.
class ticket_echo final : public xactor::actor_logic<ticket_message> {
public:
    xactor::result<void> handle(xactor::turn<ticket_message>& turn,
                                const xactor::envelope<ticket_message>& cause) override {
        return turn.reply(ticket{*std::get<ticket>(cause.payload).number + 1});
    }
};

// Handed a ticket by the host, spawns an echo and sends it the ticket; handed
// the echo's answer, arms a timer, and a keyed one it cancels; handed the
// timer's ticket, stops the echo and fails.
class ticket_parent final : public xactor::actor_logic<ticket_message> {
public:
    explicit ticket_parent(xactor::actor_ref& echo) : echo_(echo) {}

    xactor::result<void> handle(xactor::turn<ticket_message>& turn,
                                const xactor::envelope<ticket_message>& cause) override {
        const std::uint64_t number = *std::get<ticket>(cause.payload).number;
        if (cause.from.value == 0) {
            const xactor::result<xactor::actor_ref> child = turn.spawn_child<ticket_echo>();
            if (!child.has_value()) {
                return child.error();
            }
            echo_ = *child;
            return turn.send(echo_, ticket{number});
        }
        if (cause.from == echo_) {
            const xactor::result<void> armed = turn.wake_at(turn.now() + 1, ticket{number});
            if (!armed.has_value()) {
                return armed;
            }
            const xactor::result<void> keyed = turn.wake_at(turn.now() + 1, ticket{0}, "dropped");
            if (!keyed.has_value()) {
                return keyed;
            }
            turn.cancel_timer("dropped");
            return {};
        }
        static_cast<void>(turn.stop_child(echo_));
        return xactor::failure<void>(xactor::errc::resource_limit);
    }

private:
    xactor::actor_ref& echo_;
};

// Stops itself on the first ticket it is handed.
class ticket_quitter final : public xactor::actor_logic<ticket_message> {
public:
    xactor::result<void> handle(xactor::turn<ticket_message>& turn,
                                const xactor::envelope<ticket_message>& cause) override {
        static_cast<void>(cause);
        return turn.stop();
    }
};

// Invariant: a message that can be neither copied nor default-constructed
// passes every public path that carries an envelope: one a program builds,
// the log, send and reply, spawn_child and stop_child, a timer and a keyed one
// cancelled, a turn's stop, the host's stop and a failing handler
// (doc: #xactor-invariant-28).
void a_move_only_message_passes_every_path_that_carries_an_envelope() {
    xactor::envelope_log log;
    const xactor::envelope<ticket_message> built{
        .to = {.value = 1},
        .from = {.value = 0},
        .correlation = {.value = 9},
        .sequence = {.value = 1},
        .payload = ticket{7},
    };
    log.record(built, 3);

    xactor::scheduler<ticket_message> system(xactor::budgets{.fuel = 10});
    xactor::actor_ref echo{};
    const xactor::result<xactor::actor_ref> parent =
        xactor::create_actor<ticket_parent>(system, echo);
    const xactor::result<xactor::actor_ref> quitter = xactor::create_actor<ticket_quitter>(system);
    const xactor::result<xactor::actor_ref> idle = xactor::create_actor<ticket_echo>(system);
    if (!BOOST_TEST(parent.has_value() && quitter.has_value() && idle.has_value())) {
        return;
    }
    const xactor::correlation_id first{.value = 1};
    if (!BOOST_TEST(system.deliver(*parent, ticket{4}, first).has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(*quitter, ticket{1}, first).has_value())) {
        return;
    }
    xactor::fifo_driver fifo(system);
    BOOST_TEST_EQ(fifo.run_until_idle(&log).value(), 4U);
    BOOST_TEST_EQ(system.pending_timers(), 1U);
    BOOST_TEST(system.next_deadline() == std::optional<std::uint64_t>{1});

    static_cast<void>(system.clock_tick(1, xactor::correlation_id{.value = 2}));
    BOOST_TEST_EQ(fifo.run_until_idle(&log).value(), 1U);
    const xactor::result<std::vector<xactor::actor_ref>> stopped = system.stop(*idle);

    BOOST_TEST(system.status_of(*parent).value() == xactor::status::error);
    BOOST_TEST_EQ(system.error_of(*parent), xactor::make_error_code(xactor::errc::resource_limit));
    BOOST_TEST(system.status_of(echo).value() == xactor::status::stopped);
    BOOST_TEST(system.status_of(*quitter).value() == xactor::status::stopped);
    if (!BOOST_TEST(stopped.has_value())) {
        return;
    }
    BOOST_TEST(*stopped == std::vector<xactor::actor_ref>{*idle});
    BOOST_TEST(system.status_of(*idle).value() == xactor::status::stopped);
    BOOST_TEST_EQ(log.entries().size(), 6U);
    BOOST_TEST_EQ(log.entries().front().remaining_fuel, 3U);
}

}  // namespace

int main() {
    fifo_order_delivers_n_posts_in_order();
    fifo_order_holds_across_actors();
#ifndef __wasi__
    both_drivers_produce_the_same_envelope_log();
#endif
    clock_tick_fires_timers_in_deadline_then_sequence_order();
    test_driver_delivers_exactly_one_message_per_step();
    a_keyed_timer_replaces_the_one_with_its_key_and_a_cancelled_one_never_fires();
    keys_belong_to_their_actor();
    a_retired_actor_s_timers_are_dropped();
    now_is_the_last_clock_tick();
    a_refused_keyed_timer_leaves_the_one_with_its_key();
    finishing_and_failing_drop_the_actor_s_timers();
    a_retired_actor_arms_no_timer();
    next_deadline_is_the_earliest_armed_timer();
    release_next_releases_one_due_timer_at_a_time();
    deliver_refuses_an_address_that_names_no_actor();
    run_turn_delivers_at_most_its_budget();
    a_replacement_timer_takes_a_new_arming_order();
    a_released_timer_belongs_to_the_releasing_execution_for_free();
    the_log_records_each_delivery_as_it_was_made();
    a_message_need_only_be_a_variant_that_can_be_moved();
    a_move_only_message_passes_every_path_that_carries_an_envelope();
    return boost::report_errors();
}
