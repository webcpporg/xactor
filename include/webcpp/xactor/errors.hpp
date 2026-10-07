// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The errors an actor system reports, as boost::system::error_code values of a
 category of its own, and the result type every operation returns
 (doc: #reference-xactor-errors-hpp).

 Tip: a handler returns any error_code; these two are the system's own.
*/
#ifndef WEBCPP_XACTOR_ERRORS_HPP
#define WEBCPP_XACTOR_ERRORS_HPP

#include <boost/system/error_category.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace webcpp::xactor {

enum class errc : int {
    // A null logic given to spawn, an address that names no actor, a reply
    // to a message from the host, a finish or a stop once the actor has
    // ended, a stop_child of an actor that is not a child, or a timer or a
    // child asked by an actor that has ended (doc: #reference-xactor-errc).
    invalid_argument = 1,
    // The execution has no fuel left for what was asked.
    resource_limit = 2,
};

namespace detail {

inline const char* spelling_of(int value) noexcept {
    switch (static_cast<errc>(value)) {
        case errc::invalid_argument: return "invalid_argument";
        case errc::resource_limit: return "resource_limit";
    }
    return "unknown";
}

/**
 A constant category with a fixed identity, so it holds no mutable state.

 Tip: the identity is the ASCII of "xactor" followed by two zero bytes.
*/
class category final : public boost::system::error_category {
public:
    constexpr category() noexcept : boost::system::error_category(0x786163746f720000ULL) {}

    const char* name() const noexcept override { return "webcpp.xactor"; }

    std::string message(int value) const override { return spelling_of(value); }

    const char* message(int value, char* buffer, std::size_t size) const noexcept override {
        const char* text = spelling_of(value);
        if (buffer != nullptr && size != 0) {
            const std::string_view spelling(text);
            const std::size_t limit = size - 1;
            const std::size_t count = spelling.size() < limit ? spelling.size() : limit;
            std::memcpy(buffer, spelling.data(), count);
            buffer[count] = '\0';
        }
        return text;
    }
};

inline constexpr category the_category{};

}  // namespace detail

inline const boost::system::error_category& error_category() noexcept {
    return detail::the_category;
}

inline boost::system::error_code make_error_code(errc code) noexcept {
    return {static_cast<int>(code), error_category()};
}

template <class T>
using result = boost::system::result<T, boost::system::error_code>;

/**
 Builds a failed result of the type the call would have returned.
*/
template <class T>
result<T> failure(errc code) noexcept {
    return result<T>(boost::system::in_place_error, make_error_code(code));
}

}  // namespace webcpp::xactor

namespace boost::system {
template <>
struct is_error_code_enum<webcpp::xactor::errc> : std::true_type {};
}  // namespace boost::system

#endif  // WEBCPP_XACTOR_ERRORS_HPP
