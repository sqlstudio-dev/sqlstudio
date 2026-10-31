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
#include "wb_test_helpers.h"
#include "../cdbc/connector_test_fixture.h"
#include "context.h"

#include "backend/db_rev_eng_be.h"
#include "module_db_mysql.h"

#include "diff/changelistobjects.h"
#include "grtdb/db_helpers.h"

namespace testing {

  using namespace parsers;

#define DATABASE_TESTS 1
#define TABLE_TESTS 1
#define TABLE_PARTITION_TESTS 1
#define VIEW_TESTS 1
#define ROUTINE_TESTS 1
#define TRIGGER_TESTS 1
#define CREATE_TESTS 1

  struct TestEntry {
    std::string description;
    std::string objectName;
    std::string cleanup;
    std::string org;
    std::string mod;
  };

  db_mysql_CatalogRef createEmptyCatalog(const db_mgmt_RdbmsRef &rdbms) {
    db_mysql_CatalogRef catalog(grt::Initialized);
    catalog->version(bec::parse_version("5.7.10"));
    catalog->name("default");
    for (size_t i = 0; i < rdbms->simpleDatatypes().count(); ++i)
      catalog->simpleDatatypes().insert(rdbms->simpleDatatypes().get(i));
    return catalog;
  }

  class GRTDiffAlterTest : public sql::test::MockConnectorFixture {
  protected:
    SqlFacade::Ref sqlParser;
    DbMySQLImpl *diffsqlModule;
    grt::DbObjectMatchAlterOmf omf;
    sql::ConnectionWrapper connection;
    sql::test::MockConnection *mockConnection = nullptr;

    MySQLParserServices::Ref services;
    parsers::MySQLParserContext::Ref context;

    grt::NormalizedComparer normalizer;
    std::string dataDir;

    std::shared_ptr<grt::DiffChange> createDiff(TestEntry const &entry, db_mysql_CatalogRef org_cat,
                                                db_mysql_CatalogRef mod_cat) {
      grt::DictRef options(true);

      std::string org_script = "CREATE DATABASE grtdiff_alter_test DEFAULT CHARACTER SET latin1;\n" + entry.org + ";";
      services->parseSQLIntoCatalog(context, org_cat, org_script, options);

      std::string mod_script = "CREATE DATABASE grtdiff_alter_test DEFAULT CHARACTER SET latin1;\n" + entry.mod + ";";
      services->parseSQLIntoCatalog(context, mod_cat, mod_script, options);

      if (entry.description == "C CR C")
        mod_cat->schemata().get(0)->tables().get(0)->columns().get(1)->oldName("t");
      else if (entry.description == "C CR> C")
        mod_cat->schemata().get(0)->tables().get(0)->columns().get(2)->oldName("t");
      else if (entry.description == "C CR< C")
        mod_cat->schemata().get(0)->tables().get(0)->columns().get(0)->oldName("t");
      else if (entry.description == "C CR*> C")
        mod_cat->schemata().get(0)->tables().get(0)->columns().get(2)->oldName("t");
      else if (entry.description == "C CR*< C")
        mod_cat->schemata().get(0)->tables().get(0)->columns().get(0)->oldName("t");
      else if (entry.description == "Rename table")
        mod_cat->schemata().get(0)->tables().get(0)->oldName("t1");
      else if (entry.description == "Rename view")
        mod_cat->schemata().get(0)->views().get(0)->oldName("v1");

      grt::ValueRef default_engine = bec::GRTManager::get()->get_app_option("db.mysql.Table:tableEngine");
      std::string default_engine_name;
      if (grt::StringRef::can_wrap(default_engine))
        default_engine_name = grt::StringRef::cast_from(default_engine);

      bec::CatalogHelper::apply_defaults(mod_cat, default_engine_name);
      bec::CatalogHelper::apply_defaults(org_cat, default_engine_name);

      return diff_make(org_cat, mod_cat, &omf);
    }

    void runTestsForEntries(std::vector<TestEntry> const &entries, size_t resultIndex) {
      std::shared_ptr<grt::DiffChange> alter_change;

      grt::DictRef options(true);
      for (auto &entry : entries) {
        SCOPED_TRACE(entry.description);
        db_mysql_CatalogRef org_cat = createEmptyCatalog(tester->getRdbms());
        db_mysql_CatalogRef mod_cat = createEmptyCatalog(tester->getRdbms());

        alter_change = createDiff(entry, org_cat, mod_cat);
        EXPECT_NE(nullptr, alter_change) << "Empty alter change";

        // 1. generate alter
        grt::StringListRef alter_map(grt::Initialized);
        grt::ListRef<GrtNamedObject> alter_object_list(true);
        grt::DictRef options(true);

        options.set("UseFilteredLists", grt::IntegerRef(0));
        options.set("OutputContainer", alter_map);
        options.set("OutputObjectContainer", alter_object_list);
        options.set("CaseSensitive", grt::IntegerRef(omf.case_sensitive));

        diffsqlModule->generateSQL(mod_cat, options, alter_change);
        diffsqlModule->makeSQLSyncScript(mod_cat, options, alter_map, alter_object_list);
        std::string export_sql_script = options.get_string("OutputScript");

        // Validate generated SQL with the real parser, then exercise script execution
        // against strict connector mocks. This does not emulate server-side DDL or
        // reverse engineering; report fixtures below independently verify the diff.
        ASSERT_FALSE(export_sql_script.empty());
        executeMockScript(entry.cleanup);
        executeMockScript(entry.org);
        executeMockScript(export_sql_script, true);
        executeMockScript(entry.cleanup);

        // 5. Generate diff report
        {
          grt::StringListRef alter_map(grt::Initialized);
          grt::DictRef options(true);
          options.set("UseFilteredLists", grt::IntegerRef(0));
          options.set("OutputContainer", alter_map);
          options.set("TemplateFile", grt::StringRef(dataDir + "/reporting/Basic_Text.tpl/basic_text_report.txt.tpl"));
          options.set("OmitSchemas", grt::IntegerRef(1));
          options.set("SeparateForeignKeys", grt::IntegerRef(0));

          std::string report = diffsqlModule->generateReport(org_cat, options, alter_change);
          std::string reportFile =
            dataDir + "/reporting/Basic_Text.tpl/reports/testres_longname" + std::to_string(resultIndex) + ".txt";
          std::string expected = base::getTextFileContent(reportFile);
          EXPECT_EQ(base::replaceString(expected, "\r\n", "\n"), report);

          options.set("OmitSchemas", grt::IntegerRef(0));
          report = diffsqlModule->generateReport(org_cat, options, alter_change);
          reportFile =
            dataDir + "/reporting/Basic_Text.tpl/reports/testres_shortname" + std::to_string(resultIndex) + ".txt";
          expected = base::getTextFileContent(reportFile);
          EXPECT_EQ(base::replaceString(expected, "\r\n", "\n"), report);
        }

        ++resultIndex;
      }
    }

    void SetUp() override {
      dataDir = Context::get().tmpDataDir();

      MockConnectorFixture::SetUp();
      tester->initializeRuntime();

      omf.dontdiff_mask = 3;
      diffsqlModule = grt::GRT::get()->get_native_module<DbMySQLImpl>();
      EXPECT_NE(nullptr, diffsqlModule) << "DiffSQLGen module initialization";

      std::string target_version = bec::GRTManager::get()->get_app_option_string("DefaultTargetMySQLVersion");
      if (target_version.empty())
        target_version = "5.5.49";
      tester->getRdbms()->version(parse_version(target_version));

      mockConnection = expectConnection();
      connection = manager->getConnection(connectionProperties);
      ASSERT_NE(nullptr, connection.get());

      sqlParser = SqlFacade::instance_for_rdbms_name("Mysql");
      EXPECT_NE(nullptr, sqlParser) << "failed to get sqlparser module";

      services = MySQLParserServices::get();
      context =
        services->createParserContext(tester->getRdbms()->characterSets(), tester->getRdbms()->version(), "", false);

      normalizer.init_omf(&omf);

    }

    void TearDown() override {
      connection.reset();
      context.reset();
      services = nullptr;
      sqlParser = nullptr;
      MockConnectorFixture::TearDown();
    }

    void executeMockScript(const std::string &script, bool validateSyntax = false) {
      auto mock = sql::test::newMock<sql::test::MockStatement>();
      EXPECT_CALL(*mockConnection, createStatement()).WillOnce(sql::test::transferOwnership(mock));
      std::unique_ptr<sql::Statement> statement(connection->createStatement());
      std::vector<StatementRange> ranges;
      services->determineStatementRanges(script.c_str(), script.size(), ";", ranges);
      {
        InSequence sequence;
        for (const auto &range : ranges) {
          const std::string query(script, range.start, range.length);
          if (validateSyntax) {
            // Preserve semicolons inside procedure and trigger bodies.
            EXPECT_EQ(1, sqlParser->checkSqlSyntax("DELIMITER $\n" + query + "\n$")) << query;
          }
          EXPECT_CALL(*mock, execute(sql::SQLString(query))).WillOnce(Return(false));
        }
      }
      tester->executeScript(statement.get(), script);
    }
  };

  TEST_F(GRTDiffAlterTest, CreateAndDropSchema) {
    std::vector<TestEntry> entries = {
      { "Create database", "grtdiff_alter_test", "DROP DATABASE IF EXISTS grtdiff_alter_test2;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;CREATE DATABASE grtdiff_alter_test2 /*!40100 DEFAULT "
        "CHARACTER "
        "SET latin1 */;" },
      { "Drop database", "grtdiff_alter_test", "DROP DATABASE IF EXISTS grtdiff_alter_test2;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;CREATE DATABASE grtdiff_alter_test2 /*!40100 DEFAULT "
        "CHARACTER "
        "SET latin1 */;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;" },
    };

    runTestsForEntries(entries, 0);
  }

  TEST_F(GRTDiffAlterTest, AlterTableAddColumn) {
    std::vector<TestEntry> entries = {
      { "C C C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C C C+ C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT, t2 TEXT) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C+ C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C+ C C+ C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C+ C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C C+ C C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C+ C C+ C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL, t2 TEXT, "
        "t3 "
        "TEXT) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C+ C+ C C+ C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, t2 TEXT, `id2` int(11) DEFAULT NULL, "
        "t3 "
        "TEXT, t4 TEXT) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C+ C+ C C+ C+ C C+ C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, t2 TEXT, `id` int(11) DEFAULT NULL, t3 TEXT, t4 TEXT, `id2` "
        "int(11) "
        "DEFAULT NULL, t5 TEXT, t6 TEXT) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
    };

    runTestsForEntries(entries, 2);
  }

  TEST_F(GRTDiffAlterTest, AlterTableDropColumn) {
    std::vector<TestEntry> entries = {
      { "C C C-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C C C- C-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT, t2 TEXT) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C- C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C- C C- C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C C- C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C C- C C-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C C- C C- C-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL, t2 TEXT, "
        "t3 "
        "TEXT) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C C- C- C C- C-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, t2 TEXT, `id2` int(11) DEFAULT NULL, "
        "t3 "
        "TEXT, t4 TEXT) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "C- C- C C- C- C C- C-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, t2 TEXT, `id` int(11) DEFAULT NULL, t3 TEXT, t4 TEXT, `id2` "
        "int(11) "
        "DEFAULT NULL, t5 TEXT, t6 TEXT) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
    };

    runTestsForEntries(entries, 11);
  }

  TEST_F(GRTDiffAlterTest, AlterTableAddDropColumnMix) {
    std::vector<TestEntry> entries = {
      { "C- C- C+ C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, t2 TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t3 TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C+ C+ C- C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t2 TEXT, t3 TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C- C C+ C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C- C C- C+ C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t3 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C+ C C- C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C+ C C+ C- C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, t3 TEXT, t4 TEXT, `id2` int(11) "
        "DEFAULT "
        "NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C- C C C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C- C C C- C+ C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t3 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C+ C C C- C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT, `id3` "
        "int(11) "
        "DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t2 TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, `id3` "
        "int(11) "
        "DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C- C C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C C- C C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C C- C C- C+ C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL, t2 TEXT, "
        "`id3` "
        "int(11) DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t3 TEXT, `id3` "
        "int(11) "
        "DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C+ C C- C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t2 TEXT, `id3` "
        "int(11) "
        "DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL, `id3` "
        "int(11) "
        "DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C C- C- C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT, t2 TEXT) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t3 TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
    };

    runTestsForEntries(entries, 20);
  }

  TEST_F(GRTDiffAlterTest, AlterTableColumnPositionAndContentChange) {
    std::vector<TestEntry> entries = {
      { "C> C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C C> C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C C< C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C*", // a test for precision/scale
        "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` DECIMAL) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` DECIMAL(8,2)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C*> C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT NOT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C* C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t int(11) DEFAULT NULL, `id2` int(11) DEFAULT "
        "NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C* C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t int(11) DEFAULT NULL, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT "
        "NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C*> C C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t int(11) DEFAULT NULL, `id2` int(11) DEFAULT "
        "NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C*> C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t int(11) DEFAULT "
        "NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C C*< C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t int(11) DEFAULT NULL, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT "
        "NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C CR C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t2 TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C CR> C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C CR< C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t2 TEXT, `id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C CR*> C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t2 TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C CR*< C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, t TEXT, `id2` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t2  int(11) DEFAULT NULL, `id` int(11) DEFAULT NULL, `id2` int(11) "
        "DEFAULT "
        "NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "C> C C+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id2` int(11) DEFAULT NULL, `id` int(11) DEFAULT NULL, t TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C< C+ C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id2` int(11) DEFAULT NULL, t TEXT, `id` int(11) DEFAULT NULL) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "C> C- C", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, t TEXT) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (t TEXT, `id` int(11) DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      // TODO: test USING HASH and other indexKind values
      { "I(1)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, KEY `idx1` (`id`)) ENGINE=InnoDB DEFAULT "
        "CHARSET=latin1" },
      { "I(1)+ I(1)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, KEY `idx1` (`id`), KEY `idx2` (`id`)) "
        "ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1" },
      { "I(2)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "I(2)+ I(2)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`), KEY `idx2` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "I(2)-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "I(2)- I(2)-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`), KEY `idx2` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1" },
      { "I(2)- I(2)- I(2)+ I(2)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`), KEY `idx2` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx3` (`id`, "
        "`id2`), KEY `idx4` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "I(2)*", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx2` (`id`)) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "I(1)* I(2)*", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`), "
        "KEY "
        "`idx2` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) DEFAULT NULL, `id2` int(11) DEFAULT NULL, KEY `idx1` (`id`, "
        "`id2`), KEY `idx2` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "PK(2)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0') "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "PK(2)-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0') "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "PK(2)*", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "PK(2)+ I(2)+", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0') "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`, `id2`), KEY `idx1` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "PK(2)- I(2)-", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`, `id2`), KEY `idx1` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0') "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "PK(2)* I(2)*", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`, `id2`), KEY `idx1` (`id`, `id2`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', `id2` int(11) NOT NULL DEFAULT '0', "
        "PRIMARY "
        "KEY (`id`), KEY `idx1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F+", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`) ) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F+ F+", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`), CONSTRAINT `c2` FOREIGN KEY (`id`) REFERENCES "
        "`grtdiff_alter_test`.`ref_t2` (`id`) ) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F-", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`) ) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1;" },
      { "F- F-", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`), CONSTRAINT `c2` FOREIGN KEY (`id`) REFERENCES "
        "`grtdiff_alter_test`.`ref_t2` (`id`) ) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1;" },
      { "F+ F-", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t2` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F+ F-", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c2` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t2` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F+ F-", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c2` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F+ F+ F-", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c2` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`), CONSTRAINT `c1` FOREIGN KEY (`id`) REFERENCES "
        "`grtdiff_alter_test`.`ref_t2` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "F+ F+ F- (2)", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.ref_t1; DROP TABLE IF "
        "EXISTS "
        "grtdiff_alter_test.ref_t2;",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c2` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`), CONSTRAINT `c1` FOREIGN KEY (`id`) REFERENCES "
        "`grtdiff_alter_test`.`ref_t2` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.ref_t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.ref_t2 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`)) ENGINE=InnoDB "
        "DEFAULT CHARSET=latin1;"
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (`id`), CONSTRAINT `c1` "
        "FOREIGN "
        "KEY (`id`) REFERENCES `grtdiff_alter_test`.`ref_t1` (`id`)) ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "Rename table", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1; DROP TABLE IF EXISTS grtdiff_alter_test.t2;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t2 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "Change ENGINE attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1" },
      { "Change COMMENT attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM ENGINE=InnoDB DEFAULT "
        "CHARSET=latin1 COMMENT='some comment'" },
      { // this attribute is currently ignored during comparison
        //  "Change AUTO_INCREMENT attribute",
        //  "grtdiff_alter_test.t1",
        //  "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        //  "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT
        //  CHARSET=latin1", "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM
        //  DEFAULT CHARSET=latin1 AUTO_INCREMENT = 2"
        //}, {
        "Change DELAY_KEY_WRITE attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1 "
        "DELAY_KEY_WRITE = 1" },
      { "Change CHARACTER SET attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=utf8" },
      { "Change COLLATE attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=utf8 "
        "COLLATE=utf8_unicode_ci",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=utf8 "
        "COLLATE=utf8_bin" },
      { "Change PACK_KEYS attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "PACK_KEYS = 1" },
      { "Change CHECKSUM attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "CHECKSUM = 1" },
      { "Change ROW_FORMAT attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "ROW_FORMAT = COMPRESSED" },
      { "Change AVG_ROW_LENGTH attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "AVG_ROW_LENGTH = 101" },
      { "Change MIN_ROWS attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "MIN_ROWS = 2" },
      { "Change MAX_ROWS attribute", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "MAX_ROWS = 200" },
      { "a test for bug #35265", "grtdiff_alter_test.table1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.table2; DROP TABLE IF EXISTS grtdiff_alter_test.table1;",
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table2` ("
        " `idtable2` INT(11) NOT NULL,"
        " PRIMARY KEY (`idtable2`))"
        " ENGINE = InnoDB DEFAULT CHARSET=latin1;"
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table1` ("
        " `idtable1` INT(11) NOT NULL ,"
        " `table2_idtable2` INT(11) NULL ,"
        " PRIMARY KEY (`idtable1`) ,"
        " INDEX fk_table1_table2 (`table2_idtable2` ASC) ,"
        " CONSTRAINT `fk_table1_table2`"
        " FOREIGN KEY (`table2_idtable2` )"
        " REFERENCES `grtdiff_alter_test`.`table2` (`idtable2` )"
        " ON DELETE NO ACTION"
        " ON UPDATE NO ACTION)"
        " ENGINE = InnoDB DEFAULT CHARSET=latin1;",
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table1` ("
        " `idtable1` INT(11) NOT NULL ,"
        " PRIMARY KEY (`idtable1`) )"
        " ENGINE = InnoDB DEFAULT CHARSET=latin1;"
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table2` ("
        " `idtable2` INT(11) NOT NULL ,"
        " `table1_idtable1` INT(11) NULL ,"
        " PRIMARY KEY (`idtable2`) ,"
        " INDEX fk_table2_table1 (`table1_idtable1` ASC) ,"
        " CONSTRAINT `fk_table2_table1`"
        " FOREIGN KEY (`table1_idtable1` )"
        " REFERENCES `grtdiff_alter_test`.`table1` (`idtable1` )"
        " ON DELETE NO ACTION"
        " ON UPDATE NO ACTION)"
        " ENGINE = InnoDB DEFAULT CHARSET=latin1;" },
      { "a test for bug #36674", "grtdiff_alter_test.table1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.table2; DROP TABLE IF EXISTS grtdiff_alter_test.table1;",

        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table1` ("
        "  `idtable1` INT(11) NOT NULL ,"
        "  PRIMARY KEY (`idtable1`) )"
        "ENGINE = InnoDB DEFAULT CHARSET=latin1;"
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table2` ("
        "  `idtable2` INT(11) NOT NULL ,"
        "  `t1` INT(11) NULL ,"
        "  PRIMARY KEY (`idtable2`) )"
        "ENGINE = InnoDB DEFAULT CHARSET=latin1;",
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table1` ("
        "  `idtable1` INT(11) NOT NULL ,"
        "  PRIMARY KEY (`idtable1`) )"
        "ENGINE = InnoDB DEFAULT CHARSET=latin1;"
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`table2` ("
        "  `idtable2` INT(11) NOT NULL ,"
        "  `t1` INT(11) NULL ,"
        "  PRIMARY KEY (`idtable2`) ,"
        "  INDEX fk_table2_table1 (`t1` ASC) ,"
        "  CONSTRAINT `fk_table2_table1`"
        "    FOREIGN KEY (`t1` )"
        "    REFERENCES `grtdiff_alter_test`.`table1` (`idtable1` )"
        "    ON DELETE NO ACTION"
        "    ON UPDATE NO ACTION)"
        "ENGINE = InnoDB DEFAULT CHARSET=latin1;" },
    };

    runTestsForEntries(entries, 34);
  }

  TEST_F(GRTDiffAlterTest, TablePartitions) {
    std::vector<TestEntry> entries = {
      { "Add HASH partitioning", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY HASH (id) PARTITIONS 6" },
      { "Add KEY partitioning", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (id)) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0', PRIMARY KEY (id)) ENGINE=InnoDB "
        "DEFAULT "
        "CHARSET=latin1"
        " PARTITION BY KEY (id) PARTITIONS 6" },
      { "Drop partitioning", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY HASH (id) PARTITIONS 6",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "Partition coalescing", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY HASH (id) PARTITIONS 6",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY HASH (id) PARTITIONS 4" },
      { "Partition splitting", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY HASH (id) PARTITIONS 4",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY HASH (id) PARTITIONS 6" },
      { "Add range partitioning ", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (1991) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (1996) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (2001) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (2005) ENGINE = InnoDB,"
        " PARTITION p5 VALUES LESS THAN (MAXVALUE) ENGINE = InnoDB)" },
      { "Remove range partitioning ", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (1991) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (1996) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (2001) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (2005) ENGINE = InnoDB)",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "Range partitioning: change expression, partition LESS THAN values ", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (2) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (5) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (10) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (MAXVALUE) ENGINE = InnoDB)",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(MONTH(hired)) ("
        " PARTITION p1 VALUES LESS THAN (1991) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (1996) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (2001) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (2005) ENGINE = InnoDB)" },
      { "Range partitioning: add partition", "grtdiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (6) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (9) ENGINE = InnoDB)",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (6) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (9) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (MAXVALUE) ENGINE = InnoDB)" },
      {
        "Range partitioning: drop 2 partitions",
        "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (6) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (9) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (12) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (MAXVALUE) ENGINE = InnoDB)",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (6) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (MAXVALUE) ENGINE = InnoDB)",
      },
      {
        "Range partitioning: reorganize partitions",
        "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (6) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (9) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (12) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (13) ENGINE = InnoDB)",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY RANGE(YEAR(hired)) ("
        " PARTITION p1 VALUES LESS THAN (6) ENGINE = InnoDB,"
        " PARTITION p2 VALUES LESS THAN (9) ENGINE = InnoDB,"
        " PARTITION p3 VALUES LESS THAN (12) ENGINE = InnoDB,"
        " PARTITION p4 VALUES LESS THAN (14) ENGINE = InnoDB)",
      },
      {
        "List partitioning: reorganize partitions",
        "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY LIST(id) ("
        " PARTITION p0 VALUES IN (5,10,15),"
        " PARTITION p1 VALUES IN (6,12,18),"
        " PARTITION p2 VALUES IN (7,13,19),"
        " PARTITION p3 VALUES IN (8,14,20))",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY LIST(id) ("
        " PARTITION p0 VALUES IN (5,10,15),"
        " PARTITION p1 VALUES IN (6,12,18),"
        " PARTITION p2 VALUES IN (3,13,19),"
        " PARTITION p3 VALUES IN (8,14,20))",
      },
      {
        "List partitioning: add, remove, change partitions",
        "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY LIST(id) ("
        " PARTITION p0 VALUES IN (5,10,15),"
        " PARTITION p1 VALUES IN (6,12,18),"
        " PARTITION p2 VALUES IN (7,13,19),"
        " PARTITION p3 VALUES IN (8,14,20))",

        "CREATE TABLE grtdiff_alter_test.t1 (id int(11) NOT NULL DEFAULT '0', hired DATE NOT NULL)"
        " ENGINE=InnoDB DEFAULT CHARSET=latin1"
        " PARTITION BY LIST(id) ("
        " PARTITION p0 VALUES IN (5,10,15),"
        " PARTITION p2 VALUES IN (3,13,19),"
        " PARTITION p3 VALUES IN (8,14,20),"
        " PARTITION p4 VALUES IN (6,12,18))",
      },
    };

    runTestsForEntries(entries, 90);
  }

  TEST_F(GRTDiffAlterTest, Views) {
    std::vector<TestEntry> entries = {
      { "Create view", "grtdiff_alter_test.v1", "DROP VIEW IF EXISTS grtdiff_alter_test.v1;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;",
        "CREATE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `grtdiff_alter_test`.`v1` AS "
        "select 2 AS `2`;" },
      { "Drop view", "grtdiff_alter_test.v1",
        "DROP VIEW IF EXISTS grtdiff_alter_test.v1;DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT "
        "CHARSET=latin1;CREATE "
        "ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `grtdiff_alter_test`.`v1` AS select "
        "2 AS "
        "`2`;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT "
        "CHARSET=latin1;" },
      { "Alter view", "grtdiff_alter_test.v1", "DROP VIEW IF EXISTS grtdiff_alter_test.v1;",
        "CREATE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `grtdiff_alter_test`.`v1` AS "
        "select 1 AS `1`;",
        "CREATE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `grtdiff_alter_test`.`v1` AS "
        "select 2 AS `2`;" },
      { "Rename view", "grtdiff_alter_test.v1",
        "DROP VIEW IF EXISTS grtdiff_alter_test.v1;DROP VIEW IF EXISTS grtdiff_alter_test.v2;",
        "CREATE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `grtdiff_alter_test`.`v1` AS "
        "select 1 AS `1`;",
        "CREATE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `grtdiff_alter_test`.`v2` AS "
        "select 1 AS `1`;" },
    };

    runTestsForEntries(entries, 103);
  }

  TEST_F(GRTDiffAlterTest, Routines) {
    std::vector<TestEntry> entries = {
      { "Create procedure", "grtdiff_alter_test.p1", "DROP PROCEDURE IF EXISTS grtdiff_alter_test.p1;",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;",
        "DELIMITER //\nCREATE DEFINER=`root`@`localhost` PROCEDURE `grtdiff_alter_test`.`p1`()\nBEGIN SELECT 1; SELECT "
        "2; "
        "END//" },
      { "Drop procedure", "grtdiff_alter_test.p1", "DROP PROCEDURE IF EXISTS grtdiff_alter_test.p1;",
        "DELIMITER //\nCREATE DEFINER=`root`@`localhost` PROCEDURE `grtdiff_alter_test`.`p1`()\nBEGIN SELECT 1; SELECT "
        "2; "
        "END//",
        "CREATE DATABASE IF NOT EXISTS grtdiff_alter_test;" },
      { "Change procedure", "grtdiff_alter_test.p1", "DROP PROCEDURE IF EXISTS grtdiff_alter_test.p1;",
        "DELIMITER //\nCREATE DEFINER=`root`@`localhost` PROCEDURE `grtdiff_alter_test`.`p1`()\nBEGIN SELECT 1; SELECT "
        "2; "
        "END//",
        "DELIMITER //\nCREATE DEFINER=`root`@`localhost` PROCEDURE `grtdiff_alter_test`.`p1`()\nBEGIN SELECT 2; SELECT "
        "1; "
        "END//" },
    };

    runTestsForEntries(entries, 107);
  }

  TEST_F(GRTDiffAlterTest, Triggers) {
    std::vector<TestEntry> entries = {
      { "Create trigger", "grtdiff_alter_test.tr1",

        "DROP TRIGGER IF EXISTS grtdiff_alter_test.tr1;DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT "
        "CHARSET=latin1;\n",

        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1;\n"
        "USE grtdiff_alter_test;\n"
        "DELIMITER //\nCREATE\nDEFINER=`root`@`localhost`\nTRIGGER `grtdiff_alter_test`.`tr1`\nBEFORE INSERT ON "
        "`grtdiff_alter_test`.`t1`\nFOR EACH ROW\nBEGIN DELETE FROM grtdiff_alter_test.t1; END//" },
      { "Drop trigger", "grtdiff_alter_test.tr1",

        "DROP TRIGGER IF EXISTS grtdiff_alter_test.tr1;DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1;\n"
        "USE grtdiff_alter_test;\n"
        "DELIMITER //\nCREATE\nDEFINER=`root`@`localhost`\nTRIGGER `grtdiff_alter_test`.`tr1`\nBEFORE INSERT ON "
        "`grtdiff_alter_test`.`t1`\nFOR EACH ROW\nBEGIN DELETE FROM grtdiff_alter_test.t1; END//",

        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT "
        "CHARSET=latin1;\n" },
      { "Change trigger", "grtdiff_alter_test.tr1",

        "DROP TRIGGER IF EXISTS grtdiff_alter_test.tr1;DROP TABLE IF EXISTS grtdiff_alter_test.t1;",

        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1;\n"
        "USE grtdiff_alter_test;\n"
        "DELIMITER //\nCREATE\nDEFINER=`root`@`localhost`\nTRIGGER `grtdiff_alter_test`.`tr1`\nBEFORE INSERT ON "
        "`grtdiff_alter_test`.`t1`\nFOR EACH ROW\nBEGIN DELETE FROM t1; END//",

        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1;\n"
        "USE grtdiff_alter_test;\n"
        "DELIMITER //\nCREATE\nDEFINER=`root`@`localhost`\nTRIGGER `grtdiff_alter_test`.`tr1`\nBEFORE INSERT ON "
        "`grtdiff_alter_test`.`t1`\nFOR EACH ROW\nBEGIN DELETE FROM t2; END//" },
    };

    runTestsForEntries(entries, 110);
  }

  TEST_F(GRTDiffAlterTest, MoreComplexTableCreates) {
    std::vector<TestEntry> entries = {
      { "Create test 1", "grtdiff_alter_test.tr1", "drop table if exists grtdiff_alter_test.create_test_t1;", "",
        "CREATE TABLE grtdiff_alter_test.create_test_t1 ("
        "`id` int(11) NOT NULL DEFAULT '0') DEFAULT CHARSET=latin1 ENGINE=InnoDB;" },
      { "Create test: data types", "grtdiff_alter_test.tr1", "drop table if exists grtdiff_alter_test.types_check;", "",
        "CREATE TABLE grtdiff_alter_test.types_check ("
        "`b4` bit(1) DEFAULT NULL,  "
        "`ti` tinyint(4) DEFAULT NULL,  "
        "`si` smallint(6) DEFAULT NULL,  "
        "`mi` mediumint(9) DEFAULT NULL,  "
        "`i` int(11) DEFAULT NULL,  "
        "`i2` int(11) DEFAULT NULL,  "
        "`bi` bigint(20) DEFAULT NULL, "
        "`r` double DEFAULT NULL,  "
        "`d` double DEFAULT NULL,  "
        "`f` float DEFAULT NULL,  "
        "`dc` decimal(10,0) DEFAULT NULL,  "
        "`num` decimal(10,2) DEFAULT NULL,  "
        "`dt` date DEFAULT NULL,  "
        "`tm` time DEFAULT NULL,"
        "`tmst` timestamp NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,  "
        "`dttm` datetime DEFAULT NULL,  "
        "`yr` year(4) DEFAULT NULL,  "
        "`ch` char(32) CHARACTER SET utf8 DEFAULT NULL,"
        "`vchr` varchar(128) DEFAULT NULL, "
        "`bnr` binary(32) DEFAULT NULL,  "
        "`vbnr` varbinary(32) DEFAULT NULL,  "
        "`tblb` tinyblob,  "
        "`blb` blob,  "
        "`mblb` mediumblob,  "
        "`lblb` longblob,  "
        "`ttxt` tinytext,  "
        "`mtxt` mediumtext CHARACTER SET latin1 COLLATE latin1_bin,"
        "`enm` enum('one','two','three') DEFAULT NULL,  "
        "`st` set('on','off') DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "Create test: keys and indices", "grtdiff_alter_test.tr1",
        "drop table if exists grtdiff_alter_test.keys_check;", "",
        "CREATE  TABLE IF NOT EXISTS `grtdiff_alter_test`.`keys_check` (`id` INT(11) NOT NULL ,  `txt` VARCHAR(64) "
        "NULL "
        "DEFAULT NULL ,  `cnt` INT(11) NOT NULL AUTO_INCREMENT ,  `chk` INT(11) NOT NULL ,  PRIMARY KEY (`id`) ,  "
        "UNIQUE "
        "INDEX txt_2 (`txt` ASC) ,  INDEX cnt USING HASH (`cnt` ASC) ,  FULLTEXT INDEX txt (`txt` ASC) ) ENGINE = "
        "MyISAM "
        "DEFAULT CHARACTER SET = latin1;" },
      { "Create test: foreign keys", "grtdiff_alter_test.tr1",
        "drop table if exists grtdiff_alter_test.frn_keys_check_t2;drop table if exists "
        "grtdiff_alter_test.frn_keys_check_t1;",
        "",
        "CREATE TABLE grtdiff_alter_test.frn_keys_check_t1( `id` int(11) NOT NULL,  PRIMARY KEY (`id`) USING BTREE) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1; CREATE TABLE `grtdiff_alter_test`.`frn_keys_check_t2` (`id` int(11) NOT "
        "NULL,  KEY `fid` (`id`),  CONSTRAINT `fid` FOREIGN KEY (`id`) REFERENCES "
        "`grtdiff_alter_test`.`frn_keys_check_t1` "
        "(`id`) ON DELETE CASCADE ON UPDATE CASCADE) ENGINE=InnoDB DEFAULT CHARSET=latin1;" },
    };

    runTestsForEntries(entries, 113);
  }

  TEST_F(GRTDiffAlterTest, TableCharsets) {
    std::vector<TestEntry> entries = {
      { "Change CHARSET attribute to server default", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin2",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT" },

      { "Change COLLATE attribute to server default", "grtdiff_alter_test.t1",
        "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1 "
        "COLLATE=latin1_german1_ci",
        "CREATE TABLE grtdiff_alter_test.t1 (`id` int(11) NOT NULL DEFAULT '0') ENGINE=MyISAM DEFAULT CHARSET=latin1" },
    };

    runTestsForEntries(entries, 117);
  }

  TEST_F(GRTDiffAlterTest, ColumnInsertionWithoutDifferences) {
    std::vector<TestEntry> testEntries = {
      { "BINARY flag columns", "grtiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` varchar(20) BINARY) ENGINE=InnoDB DEFAULT CHARSET=latin1",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` varchar(20) CHARACTER SET latin1 COLLATE latin1_bin DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=latin1" },
      { "BINARY flag columns (utf8)", "grtiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` varchar(20) BINARY) ENGINE=InnoDB DEFAULT CHARSET=utf8",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` varchar(20) CHARACTER SET utf8 COLLATE utf8_bin DEFAULT NULL) "
        "ENGINE=InnoDB DEFAULT CHARSET=utf8" },
      { "ASCII flag columns", "grtiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` varchar(20) ASCII) ENGINE=InnoDB DEFAULT CHARSET=utf8",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` varchar(20) CHARACTER SET latin1 DEFAULT NULL) ENGINE=InnoDB DEFAULT "
        "CHARSET=utf8" },
      { "ZEROFILL flag columns", "grtiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` int ZEROFILL) ENGINE=InnoDB",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` int UNSIGNED ZEROFILL DEFAULT NULL) ENGINE=InnoDB" },
      { "reorder index", "grtiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` int, `b` int, index aa (a), index bb (b))",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` int, `b` int, index bb (b), index aa (a))" },
      { "enums", "grtiff_alter_test.t1", "DROP TABLE IF EXISTS grtdiff_alter_test.t1;",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` enum('a','b','c'))",
        "CREATE TABLE grtdiff_alter_test.t1 (`a` ENUM('a', 'b',    'c'  ))" }
    };

    std::shared_ptr<grt::DiffChange> empty_change;

    grt::DictRef options(true);
    for (auto &entry : testEntries) {
      db_mysql_CatalogRef org_cat = createEmptyCatalog(tester->getRdbms());
      db_mysql_CatalogRef mod_cat = createEmptyCatalog(tester->getRdbms());

      // This should yield no differences.
      empty_change = createDiff(entry, org_cat, mod_cat);

      if (empty_change) {
        grt::StringListRef alter_map(grt::Initialized);
        grt::ListRef<GrtNamedObject> alter_object_list(true);
        grt::DictRef options(true);
        options.set("UseFilteredLists", grt::IntegerRef(0));
        options.set("OutputContainer", alter_map);
        options.set("OutputObjectContainer", alter_object_list);
        options.set("CaseSensitive", grt::IntegerRef(omf.case_sensitive));

        alter_map.clear();
        alter_object_list.clear();
        diffsqlModule->generateSQL(mod_cat, options, empty_change);
        diffsqlModule->makeSQLSyncScript(mod_cat, options, alter_map, alter_object_list);

        // We cannot check for the changeset to make sure there are no changes, because some changes from the diff
        // don't cause a script to be generated (like foreign keys being reordered).
        // So, it's better to check whether there's any actual alteration.
        EXPECT_EQ(0U, alter_map.count() + alter_object_list.count())
          << "Unexpected differences found for step \"" + entry.description + "\":";
        if (alter_map.count() > 0 || alter_object_list.count() > 0) {
          empty_change->dump_log(0);
          std::string script = options.get_string("OutputScript");
          std::cout << "Output:\n" << script;
        }
      } else {
      }
      // Success;
    }
  }

} // namespace
