// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests the values the scheduler numbers messages with, and the comparison of two envelope logs:
// sequence_number::next(), and envelope_log's operator== on logs that are equal and on logs that
// differ in each way an entry can.

#include <webcpp/xactor/envelope.hpp>
#include <webcpp/xactor/envelope_log.hpp>
#include <webcpp/xactor/ids.hpp>

#include <boost/core/lightweight_test.hpp>

#include <cstdint>
#include <variant>

namespace xactor = webcpp::xactor;

namespace {

struct ping {};

struct pong {};

using message = std::variant<ping, pong>;

xactor::envelope<message> make_envelope(std::uint64_t sequence, message payload = ping{}) {
    return xactor::envelope<message>{
        .to = xactor::actor_ref{2},
        .from = xactor::actor_ref{1},
        .correlation = xactor::correlation_id{7},
        .sequence = xactor::sequence_number{sequence},
        .payload = payload,
    };
}

void next_is_the_sequence_number_one_after() {
    const xactor::sequence_number first{1};
    const xactor::sequence_number second = first.next();
    BOOST_TEST_EQ(second.value, 2u);
    BOOST_TEST(second == xactor::sequence_number{2});
    BOOST_TEST(first < second);
    BOOST_TEST_EQ(second.next().value, 3u);

    // The number it is called on does not change, and a default one is 0, so the first is 1.
    BOOST_TEST_EQ(first.value, 1u);
    BOOST_TEST_EQ(xactor::sequence_number{}.next().value, 1u);
}

void next_counts_on_past_the_width_of_a_narrow_integer() {
    const xactor::sequence_number past_32_bits{std::uint64_t{0xffffffffu}};
    BOOST_TEST_EQ(past_32_bits.next().value, std::uint64_t{0x100000000u});
}

void two_empty_logs_are_equal() {
    const xactor::envelope_log left;
    const xactor::envelope_log right;
    BOOST_TEST(left == right);
}

void two_logs_of_the_same_entries_are_equal() {
    xactor::envelope_log left;
    xactor::envelope_log right;
    left.record(make_envelope(1), 10);
    left.record(make_envelope(2, pong{}), 9);
    right.record(make_envelope(1), 10);
    right.record(make_envelope(2, pong{}), 9);
    BOOST_TEST(left == right);
    BOOST_TEST_EQ(left.entries().size(), 2u);
}

void logs_that_differ_are_not_equal() {
    xactor::envelope_log base;
    base.record(make_envelope(1), 10);

    // A log with one entry more, and an empty one.
    xactor::envelope_log longer;
    longer.record(make_envelope(1), 10);
    longer.record(make_envelope(2), 9);
    BOOST_TEST(!(base == longer));
    BOOST_TEST(!(longer == base));
    BOOST_TEST(!(base == xactor::envelope_log{}));

    // An entry that differs in its sequence.
    xactor::envelope_log other_sequence;
    other_sequence.record(make_envelope(5), 10);
    BOOST_TEST(!(base == other_sequence));

    // In the fuel it was delivered with.
    xactor::envelope_log other_fuel;
    other_fuel.record(make_envelope(1), 9);
    BOOST_TEST(!(base == other_fuel));

    // In the alternative of the message variant.
    xactor::envelope_log other_payload;
    other_payload.record(make_envelope(1, pong{}), 10);
    BOOST_TEST(!(base == other_payload));

    // In its receiver, its sender and its correlation.
    xactor::envelope<message> other_to = make_envelope(1);
    other_to.to = xactor::actor_ref{3};
    xactor::envelope_log to_log;
    to_log.record(other_to, 10);
    BOOST_TEST(!(base == to_log));

    xactor::envelope<message> other_from = make_envelope(1);
    other_from.from = xactor::actor_ref{};
    xactor::envelope_log from_log;
    from_log.record(other_from, 10);
    BOOST_TEST(!(base == from_log));

    xactor::envelope<message> other_correlation = make_envelope(1);
    other_correlation.correlation = xactor::correlation_id{8};
    xactor::envelope_log correlation_log;
    correlation_log.record(other_correlation, 10);
    BOOST_TEST(!(base == correlation_log));
}

void logs_of_the_same_entries_in_another_order_are_not_equal() {
    xactor::envelope_log left;
    xactor::envelope_log right;
    left.record(make_envelope(1), 10);
    left.record(make_envelope(2), 9);
    right.record(make_envelope(2), 9);
    right.record(make_envelope(1), 10);
    BOOST_TEST(!(left == right));
}

}  // namespace

int main() {
    next_is_the_sequence_number_one_after();
    next_counts_on_past_the_width_of_a_narrow_integer();
    two_empty_logs_are_equal();
    two_logs_of_the_same_entries_are_equal();
    logs_that_differ_are_not_equal();
    logs_of_the_same_entries_in_another_order_are_not_equal();
    return boost::report_errors();
}
