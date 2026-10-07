// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The identities of an actor system: an actor's address, the execution a
 message belongs to, and the order of deliveries
 (doc: #reference-xactor-ids-hpp).

 Tip: distinct types, so an address is never passed where an execution is
 expected; address zero names no actor.
*/
#ifndef WEBCPP_XACTOR_IDS_HPP
#define WEBCPP_XACTOR_IDS_HPP

#include <compare>
#include <cstdint>

namespace webcpp::xactor {

struct actor_ref {
    std::uint32_t value = 0;
    friend bool operator==(actor_ref, actor_ref) = default;
};

/**
 The execution every message caused by one host delivery belongs to; the
 execution's fuel is kept under it.
*/
struct correlation_id {
    std::uint64_t value = 0;
    friend bool operator==(correlation_id, correlation_id) = default;
    friend auto operator<=>(correlation_id, correlation_id) = default;
};

struct sequence_number {
    std::uint64_t value = 0;

    [[nodiscard]] sequence_number next() const noexcept { return sequence_number{value + 1}; }

    friend bool operator==(sequence_number, sequence_number) = default;
    friend auto operator<=>(sequence_number, sequence_number) = default;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_IDS_HPP
