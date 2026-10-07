// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 Where an actor is in its life.

 @see "Lifecycle", in the guide.
*/
#ifndef WEBCPP_XACTOR_STATUS_HPP
#define WEBCPP_XACTOR_STATUS_HPP

#include <cstdint>

namespace webcpp::xactor {

/**
 Where an actor is in its life, which @ref scheduler::status_of returns.

 An actor starts @ref active, the one status in which it receives
 messages, and ends in one of the three others, each final: its pending
 messages and its timers are dropped, and every later message to it is
 dropped too.

 @note The values are fixed and never reused, because a system that
 persists its actors rebuilds them from these numbers.

 @see "Lifecycle", in the guide.
*/
enum class status : std::uint8_t {
    /** Receives messages; every actor starts here. */
    active = 1,

    /**
     Finished with an output, through @ref turn::finish, which
     @ref scheduler::output_of returns.
    */
    done = 2,

    /** Its handler returned an error, which @ref scheduler::error_of returns. */
    error = 3,

    /**
     Stopped by @ref scheduler::stop or its parent's @ref turn::stop_child,
     alone or with an ancestor, or by its own @ref turn::stop.
    */
    stopped = 4,
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_STATUS_HPP
