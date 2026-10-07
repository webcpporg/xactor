// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests xactor's guarantees (doc: #xactor-guarantees), the lifecycle: status,
// input, output, error and stop, and that an actor that is not active
// receives nothing. The messages are the test's own.

#include <webcpp/xactor.hpp>

#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <map>
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

// The test's own messages: a start, and the text an actor finishes with.
struct start {};

struct written {
    std::string text;
};

using message = std::variant<start, written>;
using actor_logic = xactor::actor_logic<message>;
using turn = xactor::turn<message>;
using envelope = xactor::envelope<message>;
using scheduler = xactor::scheduler<message>;

constexpr xactor::correlation_id one_execution{.value = 1};
constexpr xactor::budgets plenty{.fuel = 100};

// Its input is the text it finishes with, and it counts every turn it gets.
class finisher final : public actor_logic {
public:
    explicit finisher(std::string given) : given_(std::move(given)) {}

    xactor::result<void> handle(turn& turn, const envelope& /*cause*/) override {
        ++turns;
        first_finish = turn.finish(written{.text = given_});
        second_finish = turn.finish(written{.text = "again"});
        return {};
    }

    std::uint64_t turns = 0;
    xactor::result<void> first_finish;
    xactor::result<void> second_finish;

private:
    std::string given_;
};

// Fails on every turn, with the error it was given.
class failer final : public actor_logic {
public:
    xactor::result<void> handle(turn& /*turn*/, const envelope& /*cause*/) override {
        ++turns;
        return xactor::failure<void>(xactor::errc::resource_limit);
    }

    std::uint64_t turns = 0;
};

// Counts its turns and never ends by itself.
class counter final : public actor_logic {
public:
    xactor::result<void> handle(turn& /*turn*/, const envelope& /*cause*/) override {
        ++turns;
        return {};
    }

    std::uint64_t turns = 0;
};

// What the actors of a family record, owned by the test.
struct family_record {
    std::optional<xactor::actor_ref> child;
    std::optional<xactor::actor_ref> grandchild;
    std::optional<xactor::actor_ref> parent_seen_by_child;
    std::optional<xactor::actor_ref> self_seen_by_child;
    std::optional<xactor::actor_ref> parent_seen_by_root;
    xactor::result<std::vector<xactor::actor_ref>> stopped;
    std::uint64_t grandchild_turns = 0;
};

// Counts its turns into the family record.
class grandchild_logic final : public actor_logic {
public:
    explicit grandchild_logic(family_record& record) : record_(record) {}

    xactor::result<void> handle(turn& /*turn*/, const envelope& /*cause*/) override {
        ++record_.grandchild_turns;
        return {};
    }

private:
    family_record& record_;
};

// On start, records who it is and who its parent is, and spawns a grandchild.
class child_logic final : public actor_logic {
public:
    explicit child_logic(family_record& record) : record_(record) {}

    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (!std::holds_alternative<start>(cause.payload)) {
            return {};
        }
        record_.self_seen_by_child = turn.self();
        record_.parent_seen_by_child = turn.parent();
        const xactor::result<xactor::actor_ref> spawned =
            turn.spawn_child<grandchild_logic>(record_);
        if (spawned.has_value()) {
            record_.grandchild = *spawned;
        }
        return {};
    }

private:
    family_record& record_;
};

// On start, spawns and starts a child; on "stop child", stops it; on "stop
// stranger", tries to stop an actor that is not its child.
class parent_logic final : public actor_logic {
public:
    parent_logic(family_record& record, xactor::actor_ref stranger)
        : record_(record), stranger_(stranger) {}

    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            record_.parent_seen_by_root = turn.parent();
            const xactor::result<xactor::actor_ref> spawned =
                turn.spawn_child<child_logic>(record_);
            if (!spawned.has_value()) {
                return spawned.error();
            }
            record_.child = *spawned;
            return turn.send(*spawned, start{});
        }
        const auto* said = std::get_if<written>(&cause.payload);
        if (said != nullptr && said->text == "stop child" && record_.child.has_value()) {
            record_.stopped = turn.stop_child(*record_.child);
        } else if (said != nullptr && said->text == "stop stranger") {
            record_.stopped = turn.stop_child(stranger_);
        }
        return {};
    }

private:
    family_record& record_;
    xactor::actor_ref stranger_;
};

// What a tree of actors records, owned by the test: which actor spawns which,
// by name, and what each did.
struct tree_record {
    // (parent, child), in the order each parent spawns its children.
    std::vector<std::pair<std::string, std::string>> shape;
    std::map<std::string, xactor::actor_ref> refs;
    xactor::result<std::vector<xactor::actor_ref>> stopped = std::vector<xactor::actor_ref>{};
    xactor::result<void> stopped_self;
};

// On start, records its reference, arms a timer and spawns and starts the
// children `shape` gives it; on "finish", finishes; on "stop <name>", stops
// the actor of that name ("nobody" is address 0).
class tree_logic final : public actor_logic {
public:
    tree_logic(tree_record& record, std::string name) : record_(record), name_(std::move(name)) {}

    xactor::result<void> handle(turn& turn, const envelope& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            return grow(turn);
        }
        const std::string& text = std::get<written>(cause.payload).text;
        if (text == "finish") {
            return turn.finish(written{.text = name_});
        }
        if (text == "stop yourself") {
            record_.stopped_self = turn.stop();
            return {};
        }
        const std::string target = text.substr(std::string("stop ").size());
        const auto found = record_.refs.find(target);
        const xactor::actor_ref named =
            found == record_.refs.end() ? xactor::actor_ref{.value = 0} : found->second;
        record_.stopped = turn.stop_child(named);
        return {};
    }

private:
    xactor::result<void> grow(turn& turn) {
        record_.refs.insert_or_assign(name_, turn.self());
        if (const xactor::result<void> armed = turn.wake_at(100, start{}); !armed.has_value()) {
            return armed;
        }
        for (const auto& [parent, child] : record_.shape) {
            if (parent != name_) {
                continue;
            }
            const xactor::result<xactor::actor_ref> spawned =
                turn.spawn_child<tree_logic>(record_, child);
            if (!spawned.has_value()) {
                return spawned.error();
            }
            if (const xactor::result<void> sent = turn.send(*spawned, start{}); !sent.has_value()) {
                return sent;
            }
        }
        return {};
    }

    tree_record& record_;
    std::string name_;
};

// Counts its constructions in a count the test owns.
class counted final : public actor_logic {
public:
    explicit counted(std::uint64_t& constructions) { ++constructions; }

    xactor::result<void> handle(turn& /*turn*/, const envelope& /*cause*/) override { return {}; }
};

// Finishes on its first turn, then tries to spawn a child.
class late_spawner final : public actor_logic {
public:
    explicit late_spawner(std::uint64_t& constructions) : constructions_(constructions) {}

    xactor::result<void> handle(turn& turn, const envelope& /*cause*/) override {
        static_cast<void>(turn.finish(written{.text = "done"}));
        spawned = turn.spawn_child<counted>(constructions_);
        return {};
    }

    xactor::result<xactor::actor_ref> spawned = xactor::actor_ref{};

private:
    std::uint64_t& constructions_;
};

template <class Actor, class... Arguments>
std::pair<xactor::actor_ref, Actor*> spawn(scheduler& system, Arguments&&... arguments) {
    auto owned = std::make_unique<Actor>(std::forward<Arguments>(arguments)...);
    Actor* observed = owned.get();
    const xactor::result<xactor::actor_ref> address = system.spawn(std::move(owned));
    require(BOOST_TEST(address.has_value()));
    return {*address, observed};
}

std::uint64_t run_all(scheduler& system, xactor::envelope_log* log) {
    std::uint64_t delivered = 0;
    while (true) {
        const xactor::result<bool> ran = system.run_one(log);
        require(BOOST_TEST(ran.has_value()));
        if (!*ran) {
            return delivered;
        }
        ++delivered;
    }
}

// R spawns C; C spawns G1 then G2; G2 spawns GG2. That family is stopped, from
// C, as G1, GG2, G2, C: each actor after its descendants, siblings in spawn
// order.
std::pair<xactor::actor_ref, tree_logic*> grow_tree(scheduler& system, tree_record& record) {
    record.shape = {{"R", "C"}, {"C", "G1"}, {"C", "G2"}, {"G2", "GG2"}};
    const auto [root, observed] = spawn<tree_logic>(system, record, "R");
    require(BOOST_TEST(system.deliver(root, start{}, one_execution).has_value()));
    run_all(system, nullptr);
    require(BOOST_TEST_EQ(record.refs.size(), 5U));
    return {root, observed};
}

/** The names of `refs`, in order. */
std::vector<std::string> names_of(const tree_record& record,
                                  const std::vector<xactor::actor_ref>& refs) {
    std::vector<std::string> names;
    for (const xactor::actor_ref ref : refs) {
        for (const auto& [name, named] : record.refs) {
            if (named == ref) {
                names.push_back(name);
            }
        }
    }
    return names;
}

xactor::status status_named(const scheduler& system, const tree_record& record,
                            const std::string& name) {
    const auto found = record.refs.find(name);
    require(BOOST_TEST(found != record.refs.end()));
    return *system.status_of(found->second);
}

void a_spawned_actor_is_active_and_has_no_output() {
    scheduler system(plenty);
    const auto [address, observed] = spawn<counter>(system);
    BOOST_TEST(system.status_of(address).value() == xactor::status::active);
    BOOST_TEST_EQ(system.output_of(address), nullptr);
    BOOST_TEST(!system.error_of(address));
}

// Invariant: a logic that is not there runs as no actor.
void spawn_refuses_a_null_logic() {
    scheduler system(plenty);
    const xactor::result<xactor::actor_ref> refused = system.spawn(nullptr);
    if (!BOOST_TEST(!refused.has_value())) {
        return;
    }
    BOOST_TEST_EQ(refused.error(), xactor::make_error_code(xactor::errc::invalid_argument));
    BOOST_TEST(!system.status_of(xactor::actor_ref{.value = 1}).has_value());
}

void an_address_that_names_no_actor_has_no_status() {
    scheduler system(plenty);
    BOOST_TEST(!system.status_of(xactor::actor_ref{.value = 0}).has_value());
    BOOST_TEST(!system.status_of(xactor::actor_ref{.value = 1}).has_value());
    BOOST_TEST(!system.stop(xactor::actor_ref{.value = 1}).has_value());
}

void finish_makes_the_actor_done_with_its_input_as_output() {
    scheduler system(plenty);
    const auto [address, observed] = spawn<finisher>(system, "from the input");
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    BOOST_TEST_EQ(run_all(system, nullptr), 1U);

    BOOST_TEST(system.status_of(address).value() == xactor::status::done);
    BOOST_TEST(observed->first_finish.has_value());
    const message* output = system.output_of(address);
    if (!BOOST_TEST_NE(output, nullptr)) {
        return;
    }
    BOOST_TEST_EQ(std::get<written>(*output).text, "from the input");
}

void an_actor_finishes_once() {
    scheduler system(plenty);
    const auto [address, observed] = spawn<finisher>(system, "first");
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    run_all(system, nullptr);
    BOOST_TEST_EQ(observed->second_finish.error(),
                  xactor::make_error_code(xactor::errc::invalid_argument));
    BOOST_TEST_EQ(std::get<written>(*system.output_of(address)).text, "first");
}

void a_done_actor_receives_nothing_and_nothing_is_logged() {
    scheduler system(plenty);
    const auto [address, observed] = spawn<finisher>(system, "once");
    // Two envelopes wait in the mailbox before the first turn: the second is
    // dropped when the first finishes the actor.
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    xactor::envelope_log log;
    BOOST_TEST_EQ(run_all(system, &log), 1U);
    BOOST_TEST(system.deliver(address, start{}, one_execution).has_value());
    BOOST_TEST_EQ(run_all(system, &log), 0U);

    BOOST_TEST_EQ(observed->turns, 1U);
    BOOST_TEST_EQ(log.entries().size(), 1U);
    BOOST_TEST(system.idle());
}

void a_handler_error_ends_its_actor_and_not_the_run() {
    scheduler system(plenty);
    const auto [failing, failed] = spawn<failer>(system);
    const auto [other, counted] = spawn<counter>(system);
    if (!BOOST_TEST(system.deliver(failing, start{}, one_execution).has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(failing, start{}, one_execution).has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(other, start{}, one_execution).has_value())) {
        return;
    }

    BOOST_TEST_EQ(run_all(system, nullptr), 2U);

    BOOST_TEST(system.status_of(failing).value() == xactor::status::error);
    BOOST_TEST_EQ(system.error_of(failing), xactor::make_error_code(xactor::errc::resource_limit));
    BOOST_TEST_EQ(system.output_of(failing), nullptr);
    BOOST_TEST_EQ(failed->turns, 1U);
    BOOST_TEST(system.status_of(other).value() == xactor::status::active);
    BOOST_TEST_EQ(counted->turns, 1U);
}

void stop_drops_the_pending_envelopes() {
    scheduler system(plenty);
    const auto [address, observed] = spawn<counter>(system);
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    if (!BOOST_TEST(system.stop(address).has_value())) {
        return;
    }

    BOOST_TEST(system.status_of(address).value() == xactor::status::stopped);
    BOOST_TEST(system.idle());
    BOOST_TEST_EQ(run_all(system, nullptr), 0U);
    BOOST_TEST_EQ(observed->turns, 0U);
}

void stop_leaves_an_ended_actor_as_it_ended() {
    scheduler system(plenty);
    const auto [address, observed] = spawn<finisher>(system, "kept");
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    run_all(system, nullptr);
    if (!BOOST_TEST(system.stop(address).has_value())) {
        return;
    }
    BOOST_TEST(system.status_of(address).value() == xactor::status::done);
    BOOST_TEST_NE(system.output_of(address), nullptr);
}

void the_statuses_keep_their_persisted_values() {
    // A system that persists its actors stores these numbers, so they never change.
    BOOST_TEST_EQ(static_cast<int>(xactor::status::active), 1);
    BOOST_TEST_EQ(static_cast<int>(xactor::status::done), 2);
    BOOST_TEST_EQ(static_cast<int>(xactor::status::error), 3);
    BOOST_TEST_EQ(static_cast<int>(xactor::status::stopped), 4);
}

// Invariant: a child spawned in a turn knows its parent and itself; an actor
// the host spawned has no parent.
void a_spawned_child_knows_its_parent() {
    scheduler system(plenty);
    family_record record;
    const auto [stranger, unused] = spawn<counter>(system);
    const auto [parent, observed] = spawn<parent_logic>(system, record, stranger);
    static_cast<void>(system.deliver(parent, start{}, one_execution));
    run_all(system, nullptr);
    if (!BOOST_TEST(record.child.has_value())) {
        return;
    }
    if (!BOOST_TEST(record.parent_seen_by_child.has_value())) {
        return;
    }
    BOOST_TEST_EQ(record.parent_seen_by_child->value, parent.value);
    if (!BOOST_TEST(record.self_seen_by_child.has_value())) {
        return;
    }
    BOOST_TEST_EQ(record.self_seen_by_child->value, record.child->value);
    BOOST_TEST(!record.parent_seen_by_root.has_value());
}

// Invariant: stop_child stops a child of the actor and every descendant of
// it, their pending messages dropped; the parent stays active.
void stop_child_stops_the_child_and_its_descendants() {
    scheduler system(plenty);
    family_record record;
    const auto [stranger, unused] = spawn<counter>(system);
    const auto [parent, observed] = spawn<parent_logic>(system, record, stranger);
    static_cast<void>(system.deliver(parent, start{}, one_execution));
    run_all(system, nullptr);
    if (!BOOST_TEST(record.grandchild.has_value())) {
        return;
    }
    static_cast<void>(system.deliver(*record.grandchild, start{}, one_execution));
    static_cast<void>(system.deliver(parent, written{.text = "stop child"}, one_execution));
    // The queue holds the grandchild, then the parent: the grandchild handles
    // its message, then the parent stops it.
    if (!BOOST_TEST(system.run_one(nullptr).has_value())) {
        return;
    }
    BOOST_TEST_EQ(record.grandchild_turns, 1U);
    // This one waits behind the parent's "stop child" and is never handled.
    static_cast<void>(system.deliver(*record.grandchild, start{}, one_execution));
    run_all(system, nullptr);
    if (!BOOST_TEST(record.stopped.has_value())) {
        return;
    }
    BOOST_TEST_EQ(record.stopped->size(), 2U);
    BOOST_TEST(*system.status_of(*record.child) == xactor::status::stopped);
    BOOST_TEST(*system.status_of(*record.grandchild) == xactor::status::stopped);
    BOOST_TEST(*system.status_of(parent) == xactor::status::active);
    BOOST_TEST_EQ(record.grandchild_turns, 1U);
}

// Invariant: stop_child refuses an actor that is not a child of the caller,
// and leaves it as it was.
void stop_child_refuses_an_actor_that_is_not_a_child() {
    scheduler system(plenty);
    family_record record;
    const auto [stranger, unused] = spawn<counter>(system);
    const auto [parent, observed] = spawn<parent_logic>(system, record, stranger);
    static_cast<void>(system.deliver(parent, written{.text = "stop stranger"}, one_execution));
    run_all(system, nullptr);
    if (!BOOST_TEST(!record.stopped.has_value())) {
        return;
    }
    BOOST_TEST_EQ(record.stopped.error(), xactor::make_error_code(xactor::errc::invalid_argument));
    BOOST_TEST(*system.status_of(stranger) == xactor::status::active);
}

// Invariant: stop_child stops the family, each actor after its descendants and
// siblings in spawn order, returns the actors it stopped in that order, and
// drops their timers.
void stop_child_stops_a_family_descendants_first() {
    scheduler system(plenty);
    tree_record record;
    const auto [root, observed] = grow_tree(system, record);
    BOOST_TEST_EQ(system.pending_timers(), 5U);
    if (!BOOST_TEST(system.deliver(root, written{.text = "stop C"}, one_execution).has_value())) {
        return;
    }
    run_all(system, nullptr);
    if (!BOOST_TEST(record.stopped.has_value())) {
        return;
    }
    const std::vector<std::string> expected{"G1", "GG2", "G2", "C"};
    const std::vector<std::string> stopped = names_of(record, *record.stopped);
    BOOST_TEST_ALL_EQ(stopped.begin(), stopped.end(), expected.begin(), expected.end());
    for (const std::string& name : expected) {
        BOOST_TEST(status_named(system, record, name) == xactor::status::stopped);
    }
    BOOST_TEST(status_named(system, record, "R") == xactor::status::active);
    BOOST_TEST_EQ(system.pending_timers(), 1U);
}

// Invariant: stop_child refuses the actor itself, a grandchild and an address
// that names no actor, and changes nothing.
void stop_child_refuses_itself_a_grandchild_and_no_actor() {
    for (const std::string target : {"R", "G1", "nobody"}) {
        scheduler system(plenty);
        tree_record record;
        const auto [root, observed] = grow_tree(system, record);
        if (!BOOST_TEST(system.deliver(root, written{.text = "stop " + target}, one_execution)
                            .has_value())) {
            return;
        }
        run_all(system, nullptr);
        if (!BOOST_TEST(!record.stopped.has_value())) {
            return;
        }
        BOOST_TEST_EQ(record.stopped.error(),
                      xactor::make_error_code(xactor::errc::invalid_argument));
        BOOST_TEST_EQ(system.pending_timers(), 5U);
        for (const auto& [name, ref] : record.refs) {
            BOOST_TEST(*system.status_of(ref) == xactor::status::active);
        }
    }
}

// Invariant: stopping a child that already finished leaves it done and still
// stops its descendants.
void stop_child_of_a_done_child_still_stops_its_descendants() {
    scheduler system(plenty);
    tree_record record;
    const auto [root, observed] = grow_tree(system, record);
    if (!BOOST_TEST(system.deliver(record.refs.at("C"), written{.text = "finish"}, one_execution)
                        .has_value())) {
        return;
    }
    if (!BOOST_TEST(system.deliver(root, written{.text = "stop C"}, one_execution).has_value())) {
        return;
    }
    run_all(system, nullptr);
    if (!BOOST_TEST(record.stopped.has_value())) {
        return;
    }
    const std::vector<std::string> expected{"G1", "GG2", "G2"};
    const std::vector<std::string> stopped = names_of(record, *record.stopped);
    BOOST_TEST_ALL_EQ(stopped.begin(), stopped.end(), expected.begin(), expected.end());
    BOOST_TEST(status_named(system, record, "C") == xactor::status::done);
}

// Invariant: the host's stop stops the actor and its whole family, in the
// order stop_child stops one, and returns them.
void the_host_s_stop_stops_the_whole_family() {
    scheduler system(plenty);
    tree_record record;
    const auto [root, observed] = grow_tree(system, record);
    const xactor::result<std::vector<xactor::actor_ref>> stopped = system.stop(root);
    if (!BOOST_TEST(stopped.has_value())) {
        return;
    }
    const std::vector<std::string> expected{"G1", "GG2", "G2", "C", "R"};
    const std::vector<std::string> names = names_of(record, *stopped);
    BOOST_TEST_ALL_EQ(names.begin(), names.end(), expected.begin(), expected.end());
    BOOST_TEST_EQ(system.pending_timers(), 0U);
    BOOST_TEST(system.idle());
}

// Invariant: an actor that is no longer active spawns no child, and
// constructs no logic for one.
void a_retired_actor_spawns_nothing() {
    scheduler system(plenty);
    std::uint64_t constructions = 0;
    const auto [address, observed] = spawn<late_spawner>(system, constructions);
    if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
        return;
    }
    run_all(system, nullptr);
    if (!BOOST_TEST(!observed->spawned.has_value())) {
        return;
    }
    BOOST_TEST_EQ(observed->spawned.error(),
                  xactor::make_error_code(xactor::errc::invalid_argument));
    BOOST_TEST(!system.status_of(xactor::actor_ref{.value = 2}).has_value());
    BOOST_TEST_EQ(constructions, 0U);
}

// Invariant: an actor that stops itself ends alone, as finish does: its
// children are its logic's to stop, and run on.
void an_actor_that_stops_itself_ends_alone() {
    scheduler system(plenty);
    tree_record record;
    static_cast<void>(grow_tree(system, record));
    if (!BOOST_TEST(
            system.deliver(record.refs.at("C"), written{.text = "stop yourself"}, one_execution)
                .has_value())) {
        return;
    }
    run_all(system, nullptr);
    BOOST_TEST(record.stopped_self.has_value());
    BOOST_TEST(status_named(system, record, "C") == xactor::status::stopped);
    for (const std::string name : {"R", "G1", "G2", "GG2"}) {
        BOOST_TEST(status_named(system, record, name) == xactor::status::active);
    }
    BOOST_TEST_EQ(system.pending_timers(), 4U);
}

// How an actor ends twice in one turn.
enum class twice { stop_then_stop, stop_then_finish, finish_then_stop };

// Ends on its first turn as told, and keeps what the second ending returned.
class ending_twice final : public actor_logic {
public:
    explicit ending_twice(twice how) : how_(how) {}

    xactor::result<void> handle(turn& turn, const envelope& /*cause*/) override {
        if (how_ == twice::finish_then_stop) {
            static_cast<void>(turn.finish(written{.text = "done"}));
        } else {
            static_cast<void>(turn.stop());
        }
        second =
            how_ == twice::stop_then_finish ? turn.finish(written{.text = "late"}) : turn.stop();
        return {};
    }

    xactor::result<void> second;

private:
    twice how_;
};

// Ends on its first turn, by finish or by stop, then returns an error.
class failing_once_ended final : public actor_logic {
public:
    explicit failing_once_ended(bool finishes) : finishes_(finishes) {}

    xactor::result<void> handle(turn& turn, const envelope& /*cause*/) override {
        if (finishes_) {
            static_cast<void>(turn.finish(written{.text = "done"}));
        } else {
            static_cast<void>(turn.stop());
        }
        return xactor::failure<void>(xactor::errc::resource_limit);
    }

private:
    bool finishes_;
};

// On start, finishes first when told to, then sends a line to the actor it
// was given; keeps what the send returned and the fuel it cost.
class messenger final : public actor_logic {
public:
    messenger(xactor::actor_ref to, bool finishes_first)
        : to_(to), finishes_first_(finishes_first) {}

    xactor::result<void> handle(turn& turn, const envelope& /*cause*/) override {
        if (finishes_first_) {
            static_cast<void>(turn.finish(written{.text = "done"}));
        }
        const std::uint32_t before = turn.fuel();
        sent = turn.send(to_, written{.text = "hello"});
        cost = before - turn.fuel();
        return {};
    }

    xactor::result<void> sent = xactor::failure<void>(xactor::errc::invalid_argument);
    std::uint32_t cost = 0;

private:
    xactor::actor_ref to_;
    bool finishes_first_;
};

// Invariant: an actor ends once: once it has ended, a turn's stop and finish
// are invalid_argument and change nothing
// (doc: #xactor-invariant-24, doc: #xactor-invariant-35).
void an_ended_actor_neither_stops_nor_finishes_again() {
    struct expectation {
        twice how = twice::stop_then_stop;
        xactor::status ended = xactor::status::active;
        bool has_output = false;
    };

    const std::vector<expectation> expectations{
        {.how = twice::stop_then_stop, .ended = xactor::status::stopped, .has_output = false},
        {.how = twice::stop_then_finish, .ended = xactor::status::stopped, .has_output = false},
        {.how = twice::finish_then_stop, .ended = xactor::status::done, .has_output = true},
    };
    for (const expectation& expected : expectations) {
        scheduler system(plenty);
        const auto [address, observed] = spawn<ending_twice>(system, expected.how);
        if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
            return;
        }
        run_all(system, nullptr);
        if (!BOOST_TEST(!observed->second.has_value())) {
            return;
        }
        BOOST_TEST_EQ(observed->second.error(),
                      xactor::make_error_code(xactor::errc::invalid_argument));
        BOOST_TEST(system.status_of(address).value() == expected.ended);
        BOOST_TEST_EQ(system.output_of(address) != nullptr, expected.has_output);
    }
}

// Invariant: an error that handle returns in the turn that already ended its
// actor changes nothing: the actor stays as it ended
// (doc: #xactor-invariant-4).
void an_error_after_the_actor_ended_changes_nothing() {
    for (const bool finishes : {true, false}) {
        scheduler system(plenty);
        const auto [address, observed] = spawn<failing_once_ended>(system, finishes);
        if (!BOOST_TEST(system.deliver(address, start{}, one_execution).has_value())) {
            return;
        }
        BOOST_TEST_EQ(run_all(system, nullptr), 1U);
        const xactor::status ended = finishes ? xactor::status::done : xactor::status::stopped;
        BOOST_TEST(system.status_of(address).value() == ended);
        BOOST_TEST(!system.error_of(address));
    }
}

// Invariant: an actor still sends in the rest of the turn that ended it, and
// pays for it (doc: #xactor-invariant-33).
void an_actor_still_sends_in_the_turn_that_ended_it() {
    scheduler system(plenty);
    const auto [listener, counted] = spawn<counter>(system);
    const auto [sender, observed] = spawn<messenger>(system, listener, true);
    if (!BOOST_TEST(system.deliver(sender, start{}, one_execution).has_value())) {
        return;
    }
    BOOST_TEST_EQ(run_all(system, nullptr), 2U);
    BOOST_TEST(system.status_of(sender).value() == xactor::status::done);
    BOOST_TEST(observed->sent.has_value());
    BOOST_TEST_EQ(observed->cost, 1U);
    BOOST_TEST_EQ(counted->turns, 1U);
}

// Invariant: a send to an actor that has ended succeeds and is paid, and its
// message is dropped, never delivered and never logged
// (doc: #xactor-invariant-23).
void a_message_to_an_ended_actor_is_paid_and_dropped() {
    scheduler system(plenty);
    const auto [ended, finished] = spawn<finisher>(system, "ended");
    if (!BOOST_TEST(system.deliver(ended, start{}, one_execution).has_value())) {
        return;
    }
    run_all(system, nullptr);
    const auto [sender, observed] = spawn<messenger>(system, ended, false);
    const xactor::correlation_id second_execution{.value = 2};
    if (!BOOST_TEST(system.deliver(sender, start{}, second_execution).has_value())) {
        return;
    }
    xactor::envelope_log log;
    BOOST_TEST_EQ(run_all(system, &log), 1U);
    BOOST_TEST(observed->sent.has_value());
    BOOST_TEST_EQ(observed->cost, 1U);
    BOOST_TEST_EQ(system.fuel_of(second_execution), plenty.fuel - 1);
    BOOST_TEST_EQ(finished->turns, 1U);
    BOOST_TEST_EQ(log.entries().size(), 1U);
}

}  // namespace

int main() {
    a_spawned_actor_is_active_and_has_no_output();
    spawn_refuses_a_null_logic();
    an_address_that_names_no_actor_has_no_status();
    finish_makes_the_actor_done_with_its_input_as_output();
    an_actor_finishes_once();
    a_done_actor_receives_nothing_and_nothing_is_logged();
    a_handler_error_ends_its_actor_and_not_the_run();
    stop_drops_the_pending_envelopes();
    stop_leaves_an_ended_actor_as_it_ended();
    the_statuses_keep_their_persisted_values();
    a_spawned_child_knows_its_parent();
    stop_child_stops_the_child_and_its_descendants();
    stop_child_refuses_an_actor_that_is_not_a_child();
    stop_child_stops_a_family_descendants_first();
    stop_child_refuses_itself_a_grandchild_and_no_actor();
    stop_child_of_a_done_child_still_stops_its_descendants();
    the_host_s_stop_stops_the_whole_family();
    a_retired_actor_spawns_nothing();
    an_actor_that_stops_itself_ends_alone();
    an_ended_actor_neither_stops_nor_finishes_again();
    an_error_after_the_actor_ended_changes_nothing();
    an_actor_still_sends_in_the_turn_that_ended_it();
    a_message_to_an_ended_actor_is_paid_and_dropped();
    return boost::report_errors();
}
