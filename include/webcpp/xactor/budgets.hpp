// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The limits an actor system enforces.

 @see "Fuel", in the guide.
*/
#ifndef WEBCPP_XACTOR_BUDGETS_HPP
#define WEBCPP_XACTOR_BUDGETS_HPP

#include <webcpp/xactor/config.hpp>

#include <cstdint>

namespace webcpp::xactor {

/**
 The limits a scheduler enforces, which it is constructed with.

 @see "Fuel", in the guide.
*/
struct budgets {
    /**
     The fuel of one execution: every message its actors send, every timer
     they arm and every unit they spend is paid from it.

     An execution starts with the whole of it and is never refilled, so the
     work of one execution is bounded, whatever its actors send.
    */
    std::uint32_t fuel = 1'000'000;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_BUDGETS_HPP
