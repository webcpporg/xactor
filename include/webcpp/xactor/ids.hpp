// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The identities of an actor system: an actor's address, the execution a
 message belongs to, and the order of deliveries.

 @note They are distinct types, so an address is never passed where an
 execution is expected.

 @see "The model", in the guide.
*/
#ifndef WEBCPP_XACTOR_IDS_HPP
#define WEBCPP_XACTOR_IDS_HPP

#include <compare>
#include <cstdint>

namespace webcpp::xactor {

/**
 An actor's address in its scheduler.

 It is a number, not a handle: it keeps nothing alive, and
 @ref scheduler::status_of tells whether its actor still runs. Addresses
 start at 1 and follow the order the actors were spawned in, by the host or
 by a turn, and an address is never reused: an actor that has ended keeps
 its address and its status. Address zero names no actor; it is the
 @ref envelope::from of a message the host sent.
*/
struct actor_ref {
    /** The address: 1 for the first actor spawned, and 0 for none. */
    std::uint32_t value = 0;

    /**
     Whether two addresses are the same.

     @return `true` when both name the same actor, or both name none.
    */
    friend bool operator==(actor_ref, actor_ref) = default;
};

/**
 The execution every message caused by one host delivery belongs to; the
 execution's fuel is kept under it.

 The host names it with each @ref scheduler::deliver,
 @ref scheduler::clock_tick and @ref scheduler::release_next, and every
 message sent in the turns that follow belongs to it. The host chooses the
 values: a delivery with a correlation already used shares what its
 execution has left, so a host gives each input a correlation of its own.
*/
struct correlation_id {
    /** The number the host gave the execution. */
    std::uint64_t value = 0;

    /**
     Whether two correlations name the same execution.

     @return `true` when their values are equal.
    */
    friend bool operator==(correlation_id, correlation_id) = default;

    /**
     Orders two correlations by their values, so that one can key an ordered
     container.

     @return The order of their values.
    */
    friend auto operator<=>(correlation_id, correlation_id) = default;
};

/**
 A message's place in the order messages entered a mailbox, across every
 actor of a scheduler, which gives each its number as it enqueues it.
*/
struct sequence_number {
    /** The place, counted from 1 for the first message enqueued. */
    std::uint64_t value = 0;

    /**
     The number after this one.

     @return The sequence number whose value is one more than this one's.
    */
    [[nodiscard]] sequence_number next() const noexcept { return sequence_number{value + 1}; }

    /**
     Whether two places are the same.

     @return `true` when their values are equal.
    */
    friend bool operator==(sequence_number, sequence_number) = default;

    /**
     Orders two places, the earlier first.

     @return The order of their values.
    */
    friend auto operator<=>(sequence_number, sequence_number) = default;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_IDS_HPP
