// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// tag::includes[]
#include <webcpp/xactor.hpp>

#include <cstddef>
#include <iostream>
#include <variant>

namespace xactor = webcpp::xactor;
// end::includes[]

namespace {

// tag::messages[]
struct start {};

struct ask {
    int value = 0;
};

struct answer {
    int value = 0;
};

struct total {
    int value = 0;
};

using message = std::variant<start, ask, answer, total>;
// end::messages[]

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

/** Asks the echo for 1, 2 and 3, and finishes with the sum of the answers. */
class counter final : public xactor::actor_logic<message> {
public:
    explicit counter(xactor::actor_ref echo) : echo_(echo) {}

    xactor::result<void> handle(xactor::turn<message>& turn,
                                const xactor::envelope<message>& cause) override {
        if (std::holds_alternative<start>(cause.payload)) {
            for (int value = 1; value <= 3; ++value) {
                const xactor::result<void> sent = turn.send(echo_, ask{.value = value});
                if (!sent.has_value()) {
                    return sent;
                }
            }
            return {};
        }
        if (const auto* answered = std::get_if<answer>(&cause.payload)) {
            sum_ += answered->value;
            ++answers_;
            if (answers_ == 3) {
                return turn.finish(total{.value = sum_});
            }
        }
        return {};
    }

private:
    xactor::actor_ref echo_;
    int sum_ = 0;
    int answers_ = 0;
};

// end::logics[]

}  // namespace

int main() {
    // tag::host[]
    xactor::scheduler<message> system(xactor::budgets{.fuel = 100});
    const xactor::result<xactor::actor_ref> echoing = xactor::create_actor<echo>(system);
    if (!echoing.has_value()) {
        return 1;
    }
    const xactor::result<xactor::actor_ref> counting =
        xactor::create_actor<counter>(system, *echoing);
    if (!counting.has_value()) {
        return 1;
    }

    const xactor::correlation_id execution{.value = 1};
    if (!system.deliver(*counting, start{}, execution).has_value()) {
        return 1;
    }
    xactor::fifo_driver<message> driver(system);
    const xactor::result<std::size_t> delivered = driver.run_until_idle(nullptr);
    if (!delivered.has_value()) {
        return 1;
    }

    const message* output = system.output_of(*counting);
    if (output == nullptr) {
        return 1;
    }
    std::cout << "delivered " << *delivered << " messages\n";
    std::cout << "total " << std::get<total>(*output).value << ", fuel left "
              << system.fuel_of(execution) << '\n';
    // end::host[]
    return 0;
}
