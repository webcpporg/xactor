// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 One delivery: who it is for, who sent it, the execution it belongs to, its
 place in the delivery order, and the message
 (doc: #reference-xactor-envelope-hpp).
*/
#ifndef WEBCPP_XACTOR_ENVELOPE_HPP
#define WEBCPP_XACTOR_ENVELOPE_HPP

#include <webcpp/xactor/ids.hpp>

namespace webcpp::xactor {

template <class Message>
struct envelope {
    actor_ref to{};
    // Address zero when the host sent it.
    actor_ref from{};
    correlation_id correlation{};
    sequence_number sequence{};
    Message payload{};
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_ENVELOPE_HPP
