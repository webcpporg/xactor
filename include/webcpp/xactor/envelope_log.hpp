// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The record of every delivery, in order: the evidence that two drivers, or
 two runs, delivered the same messages the same way
 (doc: #reference-xactor-envelope-log-hpp).

 Tip: it keeps which alternative of the message variant arrived, not the
 message, so an entry has a fixed width and two logs compare cheaply.
*/
#ifndef WEBCPP_XACTOR_ENVELOPE_LOG_HPP
#define WEBCPP_XACTOR_ENVELOPE_LOG_HPP

#include <webcpp/xactor/envelope.hpp>
#include <webcpp/xactor/ids.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace webcpp::xactor {

struct logged_envelope {
    sequence_number sequence{};
    actor_ref to{};
    actor_ref from{};
    correlation_id correlation{};
    // The execution's fuel when the message was delivered.
    std::uint32_t remaining_fuel = 0;
    std::size_t payload_index = 0;

    friend bool operator==(const logged_envelope&, const logged_envelope&) = default;
};

class envelope_log {
public:
    template <class Message>
    void record(const envelope<Message>& delivered, std::uint32_t remaining_fuel) {
        entries_.push_back(logged_envelope{
            .sequence = delivered.sequence,
            .to = delivered.to,
            .from = delivered.from,
            .correlation = delivered.correlation,
            .remaining_fuel = remaining_fuel,
            .payload_index = delivered.payload.index(),
        });
    }

    [[nodiscard]] const std::vector<logged_envelope>& entries() const noexcept { return entries_; }

    friend bool operator==(const envelope_log& left, const envelope_log& right) {
        return left.entries_ == right.entries_;
    }

private:
    std::vector<logged_envelope> entries_;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_ENVELOPE_LOG_HPP
