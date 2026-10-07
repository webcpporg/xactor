// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The limits an actor system enforces (doc: #reference-xactor-budgets-hpp).
*/
#ifndef WEBCPP_XACTOR_BUDGETS_HPP
#define WEBCPP_XACTOR_BUDGETS_HPP

#include <cstdint>

namespace webcpp::xactor {

struct budgets {
    /**
     The fuel of one execution: every message its actors send, every timer
     they arm and every unit they spend is paid from it.
    */
    std::uint32_t fuel = 1'000'000;
};

}  // namespace webcpp::xactor

#endif  // WEBCPP_XACTOR_BUDGETS_HPP
