// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// tag::includes[]
#include <webcpp/xactor.hpp>

#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <variant>
#include <vector>

namespace xactor = webcpp::xactor;
// end::includes[]

namespace {

struct start {
    int value = 0;
};

struct ask {
    int value = 0;
};

struct answer {
    int value = 0;
};

using message = std::variant<start, ask, answer>;

// tag::logics[]
/** Answers every ask with the same number. */
class echo final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (const auto* asked = std::get_if<ask>(&cause.payload)) {
            return turn.reply(answer{.value = asked->value});
        }
        return {};
    }
};

/** Asks the echo for the number a start names, and keeps each answer where the test reads it. */
class client final : public xactor::actor_logic<message> {
public:
    client(xactor::actor_ref echo, std::vector<int>& answers) : echo_(echo), answers_(answers) {}

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (const auto* started = std::get_if<start>(&cause.payload)) {
            return turn.send(echo_, ask{.value = started->value});
        }
        if (const auto* answered = std::get_if<answer>(&cause.payload)) {
            answers_.push_back(answered->value);
        }
        return {};
    }

private:
    xactor::actor_ref echo_;
    std::vector<int>& answers_;
};

// end::logics[]

// tag::steps[]
/** Whether one step of the driver delivered a message. */
bool stepped(xactor::test_driver<message>& driver) {
    const xactor::result<bool> step = driver.step();
    return step.has_value() && *step;
}

/** One message a step: after each, the test checks what that message changed. */
void the_client_keeps_what_the_echo_answers() {
    std::vector<int> answers;
    xactor::scheduler<message> system(xactor::budgets{.fuel = 10});
    const xactor::result<xactor::actor_ref> echoing = xactor::create_actor<echo>(system);
    if (!BOOST_TEST(echoing.has_value())) {
        return;
    }
    const xactor::result<xactor::actor_ref> asking =
        xactor::create_actor<client>(system, *echoing, answers);
    if (!BOOST_TEST(asking.has_value())) {
        return;
    }
    const xactor::correlation_id execution{.value = 1};
    if (!BOOST_TEST(system.deliver(*asking, start{.value = 7}, execution).has_value())) {
        return;
    }

    xactor::test_driver<message> driver(system);
    BOOST_TEST(stepped(driver));  // The client handles start, and asks.
    BOOST_TEST_EQ(system.fuel_of(execution), 9U);
    BOOST_TEST(stepped(driver));  // The echo handles the ask, and answers.
    BOOST_TEST_EQ(system.fuel_of(execution), 8U);
    BOOST_TEST(answers.empty());
    BOOST_TEST(stepped(driver));  // The client handles the answer, and keeps it.
    BOOST_TEST(answers == std::vector<int>{7});
    BOOST_TEST(!stepped(driver));
    BOOST_TEST(system.idle());
}

// end::steps[]

// tag::logs[]
/** Runs the client and the echo on one start for each of numbers, and returns their log. */
xactor::envelope_log run(const std::vector<int>& numbers) {
    std::vector<int> answers;
    xactor::envelope_log log;
    xactor::scheduler<message> system(xactor::budgets{.fuel = 10});
    const xactor::result<xactor::actor_ref> echoing = xactor::create_actor<echo>(system);
    if (!BOOST_TEST(echoing.has_value())) {
        return log;
    }
    const xactor::result<xactor::actor_ref> asking =
        xactor::create_actor<client>(system, *echoing, answers);
    if (!BOOST_TEST(asking.has_value())) {
        return log;
    }
    std::uint64_t correlation = 0;
    for (const int number : numbers) {
        ++correlation;
        const xactor::correlation_id execution{.value = correlation};
        BOOST_TEST(system.deliver(*asking, start{.value = number}, execution).has_value());
    }
    xactor::fifo_driver<message> driver(system);
    BOOST_TEST(driver.run_until_idle(&log).has_value());
    return log;
}

/** Two runs of the same deliveries record equal envelope logs. */
void two_runs_record_the_same_log() {
    const xactor::envelope_log first = run({1, 2, 3});
    const xactor::envelope_log second = run({1, 2, 3});
    BOOST_TEST_EQ(first.entries().size(), 9U);
    BOOST_TEST(first == second);
}

// end::logs[]

}  // namespace

int main() {
    // tag::main[]
    the_client_keeps_what_the_echo_answers();
    two_runs_record_the_same_log();
    return boost::report_errors();
    // end::main[]
}
