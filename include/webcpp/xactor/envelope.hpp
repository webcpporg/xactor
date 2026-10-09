// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 One delivery: who it is for, who sent it, the execution it belongs to, its
 place in the delivery order, and the message.

 @see "The model", in the guide.
*/
#ifndef WEBCPP_XACTOR_ENVELOPE_HPP
#define WEBCPP_XACTOR_ENVELOPE_HPP

#include <webcpp/xactor/config.hpp>

#include <webcpp/xactor/ids.hpp>

namespace webcpp::xactor {

/**
 A message as the scheduler delivers it, and as @ref actor_logic::handle
 receives it.

 The scheduler makes every envelope; a program reads them.

 @tparam Message The type of every message of the system.
*/
template <class Message>
struct envelope {
    /** The actor it is for. */
    actor_ref to{};

    /**
     The actor that sent it: address zero when the host sent it, and the
     actor itself for the message of one of its timers.
    */
    actor_ref from{};

    /** The execution it belongs to, which pays for what its handling sends. */
    correlation_id correlation{};

    /**
     Its place in the order messages entered a mailbox, across every actor of
     the scheduler: 1 for the first.
    */
    sequence_number sequence{};

    /** The message. */
    Message payload{};
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_ENVELOPE_HPP
