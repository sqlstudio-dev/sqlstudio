/*
 * Copyright (c) 2018, 2019, Oracle and/or its affiliates. All rights reserved.
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

#define DEFINE_TEST_MODULES_CODE
#include "wb_test_helpers.h"
#include "structs.test.h"
#include "grtpp_module_cpp.h"
#include "test_modules.h"

#include "gtest/gtest.h"

class TestModuleImpl : public grt::ModuleImplBase { // this module does not implement everything from the interface
public:
  TestModuleImpl(grt::CPPModuleLoader *ldr) : grt::ModuleImplBase(ldr) {
  }

  DEFINE_INIT_MODULE("1.0", "", grt::ModuleImplBase, DECLARE_MODULE_FUNCTION(TestModuleImpl::returnNull), NULL);

  grt::ObjectRef returnNull() {
    return grt::ObjectRef();
  }
};

/*
 * Copyright (c) 2018, 2019, Oracle and/or its affiliates. All rights reserved.
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

#define DEFINE_TEST_MODULES_CODE
#include "wb_test_helpers.h"
#include "structs.test.h"
#include "grtpp_module_cpp.h"
#include "test_modules.h"

#include "gtest/gtest.h"
#include "context.h"

//class TestModuleImpl : public grt::ModuleImplBase { // this module does not implement everything from the interface
//public:
//  TestModuleImpl(grt::CPPModuleLoader *ldr) : grt::ModuleImplBase(ldr) {
//  }
//
//  DEFINE_INIT_MODULE("1.0", "", grt::ModuleImplBase, DECLARE_MODULE_FUNCTION(TestModuleImpl::returnNull), NULL);
//
//  grt::ObjectRef returnNull() {
//    return grt::ObjectRef();
//  }
//};

namespace testing {

class GRTCppModulesTest : public ::testing::Test {
protected:
  void SetUp() override {
    // We have to excplicitly cleanup grt, as we don't know which spec was before
    // otherwise this one will fail.
    grt::GRT::get()->reinitialiseForTests();
    register_structs_test_xml();
    grt::GRT::get()->load_metaclasses(Context::get().tmpDataDir() + "/structs.test.xml");
    grt::GRT::get()->end_loading_metaclasses();
    grt::InterfaceImplBase::Register<SampleInterface1Impl>();
    grt::InterfaceImplBase::Register<SampleInterface2Impl>();
    grt::GRT::get()->get_native_module<SampleModule1Impl>();
    grt::GRT::get()->get_native_module<SampleModule2Impl>();
    grt::GRT::get()->get_native_module<SampleModule3Impl>();
    grt::GRT::get()->get_native_module<TestModuleImpl>();
  }

  void TearDown() override {
    SqlStudioTester::reinitGRT();
  }
};

//-----------------------------------------------------------------------------------------------------

TEST_F(GRTCppModulesTest, LoadStructures) {
  EXPECT_EQ(grt::GRT::get()->get_metaclasses().size(), 6U);
}

TEST_F(GRTCppModulesTest, LoadInvalidModule) {
  EXPECT_THROW(grt::GRT::get()->get_native_module<BadModuleImpl>(), std::exception);
}

//-----------------------------------------------------------------------------------------------------

TEST_F(GRTCppModulesTest, ModuleInterfaceRegistration) {
  EXPECT_EQ(grt::GRT::get()->get_interfaces().size(), 2UL);
  EXPECT_EQ(grt::GRT::get()->get_modules().size(), 4UL);

  // interfaces[0]
  const grt::Interface *iface = grt::GRT::get()->get_interface("SampleInterface1");

  EXPECT_EQ(iface->name(), "SampleInterface1");
  EXPECT_EQ(iface->get_functions().size(), 2U);
  EXPECT_TRUE(iface->extends().empty());
  EXPECT_TRUE(iface->get_interfaces().empty());

  // virtual int getNumber()= 0;
  const grt::Module::Function *f = &iface->get_functions()[0];

  EXPECT_TRUE(f->arg_types.empty());
  EXPECT_EQ(f->name, "getNumber");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");
  EXPECT_EQ((int)f->ret_type.content.type, grt::UnknownType);
  EXPECT_EQ(f->ret_type.content.object_class, "");

  // virtual int calculate()= 0;
  f = &iface->get_functions()[1];

  EXPECT_TRUE(f->arg_types.empty());
  EXPECT_EQ(f->name, "calculate");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // interfaces[1]
  iface = grt::GRT::get()->get_interface("SampleInterface2");

  EXPECT_EQ(iface->name(), "SampleInterface2");
  EXPECT_EQ(iface->get_functions().size(), 1U);
  EXPECT_TRUE(iface->extends().empty());
  EXPECT_TRUE(iface->get_interfaces().empty());

  // virtual int calcSum(int num1)= 0;
  f = &iface->get_functions()[0];

  EXPECT_EQ(f->arg_types.size(), 1U);
  EXPECT_EQ((int)f->arg_types[0].type.base.type, grt::IntegerType);
  EXPECT_TRUE(f->arg_types[0].type.base.object_class.empty());
  EXPECT_EQ(f->name, "calcSum");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_TRUE(f->ret_type.base.object_class.empty());
}

//-----------------------------------------------------------------------------------------------------

TEST_F(GRTCppModulesTest, ModuleLoadingAndInteraction) {
  const grt::Module::Function *f;

  EXPECT_FALSE(grt::GRT::get()->get_modules().empty());
  grt::Module *m = grt::GRT::get()->get_modules()[0];

  EXPECT_NE(dynamic_cast<SampleModule1Impl*>(m), nullptr);

  EXPECT_EQ(m->name(), "SampleModule1");
  EXPECT_EQ(m->get_functions().size(), 2U);
  EXPECT_TRUE(m->extends().empty());
  EXPECT_NE(grt::GRT::get()->get_interface(m->get_interfaces()[0]), nullptr);

  // int getNumber();
  f = &m->get_functions()[0];

  EXPECT_EQ(f->arg_types.size(), 0U);
  EXPECT_EQ(f->name, "getNumber");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // int calculate();
  f = &m->get_functions()[1];

  EXPECT_EQ(f->arg_types.size(), 0U);
  EXPECT_EQ(f->name, "calculate");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // modules[1]
  m = grt::GRT::get()->get_modules()[1];

  EXPECT_EQ(m->name(), "SampleModule2");
  EXPECT_EQ(m->get_functions().size(), 1U);
  EXPECT_TRUE(m->extends().empty());
  EXPECT_NE(grt::GRT::get()->get_interface(m->get_interfaces()[0]), nullptr);

  // virtual int calcSum(int num1)= 0;
  f = &m->get_functions()[0];

  EXPECT_EQ(f->arg_types.size(), 1U);
  EXPECT_EQ((int)f->arg_types[0].type.base.type, grt::IntegerType);
  EXPECT_EQ(f->arg_types[0].type.base.object_class, "");
  EXPECT_EQ(f->name, "calcSum");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // modules[2]
  m = grt::GRT::get()->get_modules()[2];

  EXPECT_EQ(m->name(), "SampleModule3");
  EXPECT_EQ(m->get_functions().size(), 6U);
  EXPECT_EQ(m->extends(), "SampleModule2");

  // doSomethingWithNumbers
  f = &m->get_functions()[0];

  EXPECT_EQ(f->arg_types.size(), 4U);
  EXPECT_EQ((int)f->arg_types[0].type.base.type, grt::IntegerType);
  EXPECT_EQ(f->arg_types[0].type.base.object_class, "");
  EXPECT_EQ((int)f->arg_types[1].type.base.type, grt::DoubleType);
  EXPECT_EQ(f->arg_types[1].type.base.object_class, "");
  EXPECT_EQ((int)f->arg_types[2].type.base.type, grt::IntegerType);
  EXPECT_EQ(f->arg_types[2].type.base.object_class, "");
  EXPECT_EQ((int)f->arg_types[3].type.base.type, grt::DoubleType);
  EXPECT_EQ(f->arg_types[3].type.base.object_class, "");
  EXPECT_EQ(f->name, "doSomethingWithNumbers");
  EXPECT_EQ((int)f->ret_type.base.type, grt::StringType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // doSomethingWithObject
  f = &m->get_functions()[1];

  EXPECT_EQ(f->arg_types.size(), 1U);
  EXPECT_EQ((int)f->arg_types[0].type.base.type, grt::ObjectType);
  // XXX
  EXPECT_EQ(f->arg_types[0].type.base.object_class, "Object");
  EXPECT_EQ(f->name, "doSomethingWithObject");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // doSomethingWithNumberList
  f = &m->get_functions()[2];

  EXPECT_EQ(f->arg_types.size(), 1U);
  EXPECT_EQ((int)f->arg_types[0].type.base.type, grt::ListType);
  EXPECT_EQ(f->arg_types[0].type.base.object_class, "");
  EXPECT_EQ((int)f->arg_types[0].type.content.type, grt::IntegerType);
  EXPECT_EQ(f->arg_types[0].type.content.object_class, "");
  EXPECT_EQ(f->name, "doSomethingWithNumberList");
  EXPECT_EQ((int)f->ret_type.base.type, grt::IntegerType);
  EXPECT_EQ(f->ret_type.base.object_class, "");

  // doSomethingWithTypedObject
  f = &m->get_functions()[3];

  EXPECT_EQ(f->arg_types.size(), 2U);
  EXPECT_EQ((int)f->arg_types[0].type.base.type, grt::StringType);
  EXPECT_EQ((int)f->arg_types[1].type.base.type, grt::ObjectType);
  EXPECT_EQ(f->arg_types[1].type.base.object_class, "test.Author"); // unsure
  EXPECT_EQ(f->name, "doSomethingWithTypedObject");
  EXPECT_EQ((int)f->ret_type.base.type, grt::ListType);
  EXPECT_EQ(f->ret_type.base.object_class, "");
}

//-----------------------------------------------------------------------------------------------------

TEST_F(GRTCppModulesTest, TestModuleCalling) {
  grt::Module *module = grt::GRT::get()->get_module("SampleModule1");

  ASSERT_NE(module, nullptr);

  grt::BaseListRef args(grt::AnyType);
  grt::ValueRef result;

  result = module->call_function("getNumber", args);
  EXPECT_EQ(*grt::IntegerRef::cast_from(result), 42);
}

//-----------------------------------------------------------------------------------------------------

TEST_F(GRTCppModulesTest, FunctionsReturningNULLValueWereCausingException) {
  grt::Module *module = grt::GRT::get()->get_module("TestModule");

  ASSERT_NE(module, nullptr);

  grt::BaseListRef args(grt::AnyType);
  grt::ValueRef result;

  result = module->call_function("returnNull", args);
  EXPECT_FALSE(result.is_valid());
}

//-----------------------------------------------------------------------------------------------------

}
