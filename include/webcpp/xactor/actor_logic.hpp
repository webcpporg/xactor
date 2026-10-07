// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 What an actor does: one handle, called with one message at a time
 (doc: #reference-xactor-actor-logic-hpp).

 Tip: the logic is not the actor. create_actor or turn::spawn_child runs a
 logic in a scheduler, and the running actor is the actor_ref they return;
 the logic never sees the scheduler, its mailbox or the driver, only its
 turn. What it is constructed with is its input. A handle
 that returns an error ends its actor with the status error, and only it.
*/
#ifndef WEBCPP_XACTOR_ACTOR_LOGIC_HPP
#define WEBCPP_XACTOR_ACTOR_LOGIC_HPP

#include <webcpp/xactor/envelope.hpp>
#include <webcpp/xactor/errors.hpp>

namespace webcpp::xactor {

template <class Message>
class turn;

template <class Message>
class actor_logic {
public:
    actor_logic() = default;
    virtual ~actor_logic() = default;
    actor_logic(const actor_logic&) = delete;
    actor_logic& operator=(const actor_logic&) = delete;
    actor_logic(actor_logic&&) = delete;
    actor_logic& operator=(actor_logic&&) = delete;

    virtual result<void> handle(xactor::turn<Message>& turn, const envelope<Message>& cause) = 0;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_ACTOR_LOGIC_HPP
