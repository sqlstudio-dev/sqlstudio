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

#include "grt.h"

#include "gtest/gtest.h"

// test class outside any namespace
class Foo {
public:
  int member1;
  double member2;
};

using namespace std;
using namespace grt;

namespace {

class GRTWrapperTest : public ::testing::Test {
};

TEST_F(GRTWrapperTest, GetFullTypeNameAndGetTypeName) {
    string name = get_full_type_name(typeid(Foo));
    EXPECT_EQ(name, "Foo");

    name = get_full_type_name(typeid(Foo().member1));
    EXPECT_EQ(name, "int");

    name = get_type_name(typeid(Foo));
    EXPECT_EQ(name, "Foo");

    name = get_type_name(typeid(int));
    EXPECT_EQ(name, "int");
}

TEST(GRTWrapperStandaloneTest, OsErrorException) {
    os_error* error = new os_error("dummy");

    EXPECT_STREQ(error->what(), "dummy");
    delete error;

    error = new os_error(5);
    EXPECT_STREQ(error->what(), g_strerror(5));
    delete error;
}

TEST(GRTWrapperStandaloneTest, TypeErrorException) {
    Type expected = StringType;
    Type actual = DoubleType;
    Type container = ListType;

    type_error* error = new type_error("dummy");
    EXPECT_STREQ(error->what(), "dummy");
    delete error;

    error = new type_error("foo", "bar");
    EXPECT_STREQ(error->what(), "Type mismatch: expected object of type foo, but got bar");
    delete error;

    error = new type_error("foo", "bar", container);
    EXPECT_STREQ(error->what(), "Type mismatch: expected content object of type foo, but got bar");
    delete error;

    error = new type_error(expected, actual);
    EXPECT_STREQ(error->what(), "Type mismatch: expected type string, but got real");
    delete error;

    error = new type_error(expected, actual, container);
    EXPECT_STREQ(error->what(), "Type mismatch: expected content-type string, but got real");
    delete error;
}

TEST(GRTWrapperStandaloneTest, NullValueException) {
    EXPECT_STREQ(null_value("dummy").what(), "dummy");
    EXPECT_STREQ(null_value().what(), "Attempt to operate on a NULL GRT value.");
}

TEST(GRTWrapperStandaloneTest, BadItemException) {
    EXPECT_STREQ(bad_item("dummy").what(), "Invalid item name 'dummy'");
    EXPECT_STREQ(bad_item(10, 100).what(), "Index out of range");
}

TEST(GRTWrapperStandaloneTest, GrtRuntimeErrorException) {
    grt_runtime_error error("dummy", "details");
    EXPECT_STREQ(error.what(), "dummy");
    EXPECT_EQ(error.detail, "details");
    EXPECT_FALSE(error.fatal);

    error = grt_runtime_error("foo", "bar", false);
    EXPECT_STREQ(error.what(), "foo");
    EXPECT_EQ(error.detail, "bar");
    EXPECT_FALSE(error.fatal);

    error = grt_runtime_error("foo", "bar", true);
    EXPECT_STREQ(error.what(), "foo");
    EXPECT_EQ(error.detail, "bar");
    EXPECT_TRUE(error.fatal);
}

// Notes:
// - Tests for Struct are in struct_specs.cpp.
// - Tests for GRT values are in value_specs.cpp.

}
