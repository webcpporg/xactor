// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The scheduler that gives each actor one message at a time, the turn an
 actor runs in, and `create_actor`.

 @see "The scheduler", in the guide.
 @see "Turns", in the guide.
*/
#ifndef WEBCPP_XACTOR_SCHEDULER_HPP
#define WEBCPP_XACTOR_SCHEDULER_HPP

#include <webcpp/xactor/actor_logic.hpp>
#include <webcpp/xactor/budgets.hpp>
#include <webcpp/xactor/envelope.hpp>
#include <webcpp/xactor/envelope_log.hpp>
#include <webcpp/xactor/errors.hpp>
#include <webcpp/xactor/ids.hpp>
#include <webcpp/xactor/status.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace webcpp::xactor {

template <class Message>
class turn;

template <class Message>
class scheduler;

/**
 The system that holds the actors and gives each of them one message at a
 time, run by its host.

 Each actor has a FIFO mailbox, and the actors with messages waiting wait in
 one FIFO queue, each at most once: @ref run_one takes the actor at the
 front, hands it the first message of its mailbox, and puts it back at the
 end while its mailbox holds more. A message an actor sends waits in a
 mailbox, and is never handled inside the sender's turn. The scheduler keeps
 each actor's logic, status, output, error and family for as long as it
 lives. It starts no thread and reads no clock: its time is what the host
 brings.

 @tparam Message The type of every message of the system: a `std::variant`
 that can be moved, whose `index()` the envelope log records.

 @note One FIFO mailbox per actor and one FIFO queue of actors with work make
 the delivery order a function of the enqueue order alone, which is what
 makes a run deterministic and lets two drivers produce one log. Fuel is kept
 per execution: every message caused by one host delivery, whatever actor
 sends it, is paid from that delivery's execution.

 @see "The scheduler", in the guide.
*/
template <class Message>
class scheduler {
public:
    /**
     Makes a system with no actor, at the time 0, whose executions each start
     with `limits.fuel`.

     @param limits The budgets it enforces.
    */
    explicit scheduler(budgets limits) noexcept : limits_(limits) {}

    /**
     Takes an actor into the system and gives it its address.

     The actor starts @ref status::active, with no parent, at the next
     address: addresses start at one. A test that keeps a pointer to the
     logic reads its members after each delivery: the scheduler never moves
     it.

     @param spawned The actor's logic, which the scheduler owns from then on.
     @return The new actor's address; @ref errc::invalid_argument for a null
     `spawned`, which takes no address.
    */
    result<actor_ref> spawn(std::unique_ptr<actor_logic<Message>> spawned) {
        if (spawned == nullptr) {
            return failure<actor_ref>(errc::invalid_argument);
        }
        slots_.push_back(slot{.behaviour = std::move(spawned)});
        return actor_ref{.value = static_cast<std::uint32_t>(slots_.size())};
    }

    /**
     Puts a message from the host in an actor's mailbox; it runs when a
     driver delivers it.

     The message's @ref envelope::from is address zero. A message to an
     actor that has ended is dropped, and the call succeeds.

     @param to The actor it is for.
     @param payload The message.
     @param correlation The execution it belongs to.
     @return Success; @ref errc::invalid_argument when `to` names no actor,
     which changes nothing.
     @note The first call that names a correlation, this one,
     @ref clock_tick or @ref release_next, opens its execution with the
     budget's fuel; a host delivery costs no fuel.
    */
    result<void> deliver(actor_ref to, Message payload, correlation_id correlation) {
        if (!names_an_actor(to)) {
            return failure<void>(errc::invalid_argument);
        }
        open(correlation);
        const actor_ref no_sender{.value = 0};
        enqueue(to, no_sender, correlation, std::move(payload));
        return {};
    }

    /**
     Delivers exactly one message to its actor and returns whether there was
     one.

     It takes the actor at the front of the queue, puts it back at the end
     while its mailbox holds more, records the message in `log`, when it is
     not `nullptr`, with the fuel its execution has, and calls the actor's
     @ref actor_logic::handle with the message and a @ref turn. A handler's
     error ends its actor with the status @ref status::error, unless the turn
     has already ended it.

     @param log The log that records the delivery, or `nullptr` for none.
     @return `true` when it delivered a message, `false` when none was
     waiting.
     @note A handler's error ends its actor, not the run, so this returns no
     error today; it returns a result so that a failure of the scheduler
     itself has a channel the drivers already forward.
    */
    result<bool> run_one(envelope_log* log);

    /**
     Moves time to `now`: releases every timer whose deadline has been
     reached, in (deadline, arming) order, into the execution `correlation`.

     Each timer's message enters its actor's mailbox, its sender the actor
     itself; releasing costs no fuel, since arming was paid. A time earlier
     than @ref now is not refused: time moves back. The call opens the
     execution `correlation` when it is new, as @ref deliver does.

     @param now The time the host brings.
     @param correlation The execution the released messages belong to.
     @return Success, always.
     @note Time never advances by itself; the host brings it.
    */
    result<void> clock_tick(std::uint64_t now, correlation_id correlation) {
        open(correlation);
        now_ = now;
        while (!timers_.empty() && timers_.begin()->first.first <= now) {
            const auto entry = timers_.begin();
            timer armed = std::move(entry->second);
            timers_.erase(entry);
            enqueue(armed.owner, armed.owner, correlation, std::move(armed.payload));
        }
        return {};
    }

    /**
     Moves time to `now` and releases the first timer due by then, in
     (deadline, arming) order, into the execution `correlation`.

     The timer is released as @ref clock_tick releases each of its own.

     @param now The time the host brings.
     @param correlation The execution the released message belongs to.
     @return `true` when it released a timer, `false` when none is due.
     @note A caller that runs the system between two releases lets a timer's
     handling cancel or replace a later timer of the same tick, which
     @ref clock_tick, releasing them all at once, cannot.
    */
    result<bool> release_next(std::uint64_t now, correlation_id correlation) {
        open(correlation);
        now_ = now;
        if (timers_.empty() || timers_.begin()->first.first > now) {
            return false;
        }
        const auto entry = timers_.begin();
        timer armed = std::move(entry->second);
        timers_.erase(entry);
        enqueue(armed.owner, armed.owner, correlation, std::move(armed.payload));
        return true;
    }

    /**
     Whether no message waits in any mailbox.

     @return `true` when @ref run_one would deliver nothing.
    */
    [[nodiscard]] bool idle() const noexcept { return runnable_.empty(); }

    /**
     The time the last @ref clock_tick or @ref release_next brought; 0 before
     any.

     @return The time.
    */
    [[nodiscard]] std::uint64_t now() const noexcept { return now_; }

    /**
     How many timers are armed and not yet released.

     @return The number of armed timers.
    */
    [[nodiscard]] std::size_t pending_timers() const noexcept { return timers_.size(); }

    /**
     The earliest deadline of an armed timer; none while no timer is armed.

     @return The earliest deadline, or `std::nullopt`.
    */
    [[nodiscard]] std::optional<std::uint64_t> next_deadline() const noexcept {
        if (timers_.empty()) {
            return std::nullopt;
        }
        return timers_.begin()->first.first;
    }

    /**
     The budgets the scheduler was constructed with.

     @return The budgets.
    */
    [[nodiscard]] const budgets& limits() const noexcept { return limits_; }

    /**
     The fuel an execution has left; the whole budget for one not yet opened.

     @param correlation The execution.
     @return The fuel it has left.
    */
    [[nodiscard]] std::uint32_t fuel_of(correlation_id correlation) const {
        const auto found = fuel_.find(correlation);
        return found == fuel_.end() ? limits_.fuel : found->second;
    }

    /**
     Ends an actor from outside with its whole family, as
     @ref turn::stop_child does.

     Each member that is still active is stopped after its own descendants,
     siblings in spawn order, and its pending messages and timers are
     dropped; a member that has already ended stays as it ended, and its
     descendants are stopped all the same.

     @param address The actor to stop, with its family.
     @return The actors it stopped, in the order it stopped them;
     @ref errc::invalid_argument when `address` names no actor, which
     changes nothing.
    */
    result<std::vector<actor_ref>> stop(actor_ref address) {
        if (!names_an_actor(address)) {
            return failure<std::vector<actor_ref>>(errc::invalid_argument);
        }
        return stop_family(address);
    }

    /**
     Where an actor is in its life.

     @param address The actor.
     @return Its status; @ref errc::invalid_argument when `address` names no
     actor.
    */
    [[nodiscard]] result<status> status_of(actor_ref address) const {
        if (!names_an_actor(address)) {
            return failure<status>(errc::invalid_argument);
        }
        return slots_[index_of(address)].state;
    }

    /**
     The output an actor finished with, or `nullptr` while it has none.

     @param address The actor.
     @return The output its turn gave @ref turn::finish; `nullptr` unless its
     status is @ref status::done, and for an address that names no actor.
     @note The pointer stays valid until the scheduler takes in another
     actor.
    */
    [[nodiscard]] const Message* output_of(actor_ref address) const {
        if (!names_an_actor(address)) {
            return nullptr;
        }
        const std::optional<Message>& output = slots_[index_of(address)].output;
        return output.has_value() ? &*output : nullptr;
    }

    /**
     The error an actor's handler returned, or an empty code.

     @param address The actor.
     @return The error, as the handler returned it; an empty code unless its
     status is @ref status::error, and for an address that names no actor.
    */
    [[nodiscard]] boost::system::error_code error_of(actor_ref address) const {
        if (!names_an_actor(address)) {
            return {};
        }
        return slots_[index_of(address)].failure;
    }

private:
    friend class turn<Message>;

    struct timer {
        actor_ref owner{};
        // No `{}`: a Message need not be default-constructible, and with
        // libstdc++ std::erase_if asks whether a timer is, which makes Clang
        // instantiate the initializer (doc: #xactor-invariant-28).
        Message payload;
        // A keyed timer replaces its owner's timer with the same key.
        std::optional<std::string> key{};
    };

    struct slot {
        std::unique_ptr<actor_logic<Message>> behaviour{};
        // The actor whose turn spawned it; none for an actor the host spawned.
        std::optional<actor_ref> parent{};
        // The actors its turns spawned, in spawn order, the order in which a
        // family's siblings are stopped.
        std::vector<actor_ref> children{};
        std::deque<envelope<Message>> mailbox{};
        status state = status::active;
        std::optional<Message> output{};
        boost::system::error_code failure{};
    };

    [[nodiscard]] bool names_an_actor(actor_ref address) const noexcept {
        return address.value != 0 && address.value <= slots_.size();
    }

    [[nodiscard]] static std::size_t index_of(actor_ref address) noexcept {
        return static_cast<std::size_t>(address.value) - 1;
    }

    [[nodiscard]] bool is_active(actor_ref address) const noexcept {
        return slots_[index_of(address)].state == status::active;
    }

    void open(correlation_id correlation) { fuel_.try_emplace(correlation, limits_.fuel); }

    /**
     Takes `units` of fuel from an execution, all of them or none.
    */
    result<void> take(correlation_id correlation, std::uint32_t units) {
        open(correlation);
        std::uint32_t& remaining = fuel_.find(correlation)->second;
        if (remaining < units) {
            return failure<void>(errc::resource_limit);
        }
        remaining -= units;
        return {};
    }

    /**
     Takes an actor out of the run: no longer queued, its pending messages
     and its timers dropped, never delivered and never logged.
    */
    void retire(actor_ref address, status final_state) {
        slot& retired = slots_[index_of(address)];
        retired.state = final_state;
        retired.mailbox.clear();
        runnable_.erase(std::ranges::remove(runnable_, address).begin(), runnable_.end());
        std::erase_if(timers_, [address](const auto& entry) {
            return entry.second.owner.value == address.value;
        });
    }

    /**
     Takes an actor spawned in `parent`'s turn into the system; an actor that
     is no longer active spawns nothing.

     @note `slots_` may grow here, so the parent's slot is found after the
     spawn.
    */
    result<actor_ref> spawn_under(std::unique_ptr<actor_logic<Message>> spawned, actor_ref parent) {
        if (!is_active(parent)) {
            return failure<actor_ref>(errc::invalid_argument);
        }
        const result<actor_ref> address = spawn(std::move(spawned));
        if (address.has_value()) {
            slots_[index_of(*address)].parent = parent;
            slots_[index_of(parent)].children.push_back(*address);
        }
        return address;
    }

    /**
     `root` and every actor descended from it, each after all of its own
     descendants, siblings in spawn order: the order a family is stopped in,
     which a caller may rely on.

     @note A preorder that visits the last-spawned child first, reversed, is
     that postorder, and an explicit stack follows a family of any depth
     without recursion.
    */
    [[nodiscard]] std::vector<actor_ref> family_of(actor_ref root) const {
        std::vector<actor_ref> order;
        std::vector<actor_ref> pending{root};
        while (!pending.empty()) {
            const actor_ref next = pending.back();
            pending.pop_back();
            order.push_back(next);
            const std::vector<actor_ref>& children = slots_[index_of(next)].children;
            pending.insert(pending.end(), children.begin(), children.end());
        }
        std::ranges::reverse(order);
        return order;
    }

    /**
     Stops every active member of `root`'s family, in family_of's order, and
     returns them in that order; one that has ended stays as it ended, and
     its descendants are stopped all the same.
    */
    std::vector<actor_ref> stop_family(actor_ref root) {
        std::vector<actor_ref> stopped;
        for (const actor_ref member : family_of(root)) {
            if (is_active(member)) {
                retire(member, status::stopped);
                stopped.push_back(member);
            }
        }
        return stopped;
    }

    /** Stops `child`, a child of `parent`, with its family. */
    result<std::vector<actor_ref>> stop_child(actor_ref parent, actor_ref child) {
        if (!names_an_actor(child) || slots_[index_of(child)].parent != parent) {
            return failure<std::vector<actor_ref>>(errc::invalid_argument);
        }
        return stop_family(child);
    }

    result<void> finish(actor_ref address, Message output) {
        slot& finished = slots_[index_of(address)];
        if (finished.state != status::active) {
            return failure<void>(errc::invalid_argument);
        }
        finished.output = std::move(output);
        retire(address, status::done);
        return {};
    }

    /**
     Puts a message in an actor's mailbox, and the actor in the queue if it
     was not there.

     @note A message to an actor that is not active is dropped, not refused:
     the sender cannot know the receiver has ended.
    */
    void enqueue(actor_ref to, actor_ref from, correlation_id correlation, Message payload) {
        slot& target = slots_[index_of(to)];
        if (target.state != status::active) {
            return;
        }
        next_sequence_ = next_sequence_.next();
        target.mailbox.push_back(envelope<Message>{
            .to = to,
            .from = from,
            .correlation = correlation,
            .sequence = next_sequence_,
            .payload = std::move(payload),
        });
        if (target.mailbox.size() == 1) {
            runnable_.push_back(to);
        }
    }

    /**
     Keeps a message for its owner until a clock_tick or a release_next
     reaches the deadline.

     @note The arming order breaks ties between equal deadlines, so no two
     entries collide and timers fire in one defined order.
    */
    void arm(actor_ref owner, std::uint64_t deadline, Message payload,
             std::optional<std::string> key = std::nullopt) {
        if (key.has_value()) {
            cancel(owner, *key);
        }
        next_timer_sequence_ = next_timer_sequence_.next();
        timers_.emplace(
            std::pair<std::uint64_t, std::uint64_t>{deadline, next_timer_sequence_.value},
            timer{.owner = owner, .payload = std::move(payload), .key = std::move(key)});
    }

    /** Removes `owner`'s timer under `key`, if it has one. */
    void cancel(actor_ref owner, std::string_view key) {
        std::erase_if(timers_, [owner, key](const auto& entry) {
            return entry.second.owner.value == owner.value && entry.second.key == key;
        });
    }

    budgets limits_;
    std::vector<slot> slots_;
    std::deque<actor_ref> runnable_;
    // The fuel each execution has left, opened by its first host delivery.
    std::map<correlation_id, std::uint32_t> fuel_;
    // Ordered by deadline, then by arming order, so a tick releases timers in
    // one defined order on every platform.
    std::map<std::pair<std::uint64_t, std::uint64_t>, timer> timers_;
    sequence_number next_sequence_;
    sequence_number next_timer_sequence_;
    std::uint64_t now_ = 0;
};

/**
 Runs a `Logic`, constructed from `inputs`, as a new actor of `system`, and
 returns its reference.

 The actor has no parent and takes the next address. `Message` is deduced
 from `system`, so a call names the logic alone:
 `xactor::create_actor<counter>(system, echo)`.

 @tparam Logic The actor's logic, a class derived from
 @ref actor_logic of `Message`.
 @tparam Message The type of every message of the system, deduced from
 `system`.
 @tparam Inputs The types of the arguments a `Logic` is constructed from.
 @param system The scheduler that runs the actor.
 @param inputs The arguments a `Logic` is constructed from, forwarded: the
 actor's input.
 @return The new actor's address. It never fails: the logic it gives
 @ref scheduler::spawn is never null.
 @note The actor is active at once, with no separate start: a caller that
 needs one starts it by delivering the actor its first message.
*/
template <class Logic, class Message, class... Inputs>
result<actor_ref> create_actor(scheduler<Message>& system, Inputs&&... inputs) {
    return system.spawn(std::make_unique<Logic>(std::forward<Inputs>(inputs)...));
}

/**
 An actor's turn: what it may do while it handles one message, the one
 delivery of the actor model.

 Every message it sends, every timer it arms and every unit it spends is
 paid from the fuel of the execution the message belongs to, and what the
 execution cannot pay fails with @ref errc::resource_limit and changes
 nothing. When @ref finish or @ref stop ends the actor, the rest of the turn
 still runs: the actor receives nothing more, and it can still send, but it
 arms no timer and spawns no child.

 @tparam Message The type of every message of the system.

 @note It lives for one handle and holds references to the scheduler and to
 the message, so a logic never keeps it.

 @see "Turns", in the guide.
*/
template <class Message>
class turn {
public:
    /**
     Makes the turn of the actor `cause` is for.

     @ref scheduler::run_one makes one for each message it delivers, and a
     program makes none.

     @param owner The scheduler that runs the actor.
     @param cause The envelope of the message being handled.
     @pre `owner` and `cause` outlive the turn, and `cause.to` names an actor
     of `owner`.
    */
    turn(scheduler<Message>& owner, const envelope<Message>& cause) noexcept
        : owner_(owner), cause_(cause) {}

    /**
     Sends a message to an actor, for one unit of fuel.

     The message waits in the mailbox of `to`, its sender this actor, and is
     handled in a turn of its own, later. A message to an actor that has
     ended is paid and dropped, and the call succeeds.

     @param to The actor it is for, this one included.
     @param payload The message.
     @return Success; @ref errc::invalid_argument, and nothing paid, when
     `to` names no actor; @ref errc::resource_limit when the execution has
     no fuel left.
    */
    result<void> send(actor_ref to, Message payload) {
        if (!owner_.names_an_actor(to)) {
            return failure<void>(errc::invalid_argument);
        }
        const result<void> paid = owner_.take(cause_.correlation, 1);
        if (!paid.has_value()) {
            return paid;
        }
        owner_.enqueue(to, cause_.to, cause_.correlation, std::move(payload));
        return {};
    }

    /**
     Sends a message to whoever sent the one being handled.

     It sends as @ref send does: to this actor itself for the message of one
     of its timers.

     @param payload The message.
     @return Success; @ref errc::invalid_argument, and nothing paid, for a
     message from the host, which has no sender; @ref errc::resource_limit
     when the execution has no fuel left.
    */
    result<void> reply(Message payload) { return send(cause_.from, std::move(payload)); }

    /**
     Runs a `Logic`, constructed from `inputs`, as a new actor of this
     system, a child of this one, and returns its reference.

     The child is @ref status::active at once, at the next address. It
     records this actor as its parent, and this actor records its children
     in spawn order. It costs no fuel.

     @tparam Logic The child's logic, a class derived from
     @ref actor_logic of `Message`.
     @tparam Inputs The types of the arguments a `Logic` is constructed from.
     @param inputs The arguments a `Logic` is constructed from, forwarded:
     the child's input.
     @return The child's address; @ref errc::invalid_argument once this
     actor has ended, before any `Logic` is constructed.
    */
    template <class Logic, class... Inputs>
    result<actor_ref> spawn_child(Inputs&&... inputs) {
        if (!owner_.is_active(cause_.to)) {
            return failure<actor_ref>(errc::invalid_argument);
        }
        return owner_.spawn_under(std::make_unique<Logic>(std::forward<Inputs>(inputs)...),
                                  cause_.to);
    }

    /**
     This actor's own reference.

     @return The address of the actor handling the message.
    */
    [[nodiscard]] actor_ref self() const noexcept { return cause_.to; }

    /**
     The actor whose turn spawned this one; none for one the host spawned.

     @return The parent's address, or `std::nullopt`.
    */
    [[nodiscard]] std::optional<actor_ref> parent() const {
        return owner_.slots_[owner_.index_of(cause_.to)].parent;
    }

    /**
     Stops a child of this actor and every active actor descended from it.

     Each is stopped after its own descendants, siblings in spawn order, and
     its pending messages and timers are dropped; a member that has already
     ended stays as it ended, and its descendants are stopped all the same.
     It costs no fuel.

     @param child The child to stop, with its family.
     @return The actors it stopped, in the order it stopped them;
     @ref errc::invalid_argument, which changes nothing, for anything but a
     child of this actor: the actor itself, a grandchild, an address that
     names no actor.
    */
    result<std::vector<actor_ref>> stop_child(actor_ref child) {
        return owner_.stop_child(cause_.to, child);
    }

    /**
     Ends this actor as stopped: nothing more reaches it, and its timers are
     dropped.

     It ends alone: its children are its logic's to stop, with
     @ref stop_child, as after @ref finish. It costs no fuel.

     @return Success; @ref errc::invalid_argument once this actor has ended.
     @note An actor its parent asks to stop with a message has handled what
     its parent sent it before, and may ask its own children the same way.
    */
    result<void> stop() {
        if (!owner_.is_active(cause_.to)) {
            return failure<void>(errc::invalid_argument);
        }
        owner_.retire(cause_.to, status::stopped);
        return {};
    }

    /**
     Ends this actor with its output: it becomes done and receives nothing
     more.

     Its timers are dropped, its children run on, and
     @ref scheduler::output_of returns the output. It costs no fuel.

     @param output The actor's output.
     @return Success; @ref errc::invalid_argument, which changes nothing,
     once this actor has ended: an actor finishes once.
    */
    result<void> finish(Message output) { return owner_.finish(cause_.to, std::move(output)); }

    /**
     Arms a timer on this actor, for one unit of fuel.

     `payload` comes back to this actor, its sender the actor itself, when
     the host's @ref scheduler::clock_tick or @ref scheduler::release_next
     reaches `deadline`; timers with one deadline are released in the order
     they were armed. A deadline is a time, so a delay is `now() + delay`.

     @param deadline The time it fires at.
     @param payload The message it brings back.
     @return Success; @ref errc::invalid_argument, and nothing paid, once this
     actor has ended; @ref errc::resource_limit when the execution has no
     fuel left.
     @see "Timers", in the guide.
    */
    result<void> wake_at(std::uint64_t deadline, Message payload) {
        return arm(deadline, std::move(payload), std::nullopt);
    }

    /**
     Arms a timer under `key`, for one unit of fuel, replacing this actor's
     timer with the same key.

     It fires as a timer without a key does, and the replacement takes a new
     arming order. Keys belong to their actor, so two actors may use the same
     one.

     @param deadline The time it fires at.
     @param payload The message it brings back.
     @param key The key the timer is armed under.
     @return Success; @ref errc::invalid_argument, and nothing paid, once this
     actor has ended; @ref errc::resource_limit when the execution has no
     fuel left. A call refused leaves the timer under `key` armed.
     @see "Timers", in the guide.
    */
    result<void> wake_at(std::uint64_t deadline, Message payload, std::string key) {
        return arm(deadline, std::move(payload), std::move(key));
    }

    /**
     Removes this actor's timer under `key`, if it has one.

     It costs no fuel.

     @param key The key the timer was armed under.
    */
    void cancel_timer(std::string_view key) { owner_.cancel(cause_.to, key); }

    /**
     The time the last @ref scheduler::clock_tick or
     @ref scheduler::release_next brought.

     @return The time, as @ref scheduler::now returns it; 0 before any.
    */
    [[nodiscard]] std::uint64_t now() const noexcept { return owner_.now_; }

    /**
     Pays `units` of fuel for work this actor does itself, all of them or
     none.

     An actor that computes without sending is bounded too.

     @param units The fuel to pay.
     @return Success; @ref errc::resource_limit, and nothing spent, when the
     execution has fewer left.
    */
    result<void> spend(std::uint32_t units) { return owner_.take(cause_.correlation, units); }

    /**
     The fuel this execution has left.

     @return The fuel the execution of the message being handled has left.
    */
    [[nodiscard]] std::uint32_t fuel() const { return owner_.fuel_of(cause_.correlation); }

private:
    result<void> arm(std::uint64_t deadline, Message payload, std::optional<std::string> key) {
        if (!owner_.is_active(cause_.to)) {
            return failure<void>(errc::invalid_argument);
        }
        const result<void> paid = owner_.take(cause_.correlation, 1);
        if (!paid.has_value()) {
            return paid;
        }
        owner_.arm(cause_.to, deadline, std::move(payload), std::move(key));
        return {};
    }

    scheduler<Message>& owner_;
    const envelope<Message>& cause_;
};

template <class Message>
result<bool> scheduler<Message>::run_one(envelope_log* log) {
    if (runnable_.empty()) {
        return false;
    }
    const actor_ref address = runnable_.front();
    runnable_.pop_front();
    std::deque<envelope<Message>>& mailbox = slots_[index_of(address)].mailbox;
    const envelope<Message> delivered = std::move(mailbox.front());
    mailbox.pop_front();
    // Back to the end of the queue while it holds work, so no actor starves
    // another however much it sends itself.
    if (!mailbox.empty()) {
        runnable_.push_back(address);
    }
    if (log != nullptr) {
        log->record(delivered, fuel_of(delivered.correlation));
    }
    turn<Message> current(*this, delivered);
    const result<void> handled = slots_[index_of(address)].behaviour->handle(current, delivered);
    // The handler may have spawned, so the slot is found again rather than
    // kept as a reference across the turn.
    if (!handled.has_value() && slots_[index_of(address)].state == status::active) {
        slots_[index_of(address)].failure = handled.error();
        retire(address, status::error);
    }
    return true;
}

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_SCHEDULER_HPP
