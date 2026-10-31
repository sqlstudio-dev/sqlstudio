/*
 * Copyright (c) 2019, 2025, Oracle and/or its affiliates. All rights reserved.
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

#include "grt/grt_manager.h"
#include "grt.h"
#include "grtdb/diff_dbobjectmatch.h"
#include "grtdb/sync_profile.h"
#include "db_mysql_public_interface.h"

#include "db_mysql_diffsqlgen.h"
#include "module_db_mysql.h"
#include "diff/changeobjects.h"
#include "diff/changelistobjects.h"

#include "model_mockup.h"
#include "../cdbc/connector_test_fixture.h"
#include "grtsqlparser/mysql_parser_services.h"

#include "gtest/gtest.h"

using namespace grt;

namespace testing {

class SyncProfileTest : public sql::test::MockConnectorFixture {
protected:
  SqlFacade::Ref sqlParser;
  DbMySQLImpl* diffsqlModule;
  DbObjectMatchAlterOmf omf;

  void SetUp() override {
    MockConnectorFixture::SetUp();
    tester->initializeRuntime();

    omf.dontdiff_mask = 3;
    diffsqlModule = GRT::get()->get_native_module<DbMySQLImpl>();
    EXPECT_NE(nullptr, diffsqlModule) << "DiffSQLGen module initialization";

    sqlParser = SqlFacade::instance_for_rdbms_name("Mysql");
    EXPECT_NE(nullptr, sqlParser) << "failed to get sqlparser module";

    tester->wb->new_document();
  }

  void TearDown() override {
    tester->wb->close_document();
    tester->wb->close_document_finish();
    MockConnectorFixture::TearDown();
  }
};

//-----------------------------------------------------------------------------------------------------

TEST_F(SyncProfileTest, ValidationOfMockupData) {
  ValueRef e;
  NormalizedComparer cmp;

  SyntheticMySQLModel model1(tester.get());
  StringRef tablename = model1.table->name();
  // unaltered model to test diffs

  const SyntheticMySQLModel model2(tester.get());

  // Save unaltered names
  db_mgmt_SyncProfileRef initial_old_names = create_sync_profile(model1.model, "test_profile", "");
  update_sync_profile_from_schema(initial_old_names, model1.catalog->schemata()[0], false);
  EXPECT_GT(initial_old_names->lastKnownDBNames().count(), 0U) << "Invalid initial old names count";

  // Rename table
  model1.table->name("new_name");

  // save updated names
  db_mgmt_SyncProfileRef updated_old_names = create_sync_profile(model1.model, "test_profile", "");
  update_sync_profile_from_schema(updated_old_names, model1.catalog->schemata()[0], false);
  EXPECT_GT(updated_old_names->lastKnownDBNames().count(), 0U) << "Invalid updated old names count";

  // Check that table rename is seen by diff module
  std::shared_ptr<DiffChange> diff = diff_make(model1.catalog, model2.catalog, &omf);
  EXPECT_NE(nullptr, diff);

  model1.table->name(tablename);
  // Now model1.table will have its initial name.
  diff = diff_make(model1.catalog, model2.catalog, &omf);
  EXPECT_EQ(nullptr, diff);

  // the only difference is oldName which should lead to drop/create of table
  update_schema_from_sync_profile(model1.catalog->schemata()[0], updated_old_names);

  diff = diff_make(model1.catalog, model2.catalog, &omf);
  EXPECT_NE(nullptr, diff);
}

//-----------------------------------------------------------------------------------------------------

TEST_F(SyncProfileTest, CreatePlusReverseEngineerMockupModel) {
  ValueRef e;
  auto mockConnection = expectConnection();
  auto connection = manager->getConnection(connectionProperties);
  auto mockStatement = sql::test::newMock<sql::test::MockStatement>();
  EXPECT_CALL(*mockConnection, createStatement()).WillOnce(sql::test::transferOwnership(mockStatement));
  std::unique_ptr<sql::Statement> stmt(connection->createStatement());
  NormalizedComparer cmp;

  // Kind of a hack. At the moment we don't properly handle server representations of procedures and views.
  cmp.add_comparison_rule("sqlDefinition", std::bind([]() { return true; }));

  SyntheticMySQLModel model(tester.get());
  model.trigger->modelOnly(1);
  model.view->modelOnly(1);

  db_mysql_CatalogRef catalog = model.catalog;

  cmp.init_omf(&omf);

  std::shared_ptr<DiffChange> create_change = diff_make(e, catalog, &omf);
  std::shared_ptr<DiffChange> drop_change = diff_make(catalog, e, &omf);

  DictRef create_map(true);
  DictRef drop_map(true);
  DictRef options(true);
  options.set("UseFilteredLists", IntegerRef(0));
  options.set("OutputContainer", create_map);
  options.set("CaseSensitive", IntegerRef(omf.case_sensitive));
  options.set("GenerateSchemaDrops", IntegerRef(1));
  diffsqlModule->generateSQL(catalog, options, create_change);

  options.set("OutputContainer", drop_map);
  diffsqlModule->generateSQL(catalog, options, drop_change);

  diffsqlModule->makeSQLExportScript(catalog, options, create_map, drop_map);
  std::string export_sql_script = options.get_string("OutputScript");
  ASSERT_FALSE(export_sql_script.empty());
  auto services = parsers::MySQLParserServices::get();
  std::vector<parsers::StatementRange> ranges;
  services->determineStatementRanges(export_sql_script.c_str(), export_sql_script.size(), ";", ranges);
  ASSERT_FALSE(ranges.empty());
  {
    InSequence sequence;
    for (const auto &range : ranges) {
      const std::string query(export_sql_script, range.start, range.length);
      EXPECT_CALL(*mockStatement, execute(sql::SQLString(query))).WillOnce(Return(false));
    }
  }
  tester->executeScript(stmt.get(), export_sql_script);

  // Reconstruct the catalog from generated DDL locally. Connector execution is
  // mocked; this checks export/import behavior, not live server reverse engineering.
  db_mysql_CatalogRef cat1(grt::Initialized);
  cat1->name(catalog->name());
  cat1->version(catalog->version());
  grt::replace_contents(cat1->simpleDatatypes(), model.rdbms->simpleDatatypes());
  grt::replace_contents(cat1->characterSets(), model.rdbms->characterSets());
  auto context = services->createParserContext(model.rdbms->characterSets(), catalog->version(), "", false);
  DictRef parseOptions(true);
  ASSERT_EQ(0U, services->parseSQLIntoCatalog(context, cat1, export_sql_script, parseOptions));
  ASSERT_EQ(1U, cat1->schemata().count());
  auto importedSchema = cat1->schemata().get(0);
  EXPECT_EQ(model.schema->name(), importedSchema->name());
  ASSERT_EQ(1U, importedSchema->tables().count());
  auto importedTable = importedSchema->tables().get(0);
  EXPECT_EQ(model.table->name(), importedTable->name());
  ASSERT_EQ(model.table->columns().count(), importedTable->columns().count());
  for (size_t i = 0; i < model.table->columns().count(); ++i) {
    EXPECT_EQ(model.table->columns().get(i)->name(), importedTable->columns().get(i)->name());
    ASSERT_TRUE(importedTable->columns().get(i)->simpleType().is_valid());
    EXPECT_EQ(model.table->columns().get(i)->simpleType()->name(),
              importedTable->columns().get(i)->simpleType()->name());
  }
  EXPECT_EQ(0U, importedSchema->views().count());
  EXPECT_EQ(0U, importedTable->triggers().count());
  ASSERT_EQ(model.schema->routines().count(), importedSchema->routines().count());
  EXPECT_EQ(model.routine->name(), importedSchema->routines().get(0)->name());

  db_mysql_CatalogRef cat2 = copy_object(cat1);

  // Diff identical catalogs, no changes expected.
  std::shared_ptr<DiffChange> diff = diff_make(cat1, cat2, &omf);
  EXPECT_EQ(nullptr, diff);
}

//-----------------------------------------------------------------------------------------------------

}


