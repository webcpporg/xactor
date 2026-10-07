// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The record of every delivery, in order: the evidence that two drivers, or
 two runs, delivered the same messages the same way.

 @see "The envelope log", in the guide.
*/
#ifndef WEBCPP_XACTOR_ENVELOPE_LOG_HPP
#define WEBCPP_XACTOR_ENVELOPE_LOG_HPP

#include <webcpp/xactor/envelope.hpp>
#include <webcpp/xactor/ids.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace webcpp::xactor {

/**
 One entry of an @ref envelope_log, a delivered @ref envelope without its
 message.

 @note It keeps which alternative of the message variant arrived, not the
 message, so an entry has a fixed width, the log is one type whatever the
 `Message`, and two logs compare cheaply.
*/
struct logged_envelope {
    /** The delivered envelope's @ref envelope::sequence. */
    sequence_number sequence{};

    /** The actor it was delivered to. */
    actor_ref to{};

    /**
     The actor that sent it: address zero when the host did, and the actor
     itself for the message of one of its timers.
    */
    actor_ref from{};

    /** The execution it belonged to. */
    correlation_id correlation{};

    /** The execution's fuel when the message was delivered, before its actor handled it. */
    std::uint32_t remaining_fuel = 0;

    /** Which alternative of the message variant it was, its `index()`. */
    std::size_t payload_index = 0;

    /**
     Whether two entries are equal, member by member.

     @return `true` when every member of one equals the same member of the
     other.
    */
    friend bool operator==(const logged_envelope&, const logged_envelope&) = default;
};

/**
 The entries of the messages a scheduler delivered, in order.

 @ref scheduler::run_one records each message it delivers, before its actor
 handles it, in the log it is given, and nothing for a message dropped. For
 the same budgets and deliveries every driver records the same log, so a
 program that runs the same deliveries twice and compares the logs checks
 that its own actors keep a run deterministic.

 @see "The envelope log", in the guide.
*/
class envelope_log {
public:
    /**
     Appends the entry of a delivered message; @ref scheduler::run_one calls
     it.

     @tparam Message The type of every message of the system, a
     `std::variant`, whose `index()` the entry keeps.
     @param delivered The envelope delivered.
     @param remaining_fuel The fuel its execution had when it was delivered.
    */
    template <class Message>
    void record(const envelope<Message>& delivered, std::uint32_t remaining_fuel) {
        entries_.push_back(logged_envelope{
            .sequence = delivered.sequence,
            .to = delivered.to,
            .from = delivered.from,
            .correlation = delivered.correlation,
            .remaining_fuel = remaining_fuel,
            .payload_index = delivered.payload.index(),
        });
    }

    /**
     The entries, in the order they were recorded.

     @return The entries, one for each delivery recorded in this log: a
     @ref scheduler::run_one given `nullptr`, or another log, records nothing
     here.
    */
    [[nodiscard]] const std::vector<logged_envelope>& entries() const noexcept { return entries_; }

    /**
     Whether two logs hold equal entries, in the same order.

     @param left One log.
     @param right The other log.
     @return `true` when both hold the same entries in the same order.
    */
    friend bool operator==(const envelope_log& left, const envelope_log& right) {
        return left.entries_ == right.entries_;
    }

private:
    std::vector<logged_envelope> entries_;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_ENVELOPE_LOG_HPP
