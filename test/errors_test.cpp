// Copyright (c) 2026 WebCpp.org
//
// Distributed under the Boost Software License, Version 1.0. (See
// accompanying file LICENSE_1_0.txt or copy at
// https://www.boost.org/LICENSE_1_0.txt)

// Tests the category of xactor's errors: its name, the text of each errc, the form of message that
// writes into a caller's buffer and cuts the text to fit it, and the error_code an errc makes.

#include <webcpp/xactor/errors.hpp>

#include <boost/core/lightweight_test.hpp>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>

namespace xactor = webcpp::xactor;

namespace {

void the_category_is_named_webcpp_xactor() {
    BOOST_TEST_CSTR_EQ(xactor::error_category().name(), "webcpp.xactor");
}

void the_message_of_each_errc_is_its_enumerator() {
    const boost::system::error_category& category = xactor::error_category();
    BOOST_TEST_EQ(category.message(static_cast<int>(xactor::errc::invalid_argument)),
                  std::string("invalid_argument"));
    BOOST_TEST_EQ(category.message(static_cast<int>(xactor::errc::resource_limit)),
                  std::string("resource_limit"));
}

void the_message_of_a_value_errc_does_not_define_is_unknown() {
    const boost::system::error_category& category = xactor::error_category();
    BOOST_TEST_EQ(category.message(0), std::string("unknown"));
    BOOST_TEST_EQ(category.message(3), std::string("unknown"));
    BOOST_TEST_EQ(category.message(-1), std::string("unknown"));
}

void the_message_that_fills_a_buffer_copies_the_whole_name_when_it_fits() {
    const boost::system::error_category& category = xactor::error_category();
    std::array<char, 32> buffer{};
    const char* text = category.message(static_cast<int>(xactor::errc::resource_limit),
                                        buffer.data(), buffer.size());
    BOOST_TEST_CSTR_EQ(text, "resource_limit");
    BOOST_TEST_CSTR_EQ(buffer.data(), "resource_limit");

    // A buffer of exactly the name and its terminating zero is enough.
    std::array<char, sizeof("invalid_argument")> exact{};
    text = category.message(static_cast<int>(xactor::errc::invalid_argument), exact.data(),
                            exact.size());
    BOOST_TEST_CSTR_EQ(text, "invalid_argument");
    BOOST_TEST_CSTR_EQ(exact.data(), "invalid_argument");
}

void the_message_that_fills_a_buffer_cuts_the_name_to_fit_and_returns_it_whole() {
    const boost::system::error_category& category = xactor::error_category();
    std::array<char, 8> buffer{};
    const char* text = category.message(static_cast<int>(xactor::errc::resource_limit),
                                        buffer.data(), buffer.size());
    BOOST_TEST_CSTR_EQ(text, "resource_limit");
    BOOST_TEST_CSTR_EQ(buffer.data(), "resourc");
    BOOST_TEST_EQ(std::strlen(buffer.data()), buffer.size() - 1);

    // One byte holds the terminating zero alone.
    std::array<char, 1> one{{'x'}};
    text = category.message(static_cast<int>(xactor::errc::resource_limit), one.data(), one.size());
    BOOST_TEST_CSTR_EQ(text, "resource_limit");
    BOOST_TEST_EQ(one[0], '\0');
}

void the_message_that_fills_a_buffer_leaves_an_unusable_buffer_alone() {
    const boost::system::error_category& category = xactor::error_category();
    const int value = static_cast<int>(xactor::errc::invalid_argument);

    std::array<char, 4> untouched{{'x', 'y', 'z', 'w'}};
    const char* text = category.message(value, untouched.data(), 0);
    BOOST_TEST_CSTR_EQ(text, "invalid_argument");
    BOOST_TEST_EQ(untouched[0], 'x');
    BOOST_TEST_EQ(untouched[1], 'y');
    BOOST_TEST_EQ(untouched[2], 'z');
    BOOST_TEST_EQ(untouched[3], 'w');

    text = category.message(value, nullptr, 16);
    BOOST_TEST_CSTR_EQ(text, "invalid_argument");
}

void the_message_that_fills_a_buffer_spells_unknown_for_an_undefined_value() {
    const boost::system::error_category& category = xactor::error_category();
    std::array<char, 16> buffer{};
    const char* text = category.message(3, buffer.data(), buffer.size());
    BOOST_TEST_CSTR_EQ(text, "unknown");
    BOOST_TEST_CSTR_EQ(buffer.data(), "unknown");
}

void an_errc_makes_an_error_code_of_its_category() {
    const boost::system::error_code code = xactor::make_error_code(xactor::errc::resource_limit);
    BOOST_TEST_EQ(code.value(), 2);
    BOOST_TEST(code.category() == xactor::error_category());
    BOOST_TEST_CSTR_EQ(code.category().name(), "webcpp.xactor");
    BOOST_TEST_EQ(code.message(), std::string("resource_limit"));
    BOOST_TEST(code == xactor::errc::resource_limit);
    BOOST_TEST(code != xactor::errc::invalid_argument);

    const boost::system::error_code converted = xactor::errc::invalid_argument;
    BOOST_TEST_EQ(converted.value(), 1);
    BOOST_TEST_EQ(converted.message(), std::string("invalid_argument"));
}

void a_failed_result_holds_the_error_of_its_errc() {
    const xactor::result<int> failed = xactor::failure<int>(xactor::errc::invalid_argument);
    BOOST_TEST(!failed.has_value());
    BOOST_TEST(failed.error() == xactor::errc::invalid_argument);
    BOOST_TEST_EQ(failed.error().message(), std::string("invalid_argument"));
}

}  // namespace

int main() {
    the_category_is_named_webcpp_xactor();
    the_message_of_each_errc_is_its_enumerator();
    the_message_of_a_value_errc_does_not_define_is_unknown();
    the_message_that_fills_a_buffer_copies_the_whole_name_when_it_fits();
    the_message_that_fills_a_buffer_cuts_the_name_to_fit_and_returns_it_whole();
    the_message_that_fills_a_buffer_leaves_an_unusable_buffer_alone();
    the_message_that_fills_a_buffer_spells_unknown_for_an_undefined_value();
    an_errc_makes_an_error_code_of_its_category();
    a_failed_result_holds_the_error_of_its_errc();
    return boost::report_errors();
}
