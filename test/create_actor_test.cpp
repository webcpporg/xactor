// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// create_actor runs a logic as a new, active actor of a scheduler, and a
// message delivered to it reaches the logic.

#include <webcpp/xactor.hpp>

#include <boost/core/lightweight_test.hpp>

#include <memory>
#include <variant>

namespace xactor = webcpp::xactor;

namespace {

struct start {};

using message = std::variant<start>;

class counter final : public xactor::actor_logic<message> {
public:
    explicit counter(int& seen) : seen_(seen) {}

    xactor::result<void> handle(xactor::turn<message>& /*turn*/,
                                const xactor::envelope<message>& /*cause*/) override {
        ++seen_;
        return {};
    }

private:
    int& seen_;
};

void create_actor_spawns_an_active_actor_that_receives_messages() {
    xactor::scheduler<message> system(xactor::budgets{.fuel = 10});
    int seen = 0;
    const xactor::result<xactor::actor_ref> actor = xactor::create_actor<counter>(system, seen);
    if (!BOOST_TEST(actor.has_value())) {
        return;
    }
    BOOST_TEST(system.status_of(*actor).value() == xactor::status::active);
    if (!BOOST_TEST(
            system.deliver(*actor, start{}, xactor::correlation_id{.value = 1}).has_value())) {
        return;
    }
    xactor::fifo_driver driver(system);
    if (!BOOST_TEST(driver.run_until_idle(nullptr).has_value())) {
        return;
    }
    BOOST_TEST_EQ(seen, 1);
}

}  // namespace

int main() {
    create_actor_spawns_an_active_actor_that_receives_messages();
    return boost::report_errors();
}
