// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The drivers: what decides when the scheduler's turns happen
 (doc: #reference-xactor-drivers-hpp).

 Tip: the scheduler decides what a turn is and a driver when it happens, so
 the same actors run under a plain FIFO loop, one step at a time in a test,
 or on an Asio io_context, and must produce one envelope log. The Asio
 driver is left out of a WASI build.
*/
#ifndef WEBCPP_XACTOR_DRIVERS_HPP
#define WEBCPP_XACTOR_DRIVERS_HPP

#include <webcpp/xactor/envelope_log.hpp>
#include <webcpp/xactor/errors.hpp>
#include <webcpp/xactor/scheduler.hpp>

#include <cstddef>

// Boost.Asio, for the Asio driver alone, which a WASI build leaves out. It
// posts handlers to an io_context and makes no I/O object, so it reaches no
// clock, file or socket; each line says so to the lint's rule "no clock,
// disk or network".
#ifndef __wasi__
#include <boost/asio/executor_work_guard.hpp>  // lint-world: posts handlers only
#include <boost/asio/io_context.hpp>           // lint-world: posts handlers only
#include <boost/asio/post.hpp>                 // lint-world: posts handlers only
#include <boost/asio/strand.hpp>               // lint-world: posts handlers only
#endif

namespace webcpp::xactor {

/**
 Delivers the queued messages in FIFO order, on the calling thread.
*/
template <class Message>
class fifo_driver {
public:
    explicit fifo_driver(scheduler<Message>& owner) noexcept : owner_(owner) {}

    /**
     Delivers until nothing is left and returns how many were delivered.
    */
    result<std::size_t> run_until_idle(envelope_log* log) {
        std::size_t delivered = 0;
        while (true) {
            const result<bool> ran = owner_.run_one(log);
            if (!ran.has_value()) {
                return {boost::system::in_place_error, ran.error()};
            }
            if (!*ran) {
                return delivered;
            }
            ++delivered;
        }
    }

    /**
     Delivers at most `budget` messages, for a host that has to take the
     thread back between turns.
    */
    result<std::size_t> run_turn(std::size_t budget, envelope_log* log) {
        std::size_t delivered = 0;
        while (delivered < budget) {
            const result<bool> ran = owner_.run_one(log);
            if (!ran.has_value()) {
                return {boost::system::in_place_error, ran.error()};
            }
            if (!*ran) {
                return delivered;
            }
            ++delivered;
        }
        return delivered;
    }

private:
    scheduler<Message>& owner_;
};

/**
 One message per step, so a test observes exactly what one delivery changed.
*/
template <class Message>
class test_driver {
public:
    explicit test_driver(scheduler<Message>& owner) noexcept : owner_(owner) {}

    result<bool> step() { return owner_.run_one(nullptr); }

    result<bool> step(envelope_log& log) { return owner_.run_one(&log); }

private:
    scheduler<Message>& owner_;
};

#ifndef __wasi__

/**
 The same turns, posted onto one strand of an io_context the driver owns, so
 threads and I/O objects can live around the scheduler without entering a
 turn.

 Tip: the context is drained with poll under a work guard; run, run_one and
 run_for would block in pause, which on WASI aborts.
*/
template <class Message>
class asio_driver {
public:
    explicit asio_driver(scheduler<Message>& owner)
        : owner_(owner),
          guard_(boost::asio::make_work_guard(io_)),
          strand_(boost::asio::make_strand(io_)) {}

    result<std::size_t> run_until_idle(envelope_log* log) {
        std::size_t delivered = 0;
        boost::system::error_code failed;
        while (!owner_.idle() && !failed) {
            boost::asio::post(strand_, [this, log, &delivered, &failed] {
                const result<bool> ran = owner_.run_one(log);
                if (!ran.has_value()) {
                    failed = ran.error();
                    return;
                }
                if (*ran) {
                    ++delivered;
                }
            });
            io_.poll();
        }
        if (failed) {
            return {boost::system::in_place_error, failed};
        }
        return delivered;
    }

private:
    scheduler<Message>& owner_;
    boost::asio::io_context io_{1};
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> guard_;
    boost::asio::strand<boost::asio::io_context::executor_type> strand_;
};

#endif  // __wasi__

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_DRIVERS_HPP
