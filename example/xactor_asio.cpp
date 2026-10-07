// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

#include <webcpp/xactor.hpp>

#include <cstddef>
#include <iostream>
#include <variant>

namespace xactor = webcpp::xactor;

namespace {

struct serve {
    xactor::actor_ref to{};
};

struct ball {
    int rally = 0;
};

using message = std::variant<serve, ball>;

// tag::player[]
/** Returns the ball until the rally reaches 4. */
class player final : public xactor::actor_logic<message> {
public:
    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (const auto* served = std::get_if<serve>(&cause.payload)) {
            return turn.send(served->to, ball{.rally = 1});
        }
        const int rally = std::get<ball>(cause.payload).rally;
        if (rally == 4) {
            return {};
        }
        return turn.reply(ball{.rally = rally + 1});
    }
};

// end::player[]

/** Two players and a serve from the host; returns the server, or 0 on failure. */
xactor::actor_ref set_up(xactor::scheduler<message>& system) {
    const xactor::result<xactor::actor_ref> left = xactor::create_actor<player>(system);
    const xactor::result<xactor::actor_ref> right = xactor::create_actor<player>(system);
    if (!left.has_value() || !right.has_value()) {
        return {};
    }
    if (!system.deliver(*left, serve{.to = *right}, xactor::correlation_id{.value = 1})
             .has_value()) {
        return {};
    }
    return *left;
}

}  // namespace

int main() {
    xactor::scheduler<message> plain(xactor::budgets{.fuel = 100});
    if (set_up(plain).value == 0) {
        return 1;
    }
    xactor::fifo_driver<message> fifo(plain);
    xactor::envelope_log fifo_log;
    const xactor::result<std::size_t> by_fifo = fifo.run_until_idle(&fifo_log);
    if (!by_fifo.has_value()) {
        return 1;
    }

    // tag::asio_driver[]
    xactor::scheduler<message> posted(xactor::budgets{.fuel = 100});
    if (set_up(posted).value == 0) {
        return 1;
    }
    xactor::asio_driver<message> asio(posted);
    xactor::envelope_log asio_log;
    const xactor::result<std::size_t> by_asio = asio.run_until_idle(&asio_log);
    if (!by_asio.has_value()) {
        return 1;
    }
    std::cout << "fifo_driver delivered " << *by_fifo << ", asio_driver delivered " << *by_asio
              << '\n';
    std::cout << "the two logs are equal: " << std::boolalpha << (fifo_log == asio_log) << '\n';
    // end::asio_driver[]
    return 0;
}
