// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The configuration of xactor, which each of its headers includes first: whether
 xactor is built without exceptions.

 @see "Targets", in the guide.
*/
#ifndef WEBCPP_XACTOR_CONFIG_HPP
#define WEBCPP_XACTOR_CONFIG_HPP

#include <boost/config.hpp>

// MrDocs parses xactor as a build with exceptions does, where the macro is not
// defined: this definition, undone at once, is the one the reference lists, and
// the parse goes on with every API that throws in it.
#ifdef __MRDOCS__
/**
 Defined when xactor is built without exceptions: automatically when the
 compiler has none (`BOOST_NO_EXCEPTIONS`), or by the developer to disable them
 in a build that has them.

 xactor raises no exception of its own: every operation that can fail returns a
 `result`, so the macro changes nothing in it. It is provided so that a program
 can set it for every webcpp library alike. An exception xactor raised would go
 through `boost::throw_exception`, and an API that throws would be absent while
 this is defined.

 @see "Targets", in the guide.
*/
#define WEBCPP_XACTOR_NO_EXCEPTIONS
#undef WEBCPP_XACTOR_NO_EXCEPTIONS
#endif

#if defined(BOOST_NO_EXCEPTIONS) && !defined(WEBCPP_XACTOR_NO_EXCEPTIONS)
#define WEBCPP_XACTOR_NO_EXCEPTIONS
#endif

#endif  // WEBCPP_XACTOR_CONFIG_HPP
