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

// High-level testing for SqlStudio
// This tests WBContext, which will test the integration of all components.

#include "base/util_functions.h"

#include "grtdb/db_helpers.h"
#include "grtdb/db_object_helpers.h"

#include "stub/stub_utilities.h"

#include "wb_test_helpers.h"
#include "context.h"
#include "grt_test_helpers.h"
#include "gtest/gtest.h"

using namespace wb;
using namespace base;
using namespace bec;

namespace {
//----------------------------------------------------------------------------------------------------------------------

static mforms::DialogResult messageOtherCallback() {
  return mforms::ResultOther;
}

//----------------------------------------------------------------------------------------------------------------------

static void set_note_content(GrtStoredNoteRef note, const std::string &text) {
  grt::Module *module = grt::GRT::get()->get_module("SqlStudio");
  if (!module)
    throw std::runtime_error("SqlStudio module not found");

  note->lastChangeDate(base::fmttime());

  grt::BaseListRef args(true);

  args.ginsert(note->filename());
  args.ginsert(grt::StringRef(text));

  module->call_function("setAttachedFileContents", args);
}

//----------------------------------------------------------------------------------------------------------------------

static std::string get_note_content(const GrtStoredNoteRef &note) {
  grt::Module *module = grt::GRT::get()->get_module("SqlStudio");
  if (!module)
    throw std::runtime_error("SqlStudio module not found");

  grt::BaseListRef args(true);

  args.ginsert(note->filename());

  return *grt::StringRef::cast_from(module->call_function("getAttachedFileContents", args));
}

//----------------------------------------------------------------------------------------------------------------------

struct WbContextData {
  std::unique_ptr<SqlStudioTester> tester;
  std::string dataDir = testing::Context::get().tmpDataDir();
  std::string outputDir = testing::Context::get().outputDir();
};

} // anonymous namespace

class SqlStudio_model_document_integration_testsTest : public ::testing::Test {
protected:
  static std::unique_ptr<WbContextData> data;

  static void SetUpTestSuite() {
    data = std::make_unique<WbContextData>();
    data->tester.reset(new SqlStudioTester());
    data->tester->initializeRuntime();

    // Modeling uses a default server version, which is not related to any server it might have
    // reverse engineered content from, nor where it was sync'ed to. So we have to mimic this here.
    std::string target_version = bec::GRTManager::get()->get_app_option_string("DefaultTargetMySQLVersion");
    if (target_version.empty())
      target_version = "5.5.49";
    data->tester->getRdbms()->version(parse_version(target_version));
  }

  static void TearDownTestSuite() {
    data.reset();
  }

};

std::unique_ptr<WbContextData> SqlStudio_model_document_integration_testsTest::data;

TEST_F(SqlStudio_model_document_integration_testsTest, Test_creating_new_document) {
  // Test creating a new document.
  // General note: many other tests depend on this to work, so there should really be a test
  // order where more complicated tests are based on simpler ones.
  data->tester->wb->new_document();

  EXPECT_TRUE(data->tester->wb->get_document().is_valid());
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels().count(), 1U);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Test_loading_documents) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/test_model_xml.mwb"));

  studio_SqlStudioRef root(data->tester->wb->get_root());

  EXPECT_TRUE(root->doc().is_valid());

  EXPECT_EQ(root->doc()->physicalModels().count(), 1U);
  EXPECT_EQ(root->doc()->physicalModels()[0]->diagrams().count(), 1U);

  model_DiagramRef view(root->doc()->physicalModels()[0]->diagrams()[0]);
  EXPECT_TRUE(view->figures().count() > 0);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Saving_and_loading_of_a_document) {
  data->tester->wb->new_document();
  data->tester->addView();

  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams().count(), 1U);

  data->tester->addTableFigure("sometable", 100, 100);
  data->tester->flushUntil(2); // TODO: this is not deterministic and hence should be replaced in tests.

  EXPECT_EQ(data->tester->wb->get_document()->physicalModels().count(), 1U);
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams().count(), 1U);
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams()[0]->figures().count(), 1U);
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams()[0]->rootLayer()->figures().count(), 1U);
  data->tester->syncView();
  ASSERT_TRUE(data->tester->wb->save_as(data->outputDir + "/test1_doc.mwb"));

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();

  ASSERT_TRUE(data->tester->wb->open_document(data->outputDir + "/test1_doc.mwb"));

  EXPECT_EQ(data->tester->wb->get_document()->physicalModels().count(), 1U);
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams().count(), 1U);
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams()[0]->figures().count(), 1U);
  EXPECT_EQ(data->tester->wb->get_document()->physicalModels()[0]->diagrams()[0]->rootLayer()->figures().count(), 1U);

  data->tester->openAllDiagrams();
  data->tester->syncView();

  testing::deepCompareGrtValues("save/load test", data->tester->wb->get_document(), data->tester->wb->get_document(), true);

  EXPECT_TRUE(data->tester->lastView != 0);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_opening_a_model_with_selection_will_cause_a_crash) {
  data->tester->wb->new_document();

  data->tester->addView();

  data->tester->addTableFigure("table", 10, 10);
  model_DiagramRef view(data->tester->getPmodel()->diagrams()[0]);

  EXPECT_EQ(view->selection().count(), 1U);

  ASSERT_TRUE(data->tester->wb->save_as(data->outputDir + "/test4.mwb"));
  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();

  ASSERT_TRUE(data->tester->wb->open_document(data->outputDir + "/test4.mwb"));

  view = data->tester->getPmodel()->diagrams()[0];

  EXPECT_TRUE(view->figures().count() == 1);
  EXPECT_EQ(view->selection().count(), 1U);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_dragging_related_tables_to_the_view_wont_create_the_connection) {
  // test dragging the table with fk 1st
  {
    ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/2tables_1fk.mwb"));
    EXPECT_TRUE(data->tester->wb->get_document().is_valid());
    EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
    EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

    std::list<db_DatabaseObjectRef> objects;
    objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table2"));

    data->tester->openAllDiagrams();
    data->tester->syncView();

    data->tester->interactivePlaceDbObjects(10, 10, objects);

    EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

    objects.clear();
    objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table1"));
    data->tester->interactivePlaceDbObjects(10, 150, objects);

    grt::BaseListRef figures(data->tester->getPview()->figures());
    data->tester->flushUntil(1, std::bind(&grt::BaseListRef::count, figures), 2);
    EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);

    grt::BaseListRef connections(data->tester->getPview()->connections());
    data->tester->flushUntil(5, std::bind(&grt::BaseListRef::count, connections), 1);

    EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

    EXPECT_TRUE(data->tester->closeDocument());
    data->tester->wb->close_document_finish();
  }

  // test dragging the table with fk last
  {
    ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/2tables_1fk.mwb"));
    EXPECT_TRUE(data->tester->wb->get_document().is_valid());
    EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
    EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

    data->tester->openAllDiagrams();
    data->tester->syncView();

    std::list<db_DatabaseObjectRef> objects;
    objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table1"));

    data->tester->interactivePlaceDbObjects(10, 10, objects);

    EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

    objects.clear();
    objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table2"));

    data->tester->interactivePlaceDbObjects(10, 150, objects);

    EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

    EXPECT_TRUE(data->tester->closeDocument());
    data->tester->wb->close_document_finish();
  }

  // test dragging both tables
  {
    ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/2tables_1fk.mwb"));
    EXPECT_TRUE(data->tester->wb->get_document().is_valid());
    EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
    EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

    data->tester->openAllDiagrams();
    data->tester->syncView();

    std::list<db_DatabaseObjectRef> objects;
    objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table1"));
    objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table2"));

    data->tester->interactivePlaceDbObjects(10, 150, objects);

    EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

    EXPECT_TRUE(data->tester->closeDocument());
    data->tester->wb->close_document_finish();
  }

  // test with a recursive relationship
  {
    ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/2tables_1fk.mwb"));
    EXPECT_TRUE(data->tester->wb->get_document().is_valid());
    EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
    EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

    data->tester->openAllDiagrams();
    data->tester->syncView();

    db_TableRef table(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table1"));

    EXPECT_TRUE(table->foreignKeys().count() == 0);

    bec::TableHelper::create_foreign_key_to_table(table, table, true, true, true, true, data->tester->getRdbms(),
                                                  grt::DictRef(true), grt::DictRef(true));

    std::list<db_DatabaseObjectRef> objects;
    objects.push_back(table);
    data->tester->interactivePlaceDbObjects(10, 150, objects);

    EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);
    EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

    EXPECT_TRUE(data->tester->closeDocument());
    data->tester->wb->close_document_finish();
  }
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_deleting_tables_with_relationship_wont_delete_the_connection) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/2tables_1fk.mwb"));
  EXPECT_TRUE(data->tester->wb->get_document().is_valid());
  EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);
  EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

  data->tester->openAllDiagrams();
  data->tester->syncView();

  std::list<db_DatabaseObjectRef> objects;
  objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table1"));
  objects.push_back(grt::find_named_object_in_list(data->tester->getSchema()->tables(), "table2"));

  data->tester->interactivePlaceDbObjects(10, 150, objects);

  EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);
  EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

  // Delete 1 of the tables and see if connection is gone.

  model_FigureRef figure = data->tester->getPview()->figures()[0];

  EXPECT_TRUE(data->tester->wb->get_root()->is_global());
  EXPECT_TRUE(data->tester->getPview()->is_global());

  EXPECT_TRUE(figure->is_global());

  data->tester->wb->get_model_context()->delete_object(figure);

  EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);
  EXPECT_EQ(data->tester->getPview()->connections().count(), 0U);

  // undo
  grt::GRT::get()->get_undo_manager()->undo();

  EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);
  EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Deleting_table_with_and_without_dbobject) {
  data->tester->wb->new_document();
  data->tester->addView();

  data->tester->addTableFigure("table", 10, 10);

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 1U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);

  // delete table with dbobject (1)
  data->tester->wb->get_model_context()->delete_object(data->tester->getPview()->figures()[0]);

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 0U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);

  grt::GRT::get()->get_undo_manager()->undo();

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 1U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);

  // delete table without dbobject (0)
  data->tester->wb->get_model_context()->remove_figure(data->tester->getPview()->figures()[0]);

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 1U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 0U);

  grt::GRT::get()->get_undo_manager()->undo();

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 1U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 1U);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Make_sure_connections_are_deleted_and_recreated_with_undo_redo) {
  data->tester->wb->new_document();
  data->tester->addView();

  grt::UndoManager *um = grt::GRT::get()->get_undo_manager();

  db_mysql_TableRef table1 = data->tester->addTableFigure("table1", 10, 10);
  db_mysql_ColumnRef column1(grt::Initialized);
  column1->owner(table1);
  column1->name("pk");
  db_mysql_TableRef table2 = data->tester->addTableFigure("table2", 100, 10);

  db_mysql_ColumnRef column2(grt::Initialized);
  column2->owner(table2);
  column2->name("pk");

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);

  grt::GRT::get()->start_tracking_changes();
  table1->addPrimaryKeyColumn(column1);
  table2->addPrimaryKeyColumn(column2);
  grt::GRT::get()->stop_tracking_changes();

  // create 1:n rel and test undo

  grt::AutoUndo undo;
  bec::TableHelper::create_foreign_key_to_table(table2, table1, true, true, true, true, data->tester->getRdbms(),
                                                grt::DictRef(true), grt::DictRef(true));
  undo.end("create fk");

  EXPECT_EQ(table2->foreignKeys().count(), 1U);

  grt::BaseListRef connections(data->tester->getPview()->connections());

  data->tester->flushUntil(2, std::bind(&grt::BaseListRef::count, connections), 1);

  EXPECT_EQ(connections.count(), 1U);

  um->undo();

  data->tester->flushUntil(2, std::bind(&grt::BaseListRef::count, connections), 0);

  EXPECT_EQ(connections.count(), 0U);

  um->redo();

  data->tester->flushUntil(2, std::bind(&grt::BaseListRef::count, connections), 1);

  EXPECT_EQ(connections.count(), 1U);

  um->undo();
  data->tester->flushUntil(2, std::bind(&grt::BaseListRef::count, connections), 0);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_copy_paste_object_across_schemas_are_not_updating_the_owner) {
  data->tester->wb->new_document();

  WBComponentPhysical *ph = data->tester->wb->get_component<WBComponentPhysical>();
  EXPECT_NE(ph, nullptr);

  studio_physical_ModelRef model(data->tester->getPmodel());

  ph->add_new_db_schema(model);

  EXPECT_EQ(model->catalog()->schemata().count(), 2U);

  db_SchemaRef srcschema(model->catalog()->schemata()[0]);
  db_SchemaRef tarschema(model->catalog()->schemata()[1]);

  EXPECT_NE(srcschema, tarschema);

  // add stuff

  ph->add_new_db_table(srcschema);
  ph->add_new_db_view(srcschema);
  ph->add_new_db_routine(srcschema);
  ph->add_new_db_routine_group(srcschema);

  grt::CopyContext context;
  ph->clone_db_object_to_schema(tarschema, srcschema->tables()[0], context);
  ph->clone_db_object_to_schema(tarschema, srcschema->views()[0], context);
  ph->clone_db_object_to_schema(tarschema, srcschema->routines()[0], context);
  ph->clone_db_object_to_schema(tarschema, srcschema->routineGroups()[0], context);

  EXPECT_EQ(tarschema->tables().count(), 1U);
  EXPECT_EQ(tarschema->views().count(), 1U);
  EXPECT_EQ(tarschema->routines().count(), 1U);
  EXPECT_EQ(tarschema->routineGroups().count(), 1U);

  EXPECT_EQ(tarschema->tables()[0]->owner(), tarschema);
  EXPECT_EQ(tarschema->views()[0]->owner(), tarschema);
  EXPECT_EQ(tarschema->routines()[0]->owner(), tarschema);
  EXPECT_EQ(tarschema->routineGroups()[0]->owner(), tarschema);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_deleting_an_identifying_relationship_doesnt_delete_indexes) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/identifying_relationship.mwb"));
  data->tester->openAllDiagrams();
  data->tester->syncView();

  studio_DocumentRef doc(data->tester->wb->get_document());

  db_TableRef table1(doc->physicalModels()[0]->catalog()->schemata()[0]->tables()[0]);
  db_TableRef table2(doc->physicalModels()[0]->catalog()->schemata()[0]->tables()[1]);

  EXPECT_EQ(table1->indices().count(), 2U);
  EXPECT_EQ(doc->physicalModels()[0]->diagrams()[0]->connections().count(), 1U);

  studio_physical_ConnectionRef conn =
    studio_physical_ConnectionRef::cast_from(doc->physicalModels()[0]->diagrams()[0]->connections().get(0));
  EXPECT_TRUE(conn->foreignKey().is_valid());

  mforms::stub::UtilitiesWrapper::set_message_callback(messageOtherCallback);
  data->tester->wb->get_model_context()->delete_object(doc->physicalModels()[0]->diagrams()[0]->connections()[0]);

  EXPECT_EQ(doc->physicalModels()[0]->diagrams()[0]->connections().count(), 0U);
  EXPECT_EQ(table1->indices().count(), 1U);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Delete_column_from_table) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/identifying_relationship.mwb"));
  data->tester->openAllDiagrams();
  data->tester->syncView();

  studio_DocumentRef doc(data->tester->wb->get_document());

  db_TableRef table1(doc->physicalModels()[0]->catalog()->schemata()[0]->tables()[0]);
  EXPECT_EQ(table1->indices().count(), 2U);
  EXPECT_TRUE(table1->primaryKey().is_valid());

  // Delete column.
  table1->removeColumn(table1->columns()[0]);
  EXPECT_FALSE(table1->primaryKey().is_valid());

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Relationship_handling_when_a_foreign_key_is_added) {
  data->tester->wb->new_document();
  data->tester->addView();

  db_mysql_TableRef table1 = data->tester->addTableFigure("table1", 10, 10);
  db_mysql_ColumnRef column1(grt::Initialized);
  column1->owner(table1);
  column1->name("pk");
  db_mysql_TableRef table2 = data->tester->addTableFigure("table2", 100, 10);

  db_mysql_ColumnRef column2(grt::Initialized);
  column2->owner(table2);
  column2->name("fkcol");

  EXPECT_EQ(data->tester->getSchema()->tables().count(), 2U);
  EXPECT_EQ(data->tester->getPview()->figures().count(), 2U);

  grt::GRT::get()->start_tracking_changes();
  table1->addPrimaryKeyColumn(column1);
  grt::GRT::get()->stop_tracking_changes();

  db_mysql_ForeignKeyRef fk(grt::Initialized);
  fk->owner(table2);
  fk->name("fk");

  table2->foreignKeys().insert(fk);
  data->tester->flushUntil(2);

  fk->columns().insert(column2);
  fk->referencedColumns().insert(column1);
  fk->referencedTable(table1);

  grt::BaseListRef connections(data->tester->getPview()->connections());
  data->tester->flushUntil(2, std::bind(&grt::BaseListRef::count, connections), 1);

  EXPECT_EQ(data->tester->getPview()->connections().count(), 1U);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Layer_size_of_a_new_document) {
  data->tester->wb->new_document();
  data->tester->addView();

  EXPECT_EQ(*data->tester->getPview()->rootLayer()->width(), data->tester->lastView->get_total_view_size().width);
  EXPECT_EQ(*data->tester->getPview()->rootLayer()->height(), data->tester->lastView->get_total_view_size().height);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Creation_of_a_simple_model_with_a_relationship) {
  data->tester->wb->new_document();
  data->tester->addView();

  data->tester->addTableFigure("table1", 100, 100);
  data->tester->addTableFigure("table2", 300, 100);

  data->tester->exportPNG(data->outputDir + "/test20_dump.png");

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_loading_a_model_with_layers_and_then_hitting_new_crashes) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/2tables_conn_layer.mwb"));
  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
  data->tester->wb->new_document();

  while (dynamic_cast<ModelDiagramForm *>(WBContextUI::get()->get_active_main_form()) != 0)
    data->tester->wb->flush_idle_tasks(false);

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_loading_model_twice_causes_bad_internal_state_in_GUI) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/sakila.mwb"));
  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();

  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/sakila.mwb"));
  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Bug_loading_a_model_with_selection_wont_reselect_the_items_in_the_canvas) {
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/selected_table.mwb"));
  data->tester->openAllDiagrams();
  data->tester->syncView();

  data->tester->flushWhile(3, std::bind(&mdc::Selection::empty, data->tester->lastView->get_selection()));

  EXPECT_TRUE(!data->tester->lastView->get_selection()->empty());

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Stored_note_management) {
  data->tester->wb->new_document();

  // add a note
  WBComponentPhysical *ph = data->tester->wb->get_component<WBComponentPhysical>();

  ph->add_new_stored_note(data->tester->getPmodel());

  data->tester->flushUntil(0.5);

  // check if created
  EXPECT_EQ(data->tester->getPmodel()->notes().count(), 1U);
  EXPECT_TRUE(data->tester->getPmodel()->notes().get(0)->filename() != "");

  // edit the note like an editor would
  set_note_content(data->tester->getPmodel()->notes().get(0), "hello world");
  std::string filename = data->tester->getPmodel()->notes().get(0)->filename();

  ASSERT_TRUE(data->tester->wb->save_as(data->outputDir + "/notetest.mwb"));
  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();

  // check if stored on disk
  ASSERT_TRUE(data->tester->wb->open_document(data->outputDir + "/notetest.mwb"));

  EXPECT_EQ(data->tester->getPmodel()->notes().count(), 1U);
  EXPECT_EQ(*data->tester->getPmodel()->notes().get(0)->filename(), filename);

  // get note contents as an editor
  std::string text = get_note_content(data->tester->getPmodel()->notes().get(0));
  EXPECT_EQ(text, "hello world");

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}

TEST_F(SqlStudio_model_document_integration_testsTest, Undo_for_stored_notes) {
  // create note
  data->tester->wb->new_document();

  // add a note
  WBComponentPhysical *ph = data->tester->wb->get_component<WBComponentPhysical>();

  ph->add_new_stored_note(data->tester->getPmodel());

  data->tester->flushUntil(0.5);

  // check if created
  EXPECT_EQ(data->tester->getPmodel()->notes().count(), 1U);
  EXPECT_TRUE(data->tester->getPmodel()->notes().get(0)->filename() != "");

  std::string fname = data->tester->getPmodel()->notes().get(0)->filename();

  set_note_content(data->tester->getPmodel()->notes().get(0), "some text");

  EXPECT_EQ(get_note_content(data->tester->getPmodel()->notes().get(0)), "some text");

  // delete note and undo
  grt::GRT::get()->get_undo_manager()->add_undo(new grt::UndoListRemoveAction(data->tester->getPmodel()->notes(), 0));
  data->tester->getPmodel()->notes().remove(0);
  grt::GRT::get()->get_undo_manager()->undo();

  EXPECT_EQ(data->tester->getPmodel()->notes().count(), 1U);
  EXPECT_EQ(*data->tester->getPmodel()->notes().get(0)->filename(), fname);
  EXPECT_EQ(get_note_content(data->tester->getPmodel()->notes().get(0)), "some text");

  for (int i = 0; i < 10; i++) {
    grt::GRT::get()->get_undo_manager()->redo();
    EXPECT_EQ(data->tester->getPmodel()->notes().count(), 0U);
    grt::GRT::get()->get_undo_manager()->undo();
    EXPECT_EQ(data->tester->getPmodel()->notes().count(), 1U);
  }
  EXPECT_EQ(*data->tester->getPmodel()->notes().get(0)->filename(), fname);
  EXPECT_EQ(get_note_content(data->tester->getPmodel()->notes().get(0)), "some text");

  // undo (should undo create note)

  EXPECT_TRUE(data->tester->closeDocument());
  data->tester->wb->close_document_finish();
}
