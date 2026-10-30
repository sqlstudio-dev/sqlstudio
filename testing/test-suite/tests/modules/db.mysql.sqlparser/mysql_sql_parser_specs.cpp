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

#include "grt_test_helpers.h"
#include "grtsqlparser/sql_facade.h"
#include "backend/db_mysql_sql_export.h"

#include "grtsqlparser/mysql_parser_services.h"

#include "gtest/gtest.h"
#include "gtest/gtest-spi.h"
#include "context.h"
#include "wb_test_helpers.h"

using namespace parsers;

using namespace grt;

namespace {

  struct TestData {
    std::unique_ptr<SqlStudioTester> tester;
    SqlFacade::Ref sqlFacade;

    MySQLParserContext::Ref context;
    MySQLParserServices::Ref services;
    GrtVersionRef oldVersion;

    DictRef options;

    void testImportSQL(size_t test_no, const char *old_schema_name = nullptr, const char *new_schema_name = nullptr) {
      std::string dataDir = testing::Context::get().tmpDataDir() + "/modules_grt/wb_mysql_import/sql/";

      // Set filenames & messages based on test number.
      std::string number_string = std::to_string(test_no);
      std::string test_sql_filename = dataDir + number_string + ".sql";
      const std::string sql = base::replaceString(base::getTextFileContent(test_sql_filename), "\r\n", "\n");

      // We have actually 2 parser tests here for now: the old server based parser and the new ANTLR one.
      // First parse the sql with the old parser.
      //
      // The old parser has some inflexibilities (e.g. regarding key/column ordering in a CREATE TABLE)
      // so some tests fail for it now, as we use more complex sql for the new parser. Ignore those for the old parser.
      if (test_no != 8 && test_no != 9 && test_no != 16) {
        // /*
        std::string failureMessage = "SQL old (" + number_string + ")";
        std::string test_catalog_state_filename = dataDir + number_string + ".xml";

        if (g_file_test(test_catalog_state_filename.c_str(),
                        G_FILE_TEST_EXISTS)) { // Some newer tests are only done for the new parser.
          db_mysql_CatalogRef res_catalog(grt::Initialized);
          res_catalog->version(tester->getRdbms()->version());
          res_catalog->defaultCharacterSetName("utf8");
          res_catalog->defaultCollationName("utf8_general_ci");
          grt::replace_contents(res_catalog->simpleDatatypes(), tester->getRdbms()->simpleDatatypes());

          // Legacy snapshots include recovery from duplicate objects and unsupported statements.
          const int expectedErrors = test_no == 17 ? 4 : (test_no == 702 || test_no == 900 ? 1 : 0);
          ASSERT_EQ(expectedErrors, sqlFacade->parseSqlScriptStringEx(res_catalog, sql, options))
            << "Legacy parser failed for fixture " << number_string;

          // Rename the schema if asked.
          if (old_schema_name && new_schema_name)
            sqlFacade->renameSchemaReferences(res_catalog, old_schema_name, new_schema_name);

          // Unserialize the result so we can compare that with the generated catalog.
          db_mysql_CatalogRef test_catalog =
            db_mysql_CatalogRef::cast_from(ValueRef(grt::GRT::get()->unserialize(test_catalog_state_filename)));

          // Before comparing set the simple data types list to that of the rdbms. Its not part of the
          // parsing process we test here. The test data additionally doesn't contain full lists,
          // so we would get a test failure on that.
          grt::replace_contents(test_catalog->simpleDatatypes(), tester->getRdbms()->simpleDatatypes());

          testing::deepCompareGrtValues(failureMessage, res_catalog, test_catalog);
        }
        //*/
      }

      // Same steps as above but using the ANTLR parser.
      {
        //*
        std::string failureMessage = "SQL new (" + number_string + ")";
        std::string test_catalog_state_filename = dataDir + number_string + "a.xml";

        db_mysql_CatalogRef res_catalog(grt::Initialized);
        res_catalog->version(bec::parse_version("5.7.10"));
        res_catalog->defaultCharacterSetName("utf8");
        res_catalog->defaultCollationName("utf8_general_ci");
        grt::replace_contents(res_catalog->simpleDatatypes(), tester->getRdbms()->simpleDatatypes());

        ASSERT_EQ(0U, services->parseSQLIntoCatalog(context, res_catalog, sql, options))
          << "Parser failed for fixture " << number_string;

        // Rename the schema if asked.
        if (old_schema_name && new_schema_name)
          services->renameSchemaReferences(context, res_catalog, old_schema_name, new_schema_name);

        // Unserialize the result so we can compare that with the generated catalog.
        db_mysql_CatalogRef test_catalog =
          db_mysql_CatalogRef::cast_from(ValueRef(grt::GRT::get()->unserialize(test_catalog_state_filename)));
        grt::replace_contents(test_catalog->simpleDatatypes(), tester->getRdbms()->simpleDatatypes());

        testing::deepCompareGrtValues(failureMessage, res_catalog, test_catalog);
        //*/
      }
    }
  };

class MySqlParserTest : public ::testing::Test {
protected:
  TestData *data = new TestData();

  void SetUp() override {
    data->tester.reset(new SqlStudioTester());
    data->tester->initializeRuntime();

    auto rdbms = db_mgmt_RdbmsRef::cast_from(grt::GRT::get()->get("/rdbms"));
    data->oldVersion = rdbms->version();
    rdbms->version(bec::parse_version("5.7.10"));

    data->options = DictRef(true);
    data->options.set("gen_fk_names_when_empty", IntegerRef(0));

    data->sqlFacade = SqlFacade::instance_for_rdbms(data->tester->getRdbms());
    EXPECT_TRUE(data->sqlFacade != nullptr) << "failed to get sqlparser module";

    data->services = MySQLParserServices::get();
    data->context = data->services->createParserContext(data->tester->getRdbms()->characterSets(),
      data->tester->getRdbms()->version(), "", false);
  }

  void TearDown() override {
    auto rdbms = db_mgmt_RdbmsRef::cast_from(grt::GRT::get()->get("/rdbms"));
    rdbms->version(data->oldVersion);
    data->context.reset(); // TODO: find out what the context is using from the tester, so that we have to release it first.
    delete data;
  }
};

TEST_F(MySqlParserTest, TableParsing) {
  for (int i = 0; i <= 18; ++i)
    data->testImportSQL(i);
}

TEST_F(MySqlParserTest, SqlDefinitionMismatchDoesNotPrintSql) {
  db_mysql_ViewRef actual(Initialized);
  actual->sqlDefinition("SELECT actual_sql_marker");
  db_mysql_ViewRef expected = copy_object(actual);
  expected->sqlDefinition("SELECT expected_sql_marker");

  testing::TestPartResultArray results;
  {
    testing::ScopedFakeTestPartResultReporter reporter(
      testing::ScopedFakeTestPartResultReporter::INTERCEPT_ONLY_CURRENT_THREAD, &results);
    testing::deepCompareGrtValues("SQL diagnostic", actual, expected);
  }

  ASSERT_EQ(results.size(), 1);
  const std::string message = results.GetTestPartResult(0).message();
  EXPECT_TRUE(message.find("sqlDefinition") != std::string::npos);
  EXPECT_TRUE(message.find("SQL differs at byte 7") != std::string::npos);
  EXPECT_TRUE(message.find("actual_sql_marker") == std::string::npos);
  EXPECT_TRUE(message.find("expected_sql_marker") == std::string::npos);
}

  TEST_F(MySqlParserTest, IndexParsing) {
    for (size_t i : { 50, 51 })
      data->testImportSQL(i);
  }

  TEST_F(MySqlParserTest, ViewParsing) {
    for (size_t i : { 100, 101 })
      data->testImportSQL(i);
  }

  TEST_F(MySqlParserTest, RoutineParsing) {
    for (size_t i : { 150, 151, 152 })
      data->testImportSQL(i);
  }

  TEST_F(MySqlParserTest, TriggerParsing) {
    data->testImportSQL(200);
  }

  TEST_F(MySqlParserTest, EventParsing) {
    for (size_t i : { 250, 251, 252, 253 })
      data->testImportSQL(i);
  }

  TEST_F(MySqlParserTest, LogfileGroupAndTablespace) {
    data->testImportSQL(300);
  }

  TEST_F(MySqlParserTest, ServerLink) {
    data->testImportSQL(350);
  }

  TEST_F(MySqlParserTest, AlterStatements) {
    data->testImportSQL(400);
  }

  TEST_F(MySqlParserTest, DropStatements) {
    data->testImportSQL(450);
  }

  TEST_F(MySqlParserTest, ReuseStubTablesAndColumns) {
    data->testImportSQL(600);
  }

  TEST_F(MySqlParserTest, SakilaDbSchemaStructures) {
    data->testImportSQL(700);
  }

  TEST_F(MySqlParserTest, SakilaDbInsertsAndTriggers) {
    data->testImportSQL(701);
  }

  TEST_F(MySqlParserTest, SakilaDbImportDump) {
    data->testImportSQL(702);
  }

  TEST_F(MySqlParserTest, SakilaDbImportDumpWithSchemaRename) {
    data->testImportSQL(703, "sakila", "new_schema_name");
  }

  TEST_F(MySqlParserTest, AnotherSchemaRenameTest) {
    data->testImportSQL(900, "test", "new_schema_name");
  }
}
