// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 What an actor does: one handle, called with one message at a time.

 @see "Writing an actor logic", in the guide.
*/
#ifndef WEBCPP_XACTOR_ACTOR_LOGIC_HPP
#define WEBCPP_XACTOR_ACTOR_LOGIC_HPP

#include <webcpp/xactor/envelope.hpp>
#include <webcpp/xactor/errors.hpp>

namespace webcpp::xactor {

template <class Message>
class turn;

/**
 The behaviour of an actor, the base of the class a program writes it in.

 A logic derives from it and overrides @ref handle. The logic is not the
 actor: @ref create_actor, @ref scheduler::spawn and
 @ref turn::spawn_child run a logic as an actor of a scheduler, and the
 running actor is the @ref actor_ref they return. What the logic is
 constructed with is the actor's input, and its members are the actor's
 state. It never sees the scheduler, its mailbox or the driver, only its
 turn.

 @tparam Message The type of every message of the system, the `Message`
 of the @ref scheduler that runs the logic.

 @see "The model", in the guide.
 @see "Writing an actor logic", in the guide.
*/
template <class Message>
class actor_logic {
public:
    /** Constructs the logic; a derived logic's constructor takes the actor's input. */
    actor_logic() = default;

    /** Destroys the logic; the scheduler that runs it does so as it is destroyed. */
    virtual ~actor_logic() = default;

    /**
     Not copyable: the scheduler owns a logic where it was constructed, and
     never moves it.
    */
    actor_logic(const actor_logic&) = delete;

    /**
     Not copy-assignable: the scheduler owns a logic where it was
     constructed, and never moves it.
    */
    actor_logic& operator=(const actor_logic&) = delete;

    /**
     Not movable: the scheduler owns a logic where it was constructed, and
     never moves it.
    */
    actor_logic(actor_logic&&) = delete;

    /**
     Not move-assignable: the scheduler owns a logic where it was
     constructed, and never moves it.
    */
    actor_logic& operator=(actor_logic&&) = delete;

    /**
     Handles one message, the payload of `cause`.

     The scheduler calls it once for each message delivered to the actor, and
     delivers the actor's next message only after it returns. Everything the
     actor does to the system goes through `turn`, which, like `cause`, lives
     only for the call.

     @param turn What the actor may do while it handles the message.
     @param cause The message, in the envelope that says who sent it and
     the execution it belongs to.
     @return Success, or an error of any category, which ends the actor with
     the status @ref status::error and is kept for @ref scheduler::error_of,
     unless the turn has already ended the actor, which then stays as it
     ended.
     @note An error ends this actor and only it: every other actor keeps its
     turns.
    */
    virtual result<void> handle(xactor::turn<Message>& turn, const envelope<Message>& cause) = 0;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_ACTOR_LOGIC_HPP
