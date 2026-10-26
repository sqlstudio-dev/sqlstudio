/*
 * Copyright (c) 2019, 2022, Oracle and/or its affiliates.
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

#include "wb_test_helpers.h"
#include "studio/wb_module.h"

#include "gtest/gtest.h"

namespace {

  struct TestData {
    std::unique_ptr<SqlStudioTester> tester;
  };

} // anonymous namespace

class WbModuleTestsForSqlStudioTest : public ::testing::Test {
protected:
  TestData* data = new TestData();

  void SetUp() override {
    data->tester.reset(new SqlStudioTester());
  }

  void TearDown() override {
    delete data;
  }
};

TEST_F(WbModuleTestsForSqlStudioTest, SupportedOsTest) {
  // As we move out of supporting old operating systems, we will need to update both this test and isOsSupported()
  // So if it's failing and it wasn't before, that's probably why - just update them.

  // proxy function for a module call
  grt::Module* module = grt::GRT::get()->get_module("SqlStudio");
  auto isOsSupportedProxy = [module](std::string const& os) -> bool {
    grt::StringListRef arguments(grt::Initialized);
    arguments.ginsert(grt::StringRef(os));

    grt::ValueRef result = module->call_function("isOsSupported", arguments);
    return *grt::IntegerRef::cast_from(result) != 0;
  };

  // unrecognised OS
  EXPECT_FALSE(isOsSupportedProxy(""));
  EXPECT_FALSE(isOsSupportedProxy("Some OS"));
  EXPECT_TRUE(
    isOsSupportedProxy("unknown")); // special flag returned by get_local_os_name() when it was unable to get OS info

  // windows
  EXPECT_FALSE(isOsSupportedProxy("Windows"));
  EXPECT_FALSE(isOsSupportedProxy("Windows 98"));
  EXPECT_FALSE(isOsSupportedProxy("..... Windows 98 ....."));
  EXPECT_TRUE(isOsSupportedProxy("Windows 10"));
  EXPECT_TRUE(isOsSupportedProxy("..... Windows 10 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... Windows ..... 10 ....."));
  EXPECT_TRUE(isOsSupportedProxy("..... Windows 11 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... Windows ..... 11 ....."));

  // debian-based
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 12.04"));
  EXPECT_FALSE(isOsSupportedProxy("..... Ubuntu 12.04 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 15.04"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 15.04 i386"));
  EXPECT_FALSE(isOsSupportedProxy("..... Ubuntu 15.04 i386 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 16.04 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("..... Ubuntu ..... 16.04 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... Ubuntu 16.04 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 16.04.2 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 18.10 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("..... Ubuntu 18.10 ..... x86_64 ....."));

  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 18.04"));
  EXPECT_FALSE(isOsSupportedProxy("..... Ubuntu 18.04 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 19.04"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 19.10"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 20.04"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 20.10"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 21.04"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 21.10"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 22.04"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 22.10"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 23.04"));
  EXPECT_FALSE(isOsSupportedProxy("Ubuntu 23.10"));
  EXPECT_TRUE(isOsSupportedProxy("Ubuntu 24.04"));
  EXPECT_TRUE(isOsSupportedProxy("Ubuntu 24.10"));

  // red-hat based
  EXPECT_FALSE(isOsSupportedProxy("Red Hat Enterprise Linux Server release"));
  EXPECT_FALSE(isOsSupportedProxy("Red Hat Enterprise Linux Server release 6"));
  EXPECT_FALSE(isOsSupportedProxy("..... Red Hat Enterprise Linux Server release 6 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Red Hat Enterprise Linux Server release 7"));
  EXPECT_FALSE(isOsSupportedProxy("..... Red Hat Enterprise Linux Server release 7 i386 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Red Hat Enterprise Linux Server release 7.1 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("..... Red Hat Enterprise Linux Server release 7.1 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... Red Hat Enterprise Linux Server release ..... 7.1 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... Red Hat Enterprise Linux release 8 ....."));
  EXPECT_FALSE(isOsSupportedProxy("Red Hat Enterprise Linux release 8.0"));
  EXPECT_FALSE(isOsSupportedProxy("..... Red Hat Enterprise Linux release 8.0"));
  EXPECT_TRUE(isOsSupportedProxy("..... Red Hat Enterprise Linux release 9 ....."));
  EXPECT_TRUE(isOsSupportedProxy("Red Hat Enterprise Linux release 9.0"));
  EXPECT_TRUE(isOsSupportedProxy("..... Red Hat Enterprise Linux release 9.0"));

  // mac
  EXPECT_FALSE(isOsSupportedProxy("Mac OS"));
  EXPECT_FALSE(isOsSupportedProxy("OS X 10.1"));
  EXPECT_FALSE(isOsSupportedProxy("..... OS X 10.1 ....."));
  EXPECT_FALSE(isOsSupportedProxy("OS X 10.10"));
  EXPECT_FALSE(isOsSupportedProxy("OS X 10.10 i386"));
  EXPECT_FALSE(isOsSupportedProxy("macOS 10.14"));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS 10.14 i386 ....."));
  EXPECT_FALSE(isOsSupportedProxy("macOS 10.15 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS 10.15 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS ..... 10.15 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("macOS 11.2"));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS 11.5 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS ..... 11.2 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS 12 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("macOS 12"));
  EXPECT_FALSE(isOsSupportedProxy("..... macOS 13 ..... x86_64 ....."));
  EXPECT_FALSE(isOsSupportedProxy("macOS 13"));
  EXPECT_TRUE(isOsSupportedProxy("..... macOS 14 ..... x86_64 ....."));
  EXPECT_TRUE(isOsSupportedProxy("macOS 14"));

  // other debian-based
  EXPECT_FALSE(isOsSupportedProxy("Debian 5 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Debian 9 x86_64"));
  EXPECT_TRUE(isOsSupportedProxy("Debian 10 x86_64"));

  // other red-hat-based
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 26 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 27 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 28 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 29 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 30 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 31 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 32 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 33 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 34 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 35 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 36 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 37 x86_64"));
  EXPECT_FALSE(isOsSupportedProxy("Fedora release 38 x86_64"));
  EXPECT_TRUE(isOsSupportedProxy("Fedora release 39 x86_64"));
  EXPECT_TRUE(isOsSupportedProxy("Fedora release 40 x86_64"));
}
