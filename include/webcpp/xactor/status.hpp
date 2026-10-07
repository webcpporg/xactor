// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 Where an actor is in its life (doc: #reference-xactor-status-hpp).

 Tip: the values are fixed and never reused, because a system that persists
 its actors rebuilds them from these numbers.
*/
#ifndef WEBCPP_XACTOR_STATUS_HPP
#define WEBCPP_XACTOR_STATUS_HPP

#include <cstdint>

namespace webcpp::xactor {

enum class status : std::uint8_t {
    // Receives messages. Every actor starts here.
    active = 1,
    // Finished with an output, through turn::finish.
    done = 2,
    // Its handler returned an error.
    error = 3,
    // Stopped from outside, by scheduler::stop or a parent's
    // turn::stop_child, alone or with an ancestor.
    stopped = 4,
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_STATUS_HPP
