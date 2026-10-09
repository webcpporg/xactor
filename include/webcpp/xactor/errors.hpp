// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

/**
 The errors an actor system reports, as `boost::system::error_code` values of
 a category of its own, and the result type of every operation that can fail.

 @see "Writing an actor logic", in the guide.
*/
#ifndef WEBCPP_XACTOR_ERRORS_HPP
#define WEBCPP_XACTOR_ERRORS_HPP

#include <webcpp/xactor/config.hpp>

#include <boost/system/error_category.hpp>
#include <boost/system/error_code.hpp>
#include <boost/system/result.hpp>

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>

namespace webcpp::xactor {

/**
 The errors xactor reports, as `boost::system::error_code` values of the
 category @ref error_category.

 The values start at 1, so an `error_code` of value 0, which
 @ref scheduler::error_of returns for an actor that has not failed, is no
 error. An `errc` converts to an `error_code` and compares with one:
 `sent.error() == xactor::errc::resource_limit`.

 @note A handler may return any `error_code`, of this category or of its
 own; these two are the system's own.
*/
enum class errc : int {
    /**
     A call that names what it cannot act on, which changes nothing.

     A null logic given to @ref scheduler::spawn; an address that names no
     actor, given to @ref scheduler::deliver, @ref scheduler::stop,
     @ref scheduler::status_of or @ref turn::send; a @ref turn::reply to a
     message from the host, which has no sender; a @ref turn::finish or a
     @ref turn::stop once the actor has ended; a @ref turn::stop_child of an
     actor that is not a child; or a timer or a child asked by an actor that
     has ended.
    */
    invalid_argument = 1,

    /**
     The execution has no fuel left for what was asked, which changes
     nothing: nothing is enqueued, armed or spent.
    */
    resource_limit = 2,
};

namespace detail {

/** The name of an @ref errc value, or "unknown" for a value it does not define. */
inline const char* spelling_of(int value) noexcept {
    switch (static_cast<errc>(value)) {
        case errc::invalid_argument: return "invalid_argument";
        case errc::resource_limit: return "resource_limit";
    }
    return "unknown";
}

/**
 A constant category with a fixed identity, so it holds no mutable state.

 @note The identity is the ASCII of "xactor" followed by two zero bytes.
*/
class category final : public boost::system::error_category {
public:
    /** Constructs the category with its fixed identity. */
    constexpr category() noexcept : boost::system::error_category(0x786163746f720000ULL) {}

    /** The category's name, "webcpp.xactor". */
    const char* name() const noexcept override { return "webcpp.xactor"; }

    /** The name of the error `value`, as @ref spelling_of spells it. */
    std::string message(int value) const override { return spelling_of(value); }

    /**
     Copies the name of the error `value` into `buffer`, cut to fit `size`
     bytes with its terminating zero, and returns the whole name.
    */
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

/** The one category of xactor's errors, which @ref error_category returns. */
inline constexpr category the_category{};

}  // namespace detail

/**
 The category of every @ref errc.

 Its `name()` is "webcpp.xactor", and its `message(value)` the name of the
 enumerator, "resource_limit" for @ref errc::resource_limit, or "unknown"
 for a value @ref errc does not define.

 @return The category, a constant with a fixed identity.
*/
inline const boost::system::error_category& error_category() noexcept {
    return detail::the_category;
}

/**
 Makes the `error_code` of an @ref errc, as its implicit conversion does.

 @param code The error.
 @return `code` as an `error_code` of the category @ref error_category.
*/
inline boost::system::error_code make_error_code(errc code) noexcept {
    return {static_cast<int>(code), error_category()};
}

/**
 What an operation that can fail returns: a `T`, or the `error_code` of the
 failure.

 It is Boost.System's `result`. xactor throws nothing of its own: every
 operation of it that can fail returns one. An operation of a @ref scheduler
 or of a @ref turn changes nothing when it fails, while a driver returns the
 failure that stopped it after the deliveries it has already made. Test it
 with `has_value()` before reading the value: `*` of a failed result is
 undefined, and its `value()` reports the failure through
 `boost::throw_exception`.

 @tparam T The type of the value; `void` for an operation that returns none.
*/
template <class T>
using result = boost::system::result<T, boost::system::error_code>;

/**
 Builds a failed result of the type the call would have returned.

 @tparam T The type of the value the result would hold.
 @param code The error.
 @return A @ref result that holds `make_error_code(code)`.
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
