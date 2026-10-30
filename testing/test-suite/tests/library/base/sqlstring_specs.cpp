/*
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2026 dev4fun. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2.0,
 * as published by the Free Software Foundation.
 *
 * This program is designed to work with certain software (including
 * but not limited to OpenSSL) that is licensed under separate terms, as
 * designated in a particular file or component or in included license
 * documentation.  The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful,  but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "gtest/gtest.h"

#include "base/sqlstring.h"

namespace {

#define MAX_SIZE_RANDOM_VALUES_ARRAY 25
#define MAX_SIZE_SQLSTRING_ARRAY 10000

  struct rdm_vals {
    int int_value;
    std::string string_value;
    double double_value;
    base::sqlstring sqlstring_value;
  } random_values[MAX_SIZE_RANDOM_VALUES_ARRAY];

  // sqlstring test class
  class SqlStringTest : public ::testing::Test {
  protected:
    std::string placeholders;
    std::string long_random_string; // Content doesn't matter. There must be no crash using it.

    void SetUp() override {
      int i;
      double tmp1, tmp2;

      srand((unsigned int)time(NULL));
      for (i = 0; i < 1000; i++) {
        long_random_string += ' ' + rand() * 64 / 32768;
        if (rand() > 16767)
          long_random_string += "\xE3\x8A\xA8"; // Valid Unicode character.
        if (i == 500)
          long_random_string += "\xE3\x8A\xA8"; // Ensure it is there at least once.
      }

      placeholders.clear();

      for (i = 0; i < MAX_SIZE_RANDOM_VALUES_ARRAY; i++) {
        placeholders += "? ! ? ! ";

        random_values[i].int_value = rand();
        tmp1 = rand() % 100;     // An int value between 0 and 99
        tmp2 = rand() % 100 + 1; // An int value between 1 and 100
        random_values[i].double_value = tmp1 / tmp2;

        random_values[i].sqlstring_value =
          base::sqlstring(std::to_string(rand()).c_str(), base::sqlstring::sqlstringformat(0));
        random_values[i].string_value = std::to_string(rand());
      }
    }
  };

  TEST_F(SqlStringTest, OverriddenOperatorForNumericValues) {
    std::int64_t int64_t_value1 = 1999;
    std::int64_t int64_t_value2 = -1999;
    int int_value1 = -2001;
    int int_value2 = 2001;
    float float_value1 = 3.141592f;
    float float_value2 = -3.141592f;
    double double_value1 = 2.718281;
    double double_value2 = -2.718281;

    // test cases for simple use of operator <<
    EXPECT_EQ(base::sqlstring("first_test ?", 0) << int64_t_value1, "first_test 1999");
    EXPECT_EQ(base::sqlstring("second_test ?", 0) << int_value1, "second_test -2001");
    EXPECT_EQ(base::sqlstring("third_test ?", 0) << float_value1, "third_test 3.141592");
    EXPECT_EQ(base::sqlstring("fourth_test ?", 0) << double_value1, "fourth_test 2.718281");

    // test cases for composed use of operator <<
    EXPECT_EQ(base::sqlstring("? fifth_test ?", 0) << int64_t_value1 << int64_t_value2, "1999 fifth_test -1999");
    EXPECT_EQ(base::sqlstring("? sixth_test ?", 0) << int_value1 << int_value2, "-2001 sixth_test 2001");
    EXPECT_EQ(base::sqlstring("? seventh_test ?", 0) << float_value1 << float_value2,
              "3.141592 seventh_test -3.141592");
    EXPECT_EQ(base::sqlstring("? eighth_test ?", 0) << double_value1 << double_value2,
              "2.718281 eighth_test -2.718281");
  }

  TEST_F(SqlStringTest, OverriddenOperatorForNumericValuesCornerValues) {
    double NaN = 2.51;

    // test cases for simple use of operator <<
    EXPECT_EQ(base::sqlstring("?", 0) << INT_MIN, std::to_string(INT_MIN));
    EXPECT_EQ(base::sqlstring("?", 0) << INT_MAX, std::to_string(INT_MAX));
    EXPECT_EQ(base::sqlstring("?", 0) << LONG_MIN, std::to_string(LONG_MIN));
    EXPECT_EQ(base::sqlstring("?", 0) << LONG_MAX, std::to_string(LONG_MAX));
    EXPECT_NE(base::sqlstring("?", 0) << NaN, std::to_string(0.0));
    EXPECT_EQ(base::sqlstring("?", 0) << SHRT_MIN, std::to_string(SHRT_MIN));
    EXPECT_EQ(base::sqlstring("?", 0) << SHRT_MAX, std::to_string(SHRT_MAX));
    EXPECT_EQ(base::sqlstring("?", 0) << LLONG_MIN, std::to_string(LLONG_MIN));
    EXPECT_EQ(base::sqlstring("?", 0) << LLONG_MAX, std::to_string(LLONG_MAX));
    EXPECT_EQ(base::sqlstring("?", 0) << CHAR_MIN, std::to_string(CHAR_MIN));
    EXPECT_EQ(base::sqlstring("?", 0) << CHAR_MAX, std::to_string(CHAR_MAX));
  }

  TEST_F(SqlStringTest, OverriddenOperatorForStdStringValues) {
    std::string tail = "TAIL";
    std::string head = "HEAD";

    // test cases for simple use of operator <<
    EXPECT_EQ(base::sqlstring("first_test ?", base::QuoteOnlyIfNeeded) << tail, "first_test 'TAIL'");
    EXPECT_EQ(base::sqlstring("second_test ?", base::UseAnsiQuotes) << tail, "second_test \"TAIL\"");
    EXPECT_EQ(base::sqlstring("third_test !", base::QuoteOnlyIfNeeded) << tail, "third_test TAIL");
    EXPECT_EQ(base::sqlstring("fourth_test !", base::UseAnsiQuotes) << tail, "fourth_test `TAIL`");

    // test cases for composed use of operator <<
    EXPECT_EQ(base::sqlstring("? fifth_test ?", base::QuoteOnlyIfNeeded) << head << tail, "'HEAD' fifth_test 'TAIL'");
    EXPECT_EQ(base::sqlstring("? sixth_test ?", base::UseAnsiQuotes) << head << tail, "\"HEAD\" sixth_test \"TAIL\"");
    EXPECT_EQ(base::sqlstring("! seventh_test !", base::QuoteOnlyIfNeeded) << head << tail, "HEAD seventh_test TAIL");
    EXPECT_EQ(base::sqlstring("! eighth_test !", base::UseAnsiQuotes) << head << tail, "`HEAD` eighth_test `TAIL`");
  }

  TEST_F(SqlStringTest, OverriddenOperatorForStdStringValuesCornerValues) {
    // test cases for simple use of operator <<
    std::string test_result = "";
    std::string temp_long_random_string = "";
    std::string escaped = "";
    test_result.append(base::sqlstring("?", base::QuoteOnlyIfNeeded) << long_random_string);

    temp_long_random_string.append(base::escape_sql_string(long_random_string));
    temp_long_random_string.insert(0, "'");
    temp_long_random_string.insert(temp_long_random_string.size(), "'");

    EXPECT_EQ(test_result, temp_long_random_string);

    test_result.clear();
    temp_long_random_string.clear();
    test_result.append(base::sqlstring("?", base::UseAnsiQuotes) << long_random_string);

    temp_long_random_string.append(base::escape_sql_string(long_random_string));
    temp_long_random_string.insert(0, "\"");
    temp_long_random_string.insert(temp_long_random_string.size(), "\"");

    EXPECT_EQ(test_result, temp_long_random_string);

    test_result.clear();
    temp_long_random_string.clear();
    test_result.append(base::sqlstring("!", base::QuoteOnlyIfNeeded) << long_random_string);

    escaped = base::escape_backticks(long_random_string);
    temp_long_random_string.append(base::quoteIdentifierIfNeeded(escaped, '`', base::MySQLVersion::MySQL57));

    EXPECT_EQ(test_result, temp_long_random_string);

    test_result.clear();
    temp_long_random_string.clear();
    escaped.clear();
    test_result.append(base::sqlstring("!", base::UseAnsiQuotes) << long_random_string);

    escaped = base::escape_backticks(long_random_string);
    temp_long_random_string.append(base::quote_identifier(escaped, '`'));

    EXPECT_EQ(test_result, temp_long_random_string);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForConstCharPtrValues) {
    const char* tail = "TAIL";
    const char* head = "HEAD";
    const char* null_str(0);

    // test cases for simple use of operator <<
    EXPECT_EQ(base::sqlstring("first_test ?", base::QuoteOnlyIfNeeded) << tail, "first_test 'TAIL'");
    EXPECT_EQ(base::sqlstring("second_test ?", base::UseAnsiQuotes) << tail, "second_test \"TAIL\"");
    EXPECT_EQ(base::sqlstring("third_test !", base::QuoteOnlyIfNeeded) << tail, "third_test TAIL");
    EXPECT_EQ(base::sqlstring("fourth_test !", base::UseAnsiQuotes) << tail, "fourth_test `TAIL`");

    // test cases for composed use of operator <<
    EXPECT_EQ(base::sqlstring("? fifth_test ?", base::QuoteOnlyIfNeeded) << head << tail, "'HEAD' fifth_test 'TAIL'");
    EXPECT_EQ(base::sqlstring("? sixth_test ?", base::UseAnsiQuotes) << head << tail, "\"HEAD\" sixth_test \"TAIL\"");
    EXPECT_EQ(base::sqlstring("! seventh_test !", base::QuoteOnlyIfNeeded) << head << tail, "HEAD seventh_test TAIL");
    EXPECT_EQ(base::sqlstring("! eighth_test !", base::UseAnsiQuotes) << head << tail, "`HEAD` eighth_test `TAIL`");

    // test cases for NULL parameter
    EXPECT_EQ(base::sqlstring("ninth_test ?", base::QuoteOnlyIfNeeded) << null_str, "ninth_test NULL");
    EXPECT_EQ(base::sqlstring("tenth_test ?", base::UseAnsiQuotes) << null_str, "tenth_test NULL");

    EXPECT_THROW(base::sqlstring("eleventh_test !", base::QuoteOnlyIfNeeded) << null_str, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("twelfth_test !", base::UseAnsiQuotes) << null_str, std::invalid_argument);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForConstCharPtrValuesCornerValues) {
    // test cases for simple use of operator <<
    std::string test_result;
    std::string temp_long_random_string;
    std::string quoted;
    test_result.append(base::sqlstring("?", base::QuoteOnlyIfNeeded) << long_random_string.c_str());

    temp_long_random_string.append(base::escape_sql_string(long_random_string.c_str()));
    temp_long_random_string.insert(0, "'");
    temp_long_random_string.insert(temp_long_random_string.size(), "'");

    EXPECT_EQ(test_result, temp_long_random_string);

    test_result.clear();
    temp_long_random_string.clear();
    test_result.append(base::sqlstring("?", base::UseAnsiQuotes) << long_random_string.c_str());

    temp_long_random_string.append(base::escape_sql_string(long_random_string.c_str()));
    temp_long_random_string.insert(0, "\"");
    temp_long_random_string.insert(temp_long_random_string.size(), "\"");

    EXPECT_EQ(test_result, temp_long_random_string);

    test_result.clear();
    temp_long_random_string.clear();
    test_result.append(base::sqlstring("!", base::QuoteOnlyIfNeeded) << long_random_string.c_str());

    quoted = base::escape_backticks(long_random_string.c_str());
    temp_long_random_string = base::quoteIdentifierIfNeeded(quoted, '`', base::MySQLVersion::MySQL57);

    EXPECT_EQ(test_result, temp_long_random_string);

    test_result.clear();
    temp_long_random_string.clear();
    quoted.clear();
    test_result.append(base::sqlstring("!", base::UseAnsiQuotes) << long_random_string.c_str());

    quoted = base::escape_backticks(long_random_string.c_str());
    temp_long_random_string.append(quoted);
    temp_long_random_string.insert(0, "`");
    temp_long_random_string.insert(temp_long_random_string.size(), "`");

    EXPECT_EQ(test_result, temp_long_random_string);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForConstSqlStringValues) {
    // test cases for simple use of operator <<
    std::string test_result = "";
    test_result.append(base::sqlstring("first_test ?", base::QuoteOnlyIfNeeded) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "first_test TAIL");

    test_result.clear();
    test_result.append(base::sqlstring("second_test ?", base::UseAnsiQuotes) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "second_test TAIL");

    test_result.clear();
    test_result.append(base::sqlstring("third_test !", base::QuoteOnlyIfNeeded) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "third_test TAIL");

    test_result.clear();
    test_result.append(base::sqlstring("fourth_test !", base::UseAnsiQuotes) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "fourth_test TAIL");

    // test cases for composed use of operator <<
    test_result.clear();
    test_result.append(base::sqlstring("? fifth_test ?", base::QuoteOnlyIfNeeded)
                       << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "HEAD fifth_test TAIL");

    test_result.clear();
    test_result.append(base::sqlstring("? sixth_test ?", base::UseAnsiQuotes)
                       << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "HEAD sixth_test TAIL");

    test_result.clear();
    test_result.append(base::sqlstring("! seventh_test !", base::QuoteOnlyIfNeeded)
                       << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "HEAD seventh_test TAIL");

    test_result.clear();
    test_result.append(base::sqlstring("! eighth_test !", base::UseAnsiQuotes)
                       << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0));

    EXPECT_EQ(test_result, "HEAD eighth_test TAIL");
  }

  TEST_F(SqlStringTest, OverriddenOperatorForConstSqlStringValuesCornerValues) {
    // test cases for simple use of operator <<
    base::sqlstring long_random_sqlstring(base::sqlstring(long_random_string.c_str(), 0));

    EXPECT_EQ(base::sqlstring("?", base::QuoteOnlyIfNeeded) << long_random_sqlstring,
              (std::string)long_random_sqlstring);
    EXPECT_EQ(base::sqlstring("?", base::UseAnsiQuotes) << long_random_sqlstring, (std::string)long_random_sqlstring);
    EXPECT_EQ(base::sqlstring("!", base::QuoteOnlyIfNeeded) << long_random_sqlstring,
              (std::string)long_random_sqlstring);
    EXPECT_EQ(base::sqlstring("!", base::UseAnsiQuotes) << long_random_sqlstring, (std::string)long_random_sqlstring);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForNumericValuesMissingExtraParameters) {
    std::int64_t int64_t_value1 = 1999;
    std::int64_t int64_t_value2 = -1999;
    int int_value1 = -2001;
    int int_value2 = 2001;
    float float_value1 = (float)3.141592;
    float float_value2 = (float)-3.141592;
    double double_value1 = 2.718281;
    double double_value2 = -2.718281;

    // test cases for missing parameters
    EXPECT_EQ(base::sqlstring("? first_test ?", 0) << int64_t_value1, "1999 first_test ?");
    EXPECT_EQ(base::sqlstring("? second_test ?", 0) << int_value1, "-2001 second_test ?");
    EXPECT_EQ(base::sqlstring("? third_test ?", 0) << float_value1, "3.141592 third_test ?");
    EXPECT_EQ(base::sqlstring("? fourth_test ?", 0) << double_value1, "2.718281 fourth_test ?");

    // test cases for extra parameters
    EXPECT_THROW(base::sqlstring("fifth_test ?", 0) << int64_t_value1 << int64_t_value2, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("sixth_test ?", 0) << int_value1 << int_value2, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("seventh_test ?", 0) << float_value1 << float_value2, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("eighth_test ?", 0) << double_value1 << double_value2, std::invalid_argument);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForStdStringValuesMissingExtraParameters) {
    std::string tail = "TAIL";
    std::string head = "HEAD";

    // test cases for missing parameters
    EXPECT_EQ(base::sqlstring("? first_test ?", base::QuoteOnlyIfNeeded) << head, "'HEAD' first_test ?");
    EXPECT_EQ(base::sqlstring("? second_test ?", base::UseAnsiQuotes) << head, "\"HEAD\" second_test ?");
    EXPECT_EQ(base::sqlstring("! third_test !", base::QuoteOnlyIfNeeded) << head, "HEAD third_test !");
    EXPECT_EQ(base::sqlstring("! fourth_test !", base::UseAnsiQuotes) << head, "`HEAD` fourth_test !");

    // test cases for extra parameters
    EXPECT_THROW(base::sqlstring("? fifth_test", base::QuoteOnlyIfNeeded) << head << tail, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("? sixth_test", base::UseAnsiQuotes) << head << tail, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("! seventh_test", base::QuoteOnlyIfNeeded) << head << tail, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("! eighth_test", base::UseAnsiQuotes) << head << tail, std::invalid_argument);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForConstCharPtrValuesMissingExtraParameters) {
    const char* tail = "TAIL";
    const char* head = "HEAD";

    // test cases for missing parameters
    EXPECT_EQ(base::sqlstring("? first_test ?", base::QuoteOnlyIfNeeded) << head, "'HEAD' first_test ?");
    EXPECT_EQ(base::sqlstring("? second_test ?", base::UseAnsiQuotes) << head, "\"HEAD\" second_test ?");
    EXPECT_EQ(base::sqlstring("! third_test !", base::QuoteOnlyIfNeeded) << head, "HEAD third_test !");
    EXPECT_EQ(base::sqlstring("! fourth_test !", base::UseAnsiQuotes) << head, "`HEAD` fourth_test !");

    // test cases for extra parameters
    EXPECT_THROW(base::sqlstring("? fifth_test", base::QuoteOnlyIfNeeded) << head << tail, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("? sixth_test", base::UseAnsiQuotes) << head << tail, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("! seventh_test", base::QuoteOnlyIfNeeded) << head << tail, std::invalid_argument);
    EXPECT_THROW(base::sqlstring("! eighth_test", base::UseAnsiQuotes) << head << tail, std::invalid_argument);
  }

  TEST_F(SqlStringTest, OverriddenOperatorForConstSqlStringValuesMissingExtraParameters) {
    // test cases for missing parameters
    EXPECT_EQ(base::sqlstring("? first_test ?", base::QuoteOnlyIfNeeded) << base::sqlstring("HEAD", 0),
              "HEAD first_test ?");
    EXPECT_EQ(base::sqlstring("? second_test ?", base::UseAnsiQuotes) << base::sqlstring("HEAD", 0),
              "HEAD second_test ?");
    EXPECT_EQ(base::sqlstring("! third_test !", base::QuoteOnlyIfNeeded) << base::sqlstring("HEAD", 0),
              "HEAD third_test !");
    EXPECT_EQ(base::sqlstring("! fourth_test !", base::UseAnsiQuotes) << base::sqlstring("HEAD", 0),
              "HEAD fourth_test !");

    // test cases for extra parameters
    EXPECT_THROW(base::sqlstring("? fifth_test", base::QuoteOnlyIfNeeded)
                   << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0),
                 std::invalid_argument);
    EXPECT_THROW(base::sqlstring("? sixth_test", base::UseAnsiQuotes)
                   << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0),
                 std::invalid_argument);
    EXPECT_THROW(base::sqlstring("! seventh_test", base::QuoteOnlyIfNeeded)
                   << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0),
                 std::invalid_argument);
    EXPECT_THROW(base::sqlstring("! eighth_test", base::UseAnsiQuotes)
                   << base::sqlstring("HEAD", 0) << base::sqlstring("TAIL", 0),
                 std::invalid_argument);
  }

  TEST_F(SqlStringTest, Constructors) {
    // constructors
    base::sqlstring c_tor_flag_QuoteOnlyIfNeeded_num("test ? ? ? ?", base::QuoteOnlyIfNeeded);
    base::sqlstring c_tor_flag_UseAnsiQuotes_num("test ? ? ? ?", base::UseAnsiQuotes);
    base::sqlstring c_tor_flag_QuoteOnlyIfNeeded_str("test ? ! ? !", base::QuoteOnlyIfNeeded);
    base::sqlstring c_tor_flag_UseAnsiQuotes_str("test ? ! ? !", base::UseAnsiQuotes);
    base::sqlstring c_tor_flag_QuoteOnlyIfNeeded_sqlstr("test ? !", base::QuoteOnlyIfNeeded);
    base::sqlstring c_tor_flag_UseAnsiQuotes_sqlstr("test ? !", base::UseAnsiQuotes);
    base::sqlstring c_tor_copy_flag_QuoteOnlyIfNeeded_str(c_tor_flag_QuoteOnlyIfNeeded_str);
    base::sqlstring c_tor_copy_flag_UseAnsiQuotes_str(c_tor_flag_UseAnsiQuotes_str);
    base::sqlstring c_tor_empty_constructor;

    // miscellaneous variables
    std::int64_t int64_t_value = 1999;
    int int_value = -2001;
    float float_value = (float)3.141592;
    double double_value = 2.718281;
    std::string tail = "TAIL";
    const char* const_tail = "TAIL";

    // test cases for constructors & numeric values
    EXPECT_EQ(c_tor_flag_QuoteOnlyIfNeeded_num << int64_t_value, "test 1999 ? ? ?");
    EXPECT_EQ(c_tor_flag_QuoteOnlyIfNeeded_num << int_value, "test 1999 -2001 ? ?");
    EXPECT_EQ(c_tor_flag_QuoteOnlyIfNeeded_num << float_value, "test 1999 -2001 3.141592 ?");
    EXPECT_EQ(c_tor_flag_QuoteOnlyIfNeeded_num << double_value, "test 1999 -2001 3.141592 2.718281");
    EXPECT_EQ(c_tor_flag_UseAnsiQuotes_num << int64_t_value, "test 1999 ? ? ?");
    EXPECT_EQ(c_tor_flag_UseAnsiQuotes_num << int_value, "test 1999 -2001 ? ?");
    EXPECT_EQ(c_tor_flag_UseAnsiQuotes_num << float_value, "test 1999 -2001 3.141592 ?");
    EXPECT_EQ(c_tor_flag_UseAnsiQuotes_num << double_value, "test 1999 -2001 3.141592 2.718281");

    // test cases for copy constructors & std::string values
    EXPECT_EQ(c_tor_copy_flag_QuoteOnlyIfNeeded_str << tail, "test 'TAIL' ! ? !");
    EXPECT_EQ(c_tor_copy_flag_UseAnsiQuotes_str << tail, "test \"TAIL\" ! ? !");
    EXPECT_EQ(c_tor_copy_flag_QuoteOnlyIfNeeded_str << tail, "test 'TAIL' TAIL ? !");
    EXPECT_EQ(c_tor_copy_flag_UseAnsiQuotes_str << tail, "test \"TAIL\" `TAIL` ? !");

    // test cases for copy constructors & (const char*) values
    EXPECT_EQ(c_tor_copy_flag_QuoteOnlyIfNeeded_str << const_tail, "test 'TAIL' TAIL 'TAIL' !");
    EXPECT_EQ(c_tor_copy_flag_UseAnsiQuotes_str << const_tail, "test \"TAIL\" `TAIL` \"TAIL\" !");
    EXPECT_EQ(c_tor_copy_flag_QuoteOnlyIfNeeded_str << const_tail, "test 'TAIL' TAIL 'TAIL' TAIL");
    EXPECT_EQ(c_tor_copy_flag_UseAnsiQuotes_str << const_tail, "test \"TAIL\" `TAIL` \"TAIL\" `TAIL`");

    // test cases for constructors & (const sqlstring&) values
    EXPECT_EQ(c_tor_flag_QuoteOnlyIfNeeded_sqlstr << base::sqlstring("TAIL", 0), "test TAIL !");
    EXPECT_EQ(c_tor_flag_UseAnsiQuotes_sqlstr << base::sqlstring("TAIL", 0), "test TAIL !");
    EXPECT_EQ(c_tor_flag_QuoteOnlyIfNeeded_sqlstr << base::sqlstring("TAIL", 0), "test TAIL TAIL");
    EXPECT_EQ(c_tor_flag_UseAnsiQuotes_sqlstr << base::sqlstring("TAIL", 0), "test TAIL TAIL");

    // test case for empty constructor
    EXPECT_EQ(c_tor_empty_constructor, "");
  }

  TEST_F(SqlStringTest, Done) {
    // constructor
    base::sqlstring sqlstr("test ? ! ? !", 0);

    std::string test_result = "";
    while (!sqlstr.done()) {
      test_result.append(sqlstr << 1 << "2" << 3.0 << base::sqlstring("4", 0));
    }

    EXPECT_EQ(test_result, "test 1 `2` 3.000000 4");
  }

  TEST_F(SqlStringTest, OverriddenOperatorStdString) {
    // constructor
    base::sqlstring sqlstr("test ? ! ? ! ?", 0);

    std::string test_result = sqlstr << 1 << "2" << 3.0 << base::sqlstring("4", 0);
    EXPECT_EQ(test_result, std::string(sqlstr));
  }

  TEST_F(SqlStringTest, MemoryManagementAndMemoryLeaks) {
    // constructor with (MAX_SIZE_RANDOM_VALUES_ARRAY x 4) placeholders
    base::sqlstring* sqlstr[MAX_SIZE_SQLSTRING_ARRAY];
    int i, j;

    // Create MAX_SIZE_SQLSTRING_ARRAY sqlstring's
    std::vector<std::string> expected;
    std::vector<std::string> actual;
    for (i = 0; i < MAX_SIZE_SQLSTRING_ARRAY; i++) {
      std::string test_result;
      std::string expected_result;
      // Each sqlstring has (MAX_SIZE_RANDOM_VALUES_ARRAY x 4) placeholders
      sqlstr[i] = new base::sqlstring(placeholders.c_str(), 0);

      for (j = 0; j < MAX_SIZE_RANDOM_VALUES_ARRAY; j++) {
        *sqlstr[i] << random_values[j].int_value << random_values[j].string_value << random_values[j].double_value
                   << random_values[j].sqlstring_value;

        expected_result.append(std::to_string(random_values[j].int_value))
          .append(" `")
          .append(random_values[j].string_value)
          .append("` ")
          .append(std::to_string(random_values[j].double_value))
          .append(" ")
          .append(random_values[j].sqlstring_value)
          .append(" ");
      }

      expected.push_back(expected_result);
      actual.push_back(*sqlstr[i]);
    }
    EXPECT_EQ(expected, actual);

    // Clean up memory without throwing exceptions
    for (i = 0; i < MAX_SIZE_SQLSTRING_ARRAY; i++) {
      delete sqlstr[i];
    }
  }
} // namespace
