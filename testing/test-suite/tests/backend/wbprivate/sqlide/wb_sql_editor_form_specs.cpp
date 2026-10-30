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
 * documentation. The authors of MySQL hereby grant you an additional
 * permission to link the program and your derivative works with the
 * separately licensed software that they have either included with
 * the program or referenced in the documentation.
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License, version 2.0, for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "stub/stub_mforms.h"
#include "grt/grt_manager.h"
#include "sqlide/wb_live_schema_tree.h"
#include "sqlide/wb_sql_editor_tree_controller.h"
#include "cppconn/exception.h"
#include <gtest/gtest.h>
#include <deque>
#include <optional>

class LocalEditorFormTester {
public:
  static std::shared_ptr<SqlEditorTreeController> create(
    SqlEditorTreeController::MetadataSessionFactory session_factory) {
    return SqlEditorTreeController::create(base::MySQLVersion::MySQL80, std::move(session_factory), nullptr);
  }

  static void fetch_schema_contents(const std::shared_ptr<SqlEditorTreeController> &controller,
                                   const wb::LiveSchemaTree::NewSchemaContentArrivedSlot &callback) {
    controller->do_fetch_live_schema_contents(controller, "wb_sql_editor_form_test", callback);
  }
};

namespace {
  using Tree = wb::LiveSchemaTree;
  using Cell = std::optional<std::string>;
  using Rows = std::vector<std::vector<Cell>>;

  class MetadataResult : public SqlEditorTreeController::MetadataResult {
    Rows _rows;
    const int &_active_sessions;
    size_t _position = 0;

    const Cell &cell(unsigned int column) const {
      if (_position == 0 || _position > _rows.size() || column == 0)
        throw std::out_of_range("Metadata cursor is not positioned on a valid cell");
      return _rows.at(_position - 1).at(column - 1);
    }

  public:
    MetadataResult(Rows rows, const int &active_sessions)
      : _rows(std::move(rows)), _active_sessions(active_sessions) {
    }

    ~MetadataResult() override {
      EXPECT_GT(_active_sessions, 0) << "Metadata session was destroyed before its result";
    }

    bool next() override { return ++_position <= _rows.size(); }
    std::string getString(unsigned int column) override { return cell(column).value_or(""); }
    int getInt(unsigned int column) override { return std::stoi(cell(column).value()); }
    bool isNull(unsigned int column) override { return !cell(column).has_value(); }
  };

  struct ExpectedQuery {
    std::string sql;
    Rows rows;
    int error_code = 0;
  };

  struct MetadataFixture {
    std::deque<ExpectedQuery> queries;
    bool supports_visibility = true;
    int active_sessions = 0;
    int opened_sessions = 0;
  };

  class MetadataSession : public SqlEditorTreeController::MetadataSession {
    MetadataFixture &_fixture;

  public:
    explicit MetadataSession(MetadataFixture &fixture) : _fixture(fixture) {
      ++_fixture.active_sessions;
      ++_fixture.opened_sessions;
    }

    ~MetadataSession() override { --_fixture.active_sessions; }

    std::unique_ptr<SqlEditorTreeController::MetadataResult> schemata() override {
      return query("<schemata>");
    }

    std::unique_ptr<SqlEditorTreeController::MetadataResult> query(const std::string &sql) override {
      if (_fixture.queries.empty()) {
        ADD_FAILURE() << "Unexpected metadata query: " << sql;
        throw std::logic_error("Unexpected metadata query");
      }
      auto expected = std::move(_fixture.queries.front());
      _fixture.queries.pop_front();
      EXPECT_EQ(sql, expected.sql);
      if (sql != expected.sql)
        throw std::logic_error("Metadata query did not match the expected SQL");
      if (expected.error_code != 0)
        throw sql::SQLException("Test metadata query failed", "HY000", expected.error_code);
      return std::make_unique<MetadataResult>(std::move(expected.rows), _fixture.active_sessions);
    }

    bool supports_index_visibility() const override { return _fixture.supports_visibility; }
  };

  class SQL_Editor_FormTest : public ::testing::Test {
  protected:
    MetadataFixture metadata;
    std::unique_ptr<mforms::TreeView> view;
    std::shared_ptr<SqlEditorTreeController> controller;

    void SetUp() override {
      // GRT installs the notification center used by the controller's observers.
      grt::GRT::get();
      mforms::stub::init(nullptr);
      view = std::make_unique<mforms::TreeView>(
        mforms::TreeNoColumns | mforms::TreeNoBorder | mforms::TreeSidebar | mforms::TreeNoHeader);
      controller = LocalEditorFormTester::create([this]() {
        return std::make_unique<MetadataSession>(metadata);
      });
      tree()->set_model_view(view.get());
      tree()->update_schemata(std::make_shared<std::list<std::string>>(
        std::initializer_list<std::string>{"wb_sql_editor_form_test"}));
    }

    void TearDown() override {
      EXPECT_TRUE(metadata.queries.empty()) << "Expected metadata queries were not executed";
      EXPECT_EQ(metadata.active_sessions, 0);
      controller.reset();
      view.reset();
    }

    Tree *tree() { return controller->get_schema_tree(); }

    Tree::NodeChildrenUpdaterSlot updater() {
      return [this](mforms::TreeNodeRef parent, base::StringListPtr names, Tree::ObjectType type,
                    bool sorted, bool append) {
        return tree()->update_node_children(parent, names, type, sorted, append);
      };
    }

    mforms::TreeNodeRef table(const std::string &name) {
      return tree()->create_node_for_object("wb_sql_editor_form_test", Tree::Table, name);
    }

    std::vector<std::string> captions(mforms::TreeNodeRef parent) {
      std::vector<std::string> result;
      for (int i = 0; i < parent->count(); ++i)
        result.push_back(parent->get_child(i)->get_string(0));
      return result;
    }

    void expect_indexes() {
      metadata.queries.push_back({"SHOW INDEXES FROM `wb_sql_editor_form_test`.`film`", {
        {"film", "0", "PRIMARY", "1", "film_id", "", "", "", "", "", "BTREE", "", "", "YES"},
        {"film", "1", "idx_title", "1", "title", "", "", "", "", "", "BTREE", "", "", "NO"},
        {"film", "1", "idx_language", "1", "language_id", "", "", "", "", "", "BTREE", "", "", "YES"},
        {"film", "1", "idx_language", "2", "original_language_id", "", "", "", "", "", "BTREE", "", "", "YES"}
      }});
    }
  };

  TEST_F(SQL_Editor_FormTest, Testing_fetch_schema_list) {
    metadata.queries.push_back({"<schemata>", {
      {"information_schema"}, {"performance_schema"}, {"mysql"}, {".hidden"}, {"wb_sql_editor_form_test"}
    }});
    const auto actual = static_cast<Tree::FetchDelegate &>(*controller).fetch_schema_list();
    EXPECT_EQ(actual, (std::vector<std::string>{"wb_sql_editor_form_test"}));
    EXPECT_EQ(metadata.opened_sessions, 1);
  }

  TEST_F(SQL_Editor_FormTest, Loads_the_schema_list) {
    metadata.queries.push_back({"SHOW FULL TABLES FROM `wb_sql_editor_form_test`", {
      {"language", "BASE TABLE"}, {"film", "BASE TABLE"}, {"film_text", "BASE TABLE"},
      {"dummy_table", "BASE TABLE"}, {"complex_pk_table", "BASE TABLE"}, {"no_pk_table", "BASE TABLE"},
      {"pk_table_unique_not_null", "BASE TABLE"}, {"no_pk_table_unique_not_null", "BASE TABLE"},
      {"dummy_film_view", "VIEW"}
    }});
    metadata.queries.push_back({"SHOW PROCEDURE STATUS WHERE Db='wb_sql_editor_form_test'", {
      {"wb_sql_editor_form_test", "get_films"}
    }});
    metadata.queries.push_back({"SHOW FUNCTION STATUS WHERE Db='wb_sql_editor_form_test'", {
      {"wb_sql_editor_form_test", "dummy_function"}, {"wb_sql_editor_form_test", "other_function"}
    }});
    int callbacks = 0;
    // Run the real worker synchronously, then drain its idle callback instead of sleeping.
    LocalEditorFormTester::fetch_schema_contents(controller,
      [&](const std::string &schema, base::StringListPtr tables, base::StringListPtr views,
          base::StringListPtr procedures, base::StringListPtr functions, bool append) {
        ++callbacks;
        EXPECT_EQ(schema, "wb_sql_editor_form_test");
        EXPECT_FALSE(append);
        EXPECT_EQ(*tables, (std::list<std::string>{"language", "film", "film_text", "dummy_table",
          "complex_pk_table", "no_pk_table", "pk_table_unique_not_null", "no_pk_table_unique_not_null"}));
        EXPECT_EQ(*views, (std::list<std::string>{"dummy_film_view"}));
        EXPECT_EQ(*procedures, (std::list<std::string>{"get_films"}));
        EXPECT_EQ(*functions, (std::list<std::string>{"dummy_function", "other_function"}));
      });
    EXPECT_EQ(callbacks, 0);
    bec::GRTManager::get()->perform_idle_tasks();
    EXPECT_EQ(callbacks, 1);
    EXPECT_EQ(metadata.opened_sessions, 1);
  }

  TEST_F(SQL_Editor_FormTest, Testing_for_SqlEditorForm_fetch_column_data) {
    auto node = table("language");
    metadata.queries.push_back({"SHOW FULL COLUMNS FROM `wb_sql_editor_form_test`.`language`", {
      {"language_id", "smallint unsigned", std::nullopt, "NO", "PRI", "", "auto_increment"},
      {"name", "char(20)", "utf8_general_ci", "NO", "UNI", "", ""},
      {"last_update", "timestamp", std::nullopt, "YES", "", "CURRENT_TIMESTAMP", ""}
    }});
    controller->fetch_column_data("wb_sql_editor_form_test", "language", Tree::Table, updater());
    auto columns = node->get_child(Tree::TABLE_COLUMNS_NODE_INDEX);
    EXPECT_EQ(captions(columns), (std::vector<std::string>{"language_id", "name", "last_update"}));
    auto data = dynamic_cast<Tree::TableData *>(node->get_data());
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->is_data_loaded(Tree::COLUMN_DATA));
    ASSERT_EQ(columns->count(), 3);
    auto id = dynamic_cast<Tree::ColumnData *>(columns->get_child(0)->get_data());
    ASSERT_NE(id, nullptr);
    EXPECT_TRUE(id->is_pk);
    EXPECT_TRUE(id->is_id);
    EXPECT_TRUE(id->is_idx);
    EXPECT_FALSE(id->is_fk);
    EXPECT_EQ(id->type, "smallint UN AI");
    EXPECT_TRUE(id->charset_collation.empty());
    auto name = dynamic_cast<Tree::ColumnData *>(columns->get_child(1)->get_data());
    ASSERT_NE(name, nullptr);
    EXPECT_FALSE(name->is_pk);
    EXPECT_TRUE(name->is_id);
    EXPECT_EQ(name->charset_collation, "utf8_general_ci");
    auto timestamp = dynamic_cast<Tree::ColumnData *>(columns->get_child(2)->get_data());
    ASSERT_NE(timestamp, nullptr);
    EXPECT_FALSE(timestamp->is_id);
    EXPECT_FALSE(timestamp->is_idx);
    EXPECT_EQ(timestamp->default_value, "CURRENT_TIMESTAMP");
  }

  TEST_F(SQL_Editor_FormTest, Testing_for_SqlEditorForm_fetch_index_data) {
    auto node = table("film");
    for (bool visibility_supported : {true, false}) {
      metadata.supports_visibility = visibility_supported;
      expect_indexes();
      controller->fetch_index_data("wb_sql_editor_form_test", "film", Tree::Table, updater());
      auto indexes = node->get_child(Tree::TABLE_INDEXES_NODE_INDEX);
      EXPECT_EQ(captions(indexes), (std::vector<std::string>{"PRIMARY", "idx_title", "idx_language"}));
      ASSERT_EQ(indexes->count(), 3);
      auto primary = dynamic_cast<Tree::IndexData *>(indexes->get_child(0)->get_data());
      ASSERT_NE(primary, nullptr);
      EXPECT_TRUE(primary->unique);
      EXPECT_EQ(primary->type, Tree::internalize_token("BTREE"));
      auto title = dynamic_cast<Tree::IndexData *>(indexes->get_child(1)->get_data());
      ASSERT_NE(title, nullptr);
      EXPECT_FALSE(title->unique);
      EXPECT_EQ(title->visible, !visibility_supported);
      auto language = dynamic_cast<Tree::IndexData *>(indexes->get_child(2)->get_data());
      ASSERT_NE(language, nullptr);
      EXPECT_EQ(language->columns, (std::vector<std::string>{"language_id", "original_language_id"}));
      auto data = dynamic_cast<Tree::TableData *>(node->get_data());
      ASSERT_NE(data, nullptr);
      EXPECT_TRUE(data->is_data_loaded(Tree::INDEX_DATA));
    }
  }

  TEST_F(SQL_Editor_FormTest, Testing_for_SqlEditorForm_fetch_trigger_data) {
    auto node = table("film");
    metadata.queries.push_back({"SHOW TRIGGERS FROM `wb_sql_editor_form_test` LIKE 'film'", {
      {"ins_film", "INSERT", "film", "", "AFTER"},
      {"upd_film", "UPDATE", "film", "", "AFTER"},
      {"del_film", "DELETE", "film", "", "BEFORE"}
    }});
    controller->fetch_trigger_data("wb_sql_editor_form_test", "film", Tree::Table, updater());
    auto triggers = node->get_child(Tree::TABLE_TRIGGERS_NODE_INDEX);
    EXPECT_EQ(captions(triggers), (std::vector<std::string>{"ins_film", "upd_film", "del_film"}));
    ASSERT_EQ(triggers->count(), 3);
    const std::vector<std::string> events{"INSERT", "UPDATE", "DELETE"};
    for (int i = 0; i < triggers->count(); ++i) {
      auto data = dynamic_cast<Tree::TriggerData *>(triggers->get_child(i)->get_data());
      ASSERT_NE(data, nullptr);
      EXPECT_EQ(data->event_manipulation, Tree::internalize_token(events[i]));
      EXPECT_EQ(data->timing, Tree::internalize_token(i == 2 ? "BEFORE" : "AFTER"));
    }
    auto data = dynamic_cast<Tree::TableData *>(node->get_data());
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->is_data_loaded(Tree::TRIGGER_DATA));
  }

  TEST_F(SQL_Editor_FormTest, Testing_for_SqlEditorForm_fetch_foreign_key_data) {
    auto node = table("film");
    metadata.queries.push_back({"SHOW CREATE TABLE `wb_sql_editor_form_test`.`film`", {
      {"film",
       "CREATE TABLE `film` (\n"
       "  `film_id` int NOT NULL,\n"
       "  CONSTRAINT `fk_film_language` FOREIGN KEY (`language_id`) REFERENCES `language` (`language_id`) "
       "ON DELETE RESTRICT ON UPDATE CASCADE,\n"
       "  CONSTRAINT `fk_film_language_original` FOREIGN KEY (`original_language_id`) "
       "REFERENCES `language` (`language_id`) ON DELETE SET NULL ON UPDATE NO ACTION\n"
       ") ENGINE=InnoDB"}
    }});
    controller->fetch_foreign_key_data("wb_sql_editor_form_test", "film", Tree::Table, updater());
    auto keys = node->get_child(Tree::TABLE_FOREIGN_KEYS_NODE_INDEX);
    EXPECT_EQ(captions(keys), (std::vector<std::string>{"fk_film_language", "fk_film_language_original"}));
    ASSERT_EQ(keys->count(), 2);
    auto first = dynamic_cast<Tree::FKData *>(keys->get_child(0)->get_data());
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->update_rule, Tree::internalize_token("CASCADE"));
    EXPECT_EQ(first->delete_rule, Tree::internalize_token("RESTRICT"));
    EXPECT_EQ(first->referenced_table, "language");
    EXPECT_EQ(first->from_cols, "language_id");
    EXPECT_EQ(first->to_cols, "language_id");
    auto second = dynamic_cast<Tree::FKData *>(keys->get_child(1)->get_data());
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->update_rule, Tree::internalize_token("NO ACTION"));
    EXPECT_EQ(second->delete_rule, Tree::internalize_token("SET NULL"));
    EXPECT_EQ(second->from_cols, "original_language_id");
    auto data = dynamic_cast<Tree::TableData *>(node->get_data());
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->is_data_loaded(Tree::FK_DATA));
  }

  TEST_F(SQL_Editor_FormTest, Broken_view_records_column_load_error) {
    auto node = tree()->create_node_for_object("wb_sql_editor_form_test", Tree::View, "broken_view");
    metadata.queries.push_back({"SHOW FULL COLUMNS FROM `wb_sql_editor_form_test`.`broken_view`", {}, 1356});
    controller->fetch_column_data("wb_sql_editor_form_test", "broken_view", Tree::View, updater());
    auto data = dynamic_cast<Tree::ViewData *>(node->get_data());
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->columns_load_error);
    EXPECT_FALSE(data->is_data_loaded(Tree::COLUMN_DATA));
    EXPECT_EQ(data->details, "Test metadata query failed");
    EXPECT_EQ(metadata.active_sessions, 0);
  }

  TEST_F(SQL_Editor_FormTest, View_columns_use_the_view_as_parent) {
    auto node = tree()->create_node_for_object("wb_sql_editor_form_test", Tree::View, "film_view");
    metadata.queries.push_back({"SHOW FULL COLUMNS FROM `wb_sql_editor_form_test`.`film_view`", {
      {"title", "varchar(255)", "utf8_general_ci", "YES", "", "", ""}
    }});
    controller->fetch_column_data("wb_sql_editor_form_test", "film_view", Tree::View, updater());
    ASSERT_EQ(node->count(), 1);
    EXPECT_EQ(node->get_child(0)->get_string(0), "title");
    auto column = dynamic_cast<Tree::ColumnData *>(node->get_child(0)->get_data());
    ASSERT_NE(column, nullptr);
    EXPECT_EQ(column->get_type(), Tree::ViewColumn);
    auto data = dynamic_cast<Tree::ViewData *>(node->get_data());
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->is_data_loaded(Tree::COLUMN_DATA));
    EXPECT_FALSE(data->columns_load_error);
  }

  TEST_F(SQL_Editor_FormTest, Empty_metadata_marks_collections_loaded) {
    auto node = table("film");
    metadata.queries.push_back({"SHOW INDEXES FROM `wb_sql_editor_form_test`.`film`", {}});
    metadata.queries.push_back({"SHOW TRIGGERS FROM `wb_sql_editor_form_test` LIKE 'film'", {}});
    metadata.queries.push_back({"SHOW CREATE TABLE `wb_sql_editor_form_test`.`film`", {
      {"film", "CREATE TABLE `film` (\n  `film_id` int NOT NULL\n) ENGINE=InnoDB"}
    }});
    controller->fetch_index_data("wb_sql_editor_form_test", "film", Tree::Table, updater());
    controller->fetch_trigger_data("wb_sql_editor_form_test", "film", Tree::Table, updater());
    controller->fetch_foreign_key_data("wb_sql_editor_form_test", "film", Tree::Table, updater());
    EXPECT_EQ(node->get_child(Tree::TABLE_INDEXES_NODE_INDEX)->count(), 0);
    EXPECT_EQ(node->get_child(Tree::TABLE_TRIGGERS_NODE_INDEX)->count(), 0);
    EXPECT_EQ(node->get_child(Tree::TABLE_FOREIGN_KEYS_NODE_INDEX)->count(), 0);
    auto data = dynamic_cast<Tree::TableData *>(node->get_data());
    ASSERT_NE(data, nullptr);
    EXPECT_TRUE(data->is_data_loaded(Tree::INDEX_DATA | Tree::TRIGGER_DATA | Tree::FK_DATA));
    EXPECT_EQ(metadata.opened_sessions, 3);
  }
}
