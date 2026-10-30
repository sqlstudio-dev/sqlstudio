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

#include "grt/grt_manager.h"
#include "grt.h"

#include "grts/structs.h"
#include "grts/structs.studio.h"
#include "grts/structs.db.mgmt.h"
#include "grts/structs.db.mysql.h"
#include "grts/structs.db.mgmt.h"

#include "grtdb/db_object_helpers.h"

#include "cppdbc.h"
#include "backend/db_rev_eng_be.h"

#include "db_mysql_diffsqlgen.h"

#include "diff/diffchange.h"
#include "diff/grtdiff.h"
#include "diff/changeobjects.h"
#include "diff/changelistobjects.h"
#include "grtdb/db_helpers.h"
#include "grtdb/diff_dbobjectmatch.h"

#include "myx_statement_parser.h"
#include "backend/db_mysql_sql_script_sync.h"
#include "backend/db_mysql_sql_export.h"
#include "module_db_mysql.h"

#include "module_db_mysql_shared_code.h"

#include "wb_test_helpers.h"
#include <filesystem>


/*
  We override get_model_catalog() method of the original plugin,
  to make testing setup easier, in real life the model catalog
  is taken from the GRT tree, and this is not tested here.
*/

using namespace parsers;
using namespace grt;

class DbMySQLScriptSyncTest : public DbMySQLScriptSync {
protected:
  db_mysql_CatalogRef model_catalog;
  virtual db_mysql_CatalogRef get_model_catalog() override {
    return model_catalog;
  }

public:
  void set_model_catalog(const db_mysql_CatalogRef &catalog) {
    model_catalog = catalog;
  }
  DbMySQLScriptSyncTest() : DbMySQLScriptSync() {
  }
};

namespace {
class CatalogSQLExport : public DbMySQLSQLExport {
protected:
  db_mysql_CatalogRef get_model_catalog() override { return get_catalog(); }

public:
  explicit CatalogSQLExport(db_mysql_CatalogRef catalog) : DbMySQLSQLExport(catalog) {}
};

struct DbMysqlPluginData {
  std::unique_ptr<SqlStudioTester> tester;
  std::shared_ptr<DbMySQLScriptSync> syncPlugin;
  DbObjectMatchAlterOmf omf;

  std::string dataDir;

  //--------------------------------------------------------------------------------------------------------------------

  db_mysql_CatalogRef createCatalogFromScript(const std::string &sql) {
    GrtVersionRef version = tester->getRdbms()->version();
    db_mysql_CatalogRef cat = createEmptyCatalog();
    MySQLParserServices::Ref services = MySQLParserServices::get();
    MySQLParserContext::Ref context = services->createParserContext(tester->getRdbms()->characterSets(),
                                                                    tester->getRdbms()->version(), "", false);

    DictRef options(true);
    EXPECT_EQ(services->parseSQLIntoCatalog(context, cat, sql, options), 0U) << "SQL failed to parse: " + sql;

    return cat;
  }

  //--------------------------------------------------------------------------------------------------------------------

  std::string generateScript(const std::vector<std::string> &, db_mysql_CatalogRef org_cat, db_mysql_CatalogRef mod_cat) {
    syncPlugin.reset(new DbMySQLScriptSyncTest());
    static_cast<DbMySQLScriptSyncTest *>(syncPlugin.get())->set_model_catalog(mod_cat);
    syncPlugin->init_diff_tree(std::vector<std::string>(), mod_cat, org_cat);
    return syncPlugin->generate_diff_tree_script();
  }

  //--------------------------------------------------------------------------------------------------------------------

  void applyToModel(const std::vector<std::string> &schemata, db_mysql_CatalogRef org_cat, db_mysql_CatalogRef mod_cat) {
    syncPlugin.reset(new DbMySQLScriptSyncTest());
    static_cast<DbMySQLScriptSyncTest *>(syncPlugin.get())->set_model_catalog(mod_cat);
    syncPlugin->init_diff_tree(std::vector<std::string>(), org_cat, ValueRef());
    syncPlugin->apply_changes_to_model();
  }

  //--------------------------------------------------------------------------------------------------------------------

  void applySqlToModel(const std::string &sql) {
    db_mysql_CatalogRef org_cat = createCatalogFromScript(sql);

    std::vector<std::string> schemata;
    schemata.push_back("mydb");

    db_mysql_CatalogRef mod_cat = db_mysql_CatalogRef::cast_from(tester->getCatalog());

    std::string value;

    DbMySQLScriptSyncTest p;
    p.set_model_catalog(mod_cat);
    std::shared_ptr<DiffTreeBE> tree = p.init_diff_tree(std::vector<std::string>(), mod_cat, org_cat);

    // Apply everything back to model.
    tree->set_apply_direction(tree->get_root(), DiffNode::ApplyToModel, true);
    bec::NodeId mydb_node = tree->get_child(NodeId(), 0);
    bec::NodeId table1_node = tree->get_child(mydb_node, 0);
    tree->get_field(table1_node, DiffTreeBE::ModelObjectName, value);

    p.apply_changes_to_model();
  }

}; // struct DbMysqlPluginData

} // namespace

class db_mysql_plugin_testTest : public ::testing::Test {
protected:
  static std::unique_ptr<DbMysqlPluginData> data;

  static void SetUpTestSuite() {
    data = std::make_unique<DbMysqlPluginData>();
    data->dataDir = std::filesystem::absolute("data").lexically_normal().u8string();

    data->tester.reset(new SqlStudioTester());
    data->tester->initializeRuntime();

    data->omf.dontdiff_mask = 3;

    // Modeling uses a default server version, which is not related to any server it might have
    // reverse engineered content from, nor where it was sync'ed to. So we have to mimic this here.
    std::string target_version = bec::GRTManager::get()->get_app_option_string("DefaultTargetMySQLVersion");
    if (target_version.empty())
      target_version = "8.0.16";
    data->tester->getRdbms()->version(parse_version(target_version));
  }

  static void TearDownTestSuite() {
    data->syncPlugin.reset();
    data.reset();
  }

};

std::unique_ptr<DbMysqlPluginData> db_mysql_plugin_testTest::data;

TEST_F(db_mysql_plugin_testTest, Bug_Nr32327) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1`"
    " (`idtable1` INT(11) NOT NULL , PRIMARY KEY (`idtable1`) ) ENGINE=InnoDB DEFAULT CHARSET=latin1 DEFAULT COLLATE = "
    "latin1_swedish_ci;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  db_mysql_IndexRef pk = mod_cat->schemata().get(0)->tables().get(0)->indices().get(0);
  EXPECT_NE(*pk->isPrimary(), 0);

  // Rename PK.
  pk->name("mypk");

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);

  EXPECT_EQ(script.find("DROP PRIMARY KEY"), std::string::npos) << script;
  EXPECT_EQ(script.find("ADD PRIMARY KEY"), std::string::npos) << script;
  EXPECT_EQ(script.find("mypk"), std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32330) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1` (`idtable1` INT NOT NULL PRIMARY KEY) "
    " ENGINE=InnoDB DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().count(), 1U);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  // Remove table.
  mod_cat->schemata().get(0)->tables().remove(0);

  std::string script = data->generateScript(schemata, org_cat, mod_cat);

  EXPECT_NE(script.find("DROP TABLE IF EXISTS `db_mysql_plugin_test`.`table1`"), std::string::npos) << script;
  EXPECT_EQ(script.find("CREATE TABLE"), std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32334) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1` (`idtable1` INT NOT NULL, PRIMARY KEY (`idtable1`) ) "
    "ENGINE = MyISAM CHARSET = latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table2` (`idtable1` INT NOT NULL, PRIMARY KEY (`idtable1`) ) "
    "ENGINE = MyISAM CHARSET = latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table3` (`idtable1` INT NOT NULL, PRIMARY KEY (`idtable1`) ) "
    "ENGINE = MyISAM CHARSET = latin1 DEFAULT COLLATE = latin1_swedish_ci;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().count(), 3U);

  // Set table options.
  db_mysql_TableRef table = mod_cat->schemata().get(0)->tables().get(0);
  table->avgRowLength("100");
  table->checksum(1);
  table->delayKeyWrite(1);
  table->maxRows("100");
  table->mergeInsert("LAST");
  table->mergeUnion("db_mysql_plugin_test.t2,db_mysql_plugin_test.t3");
  table->minRows("10");
  table->nextAutoInc("2");
  table->packKeys("DEFAULT");
  table->rowFormat("COMPACT");

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);

  for (const char *option : {"AVG_ROW_LENGTH = 100", "CHECKSUM = 1", "DELAY_KEY_WRITE  = 1",
                             "MAX_ROWS = 100", "MIN_ROWS = 10", "ROW_FORMAT = COMPACT",
                             "INSERT_METHOD = LAST", "UNION = (`db_mysql_plugin_test`.`t2`,`db_mysql_plugin_test`.`t3`)"})
    EXPECT_NE(script.find(option), std::string::npos) << option << "\n" << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32336) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1` (`idtable1` INT NOT NULL PRIMARY KEY) "
    " ENGINE=InnoDB DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().count(), 1U);

  // Insert an invalid column.
  db_mysql_TableRef table = mod_cat->schemata().get(0)->tables().get(0);
  db_mysql_ColumnRef column(Initialized);
  column->owner(table);
  column->name("col1");
  table->columns().insert(column);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);

  EXPECT_NE(script.find("ADD COLUMN `col1`"), std::string::npos) << script;
  MySQLParserServices::Ref services = MySQLParserServices::get();
  auto context = services->createParserContext(data->tester->getRdbms()->characterSets(),
                                               data->tester->getRdbms()->version(), "", false);
  EXPECT_GT(services->parseSQLIntoCatalog(context, createEmptyCatalog(), script, DictRef(true)), 0U)
    << "A column without a datatype must not produce valid SQL";
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32358) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1` (`idtable1` INT NOT NULL PRIMARY KEY) "
    "ENGINE=InnoDB DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table2` "
    "(`idtable2` INT NOT NULL DEFAULT 100 , `col1` VARCHAR(45) NULL , PRIMARY KEY (`idtable2`) ) "
    " ENGINE=InnoDB DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  EXPECT_EQ(mod_cat->schemata().get(0)->tables().count(), 2U);

  // Insert an self-referencing FK.
  db_mysql_TableRef table = mod_cat->schemata().get(0)->tables().get(1);
  db_mysql_ForeignKeyRef fk(Initialized);
  fk->owner(table);
  fk->name("fk1");
  fk->referencedTable(table);
  fk->columns().insert(table->columns().get(0));
  fk->columns().insert(table->columns().get(1));
  fk->referencedColumns().insert(table->columns().get(0));
  fk->referencedColumns().insert(table->columns().get(1));
  table->foreignKeys().insert(fk);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, db_mysql_CatalogRef(Initialized), mod_cat);

  EXPECT_NE(script.find("CONSTRAINT `fk1`"), std::string::npos) << script;
  EXPECT_NE(script.find("FOREIGN KEY (`idtable2` , `col1`)"), std::string::npos) << script;
  EXPECT_NE(script.find("REFERENCES `db_mysql_plugin_test`.`table2` (`idtable2` , `col1`)"),
            std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32367) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS db_mysql_plugin_test;"
    "CREATE DATABASE db_mysql_plugin_test DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;"
    "USE db_mysql_plugin_test;"
    "CREATE TABLE t1(id INT NOT NULL PRIMARY KEY AUTO_INCREMENT, col_char CHAR(1));"
    "CREATE TABLE t2(id INT NOT NULL PRIMARY KEY AUTO_INCREMENT, col_char CHAR(1));"
    "CREATE TABLE t3(id INT NOT NULL PRIMARY KEY AUTO_INCREMENT, col_char CHAR(1));\n"
    "DELIMITER //\n"
    "CREATE PROCEDURE proc1(OUT param1 INT) "
    "BEGIN "
    "SELECT COUNT(*) FROM t1; "
    "END// "
    "create DEFINER=root@localhost trigger tr1 after insert on t1 for each row begin delete from t2; end //\n"
    "DELIMITER ;\n"
    "INSERT INTO t1(col_char) VALUES ('a'), ('b'), ('c');";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().count(), 3U);
  EXPECT_EQ(mod_cat->schemata().get(0)->tables().get(0)->triggers().count(), 1U);
  EXPECT_EQ(mod_cat->schemata().get(0)->routines().count(), 1U);

  // Delete a table, a routine and a trigger.
  mod_cat->schemata().get(0)->tables().remove(2);
  mod_cat->schemata().get(0)->tables().get(0)->triggers().remove(0);
  mod_cat->schemata().get(0)->routines().remove(0);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);
  EXPECT_NE(script.find("DROP TABLE IF EXISTS `db_mysql_plugin_test`.`t3`"), std::string::npos) << script;
  EXPECT_NE(script.find("DROP procedure IF EXISTS `db_mysql_plugin_test`.`proc1`"), std::string::npos) << script;
  EXPECT_NE(script.find("DROP TRIGGER IF EXISTS `db_mysql_plugin_test`.`tr1`"), std::string::npos) << script;
  EXPECT_EQ(script.find("DROP TABLE IF EXISTS `db_mysql_plugin_test`.`t1`"), std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32371) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS db_mysql_plugin_test;\n"
    "CREATE DATABASE db_mysql_plugin_test DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;\n"
    "USE db_mysql_plugin_test;\n"
    "CREATE TABLE t1 (id INT NOT NULL PRIMARY KEY AUTO_INCREMENT, col_char CHAR(1)) ENGINE=InnoDB "
    "DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;\n"
    "DELIMITER //\n"
    "CREATE PROCEDURE proc1(OUT param1 INT) "
    "BEGIN "
    "SELECT COUNT(*) FROM t1; "
    "END//\n"
    "CREATE PROCEDURE proc2(OUT param1 INT) "
    "BEGIN "
    "SELECT COUNT(*) FROM t1; "
    "END//\n"
    "DELIMITER ;\n"
    "INSERT INTO t1(col_char) VALUES ('a'), ('b'), ('c');";

  // Part1 - check that unmodified procedure is not updated.
  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().count(), 1U);
  EXPECT_EQ(mod_cat->schemata().get(0)->routines().count(), 2U);

  std::string unchanged = data->generateScript({"db_mysql_plugin_test"}, org_cat, mod_cat);
  EXPECT_EQ(unchanged.find("CREATE PROCEDURE"), std::string::npos) << unchanged;
  EXPECT_EQ(unchanged.find("DROP procedure"), std::string::npos) << unchanged;

  // Part2 - check that delimiters for routines are generated.
  static const char *def1 =
    "CREATE PROCEDURE proc1(OUT param1 INT) "
    "BEGIN "
    "SELECT 1; "
    "END";

  static const char *def2 =
    "CREATE PROCEDURE proc2(OUT param1 INT) "
    "BEGIN "
    "SELECT 1; "
    "END";

  // Modify routines.
  mod_cat->schemata().get(0)->routines().get(0)->sqlDefinition(def1);
  mod_cat->schemata().get(0)->routines().get(1)->sqlDefinition(def2);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);
  EXPECT_NE(script.find("DELIMITER $$"), std::string::npos) << script;
  EXPECT_NE(script.find("CREATE PROCEDURE proc1"), std::string::npos) << script;
  EXPECT_NE(script.find("CREATE PROCEDURE proc2"), std::string::npos) << script;
  EXPECT_NE(script.find("END$$"), std::string::npos) << script;
  EXPECT_NE(script.find("DELIMITER ;"), std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32329) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;\n"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1` "
    "(`idtable1` INT NOT NULL , `col1` VARCHAR(45) NULL , PRIMARY KEY (`idtable1`) , INDEX idx1 (`idtable1` ASC, "
    "`col1` ASC) ) engine = MyISAM;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().get(0)->indices().get(1)->columns().count(), 2U);

  // Delete column `col1` from index idx1.
  mod_cat->schemata().get(0)->tables().get(0)->indices().get(1)->columns().remove(1);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);

  EXPECT_NE(script.find("DROP INDEX `idx1`"), std::string::npos) << script;
  EXPECT_NE(script.find("`idx1` (`idtable1` ASC)"), std::string::npos) << script;
  EXPECT_EQ(script.find("`idx1` (`idtable1` ASC, `col1` ASC)"), std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32324) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;\n"
    "CREATE  TABLE IF NOT EXISTS `db_mysql_plugin_test`.`table1` "
    "(`idtable1` INT NOT NULL , `col1` VARCHAR(45) NULL , `col2` VARCHAR(45) NULL , PRIMARY KEY (`idtable1`) ) engine "
    "= MyISAM;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);
  db_mysql_CatalogRef org_cat = copy_object(mod_cat);

  EXPECT_EQ(mod_cat->schemata().get(0)->tables().get(0)->columns().count(), 3U);

  // Move `col1` after `col2`.
  db_mysql_TableRef table = mod_cat->schemata().get(0)->tables().get(0);
  db_mysql_ColumnRef col1 = table->columns().get(1);
  table->columns().remove(1);
  table->columns().insert(col1);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  std::string script = data->generateScript(schemata, org_cat, mod_cat);

  EXPECT_NE(script.find("CHANGE COLUMN `col2` `col2` VARCHAR(45) NULL DEFAULT NULL AFTER `idtable1`"),
            std::string::npos) << script;
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr32331) {
  std::string sql1 =
    "DROP DATABASE IF EXISTS `db_mysql_plugin_test`;"
    "CREATE DATABASE IF NOT EXISTS `db_mysql_plugin_test` DEFAULT CHARSET=latin1 DEFAULT COLLATE = latin1_swedish_ci;\n"
    "CREATE VIEW `db_mysql_plugin_test`.`view2` AS SELECT * FROM `db_mysql_plugin_test`.`view1`;"
    "CREATE VIEW `db_mysql_plugin_test`.`view1` AS SELECT 1;";

  db_mysql_CatalogRef mod_cat = data->createCatalogFromScript(sql1);

  EXPECT_EQ(mod_cat->schemata().get(0)->views().count(), 2U);

  // First test export.
  CatalogSQLExport plugin(mod_cat);
  for (const char *option : {"GenerateDrops", "GenerateSchemaDrops", "GenerateWarnings", "GenerateCreateIndex",
                             "NoViewPlaceholders", "NoUsersJustPrivileges", "GenerateInserts", "NoFKForInserts",
                             "TriggersAfterInserts", "OmitSchemata", "GenerateUse", "SkipForeignKeys", "SkipFKIndexes"})
    plugin.set_option(option, false);
  bec::GrtStringListModel *models[10] = {};
  plugin.setup_grt_string_list_models_from_catalog(
    &models[0], &models[1], &models[2], &models[3], &models[4], &models[5],
    &models[6], &models[7], &models[8], &models[9]);
  plugin.set_option("ViewsAreSelected", true);
  plugin.export_task(StringRef());

  std::string script = plugin.export_sql_script();

  auto assert_view_placeholders = [](const std::string &sql) {
    size_t view1 = sql.find("VIEW `db_mysql_plugin_test`.`view1` AS");
    size_t view2 = sql.find("VIEW `db_mysql_plugin_test`.`view2` AS");
    size_t placeholder1 = sql.find("CREATE TABLE IF NOT EXISTS `db_mysql_plugin_test`.`view1`");
    size_t placeholder2 = sql.find("CREATE TABLE IF NOT EXISTS `db_mysql_plugin_test`.`view2`");
    ASSERT_NE(view1, std::string::npos) << sql;
    ASSERT_NE(view2, std::string::npos) << sql;
    ASSERT_NE(placeholder1, std::string::npos) << sql;
    ASSERT_NE(placeholder2, std::string::npos) << sql;
    EXPECT_LT(placeholder1, std::min(view1, view2)) << sql;
    EXPECT_LT(placeholder2, std::min(view1, view2)) << sql;
    EXPECT_NE(sql.find("AS SELECT * FROM `db_mysql_plugin_test`.`view1`"), std::string::npos) << sql;
  };
  assert_view_placeholders(script);

  std::vector<std::string> schemata;
  schemata.push_back("db_mysql_plugin_test");

  // Now the same test for sync.
  script.assign(data->generateScript(schemata, db_mysql_CatalogRef(Initialized), mod_cat));

  assert_view_placeholders(script);
}

TEST_F(db_mysql_plugin_testTest, Bug_Nr37634) {
  // Update model figures (and FKs) when a table is replaced
  // during the db/script to model synchronization.

  std::string sql1 =
    "CREATE SCHEMA IF NOT EXISTS `mydb` DEFAULT CHARACTER SET latin1 COLLATE latin1_swedish_ci;\n"
    "USE `mydb`;\n"
    "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL ,\n"
    "  PRIMARY KEY (`idtable1`) )\n"
    "ENGINE = InnoDB;";

  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/diff_table_replace_test.mwb"));

  db_mgmt_ManagementRef mgmt(db_mgmt_ManagementRef::cast_from(GRT::get()->get("/wb/rdbmsMgmt")));

  ListRef<db_DatatypeGroup> grouplist =
    ListRef<db_DatatypeGroup>::cast_from(GRT::get()->unserialize(data->tester->wboptions->basedir + "/data/db_datatype_groups.xml"));
  replace_contents(mgmt->datatypeGroups(), grouplist);

  db_TableRef t1 = data->tester->getCatalog()->schemata().get(0)->tables().get(0);
  EXPECT_EQ(GRT::get()->get("/wb/doc/physicalModels/0/diagrams/0/figures/0/table"),
    t1) << "before update table is referenced from figure 0";

  EXPECT_EQ(GRT::get()->get("/wb/doc/physicalModels/0/diagrams/1/figures/0/table"),
    t1) << "before update table is referenced from figure 1";

  db_mysql_CatalogRef org_cat = data->createCatalogFromScript(sql1);

  std::vector<std::string> schemata;
  schemata.push_back("mydb");

  db_mysql_CatalogRef mod_cat = db_mysql_CatalogRef::cast_from(data->tester->getCatalog());

  std::string value;

  DbMySQLScriptSyncTest p;
  p.set_model_catalog(mod_cat);
  std::shared_ptr<DiffTreeBE> tree = p.init_diff_tree(std::vector<std::string>(), org_cat, ValueRef());

  // Change apply direction for table table1.
  bec::NodeId mydb_node = tree->get_child(NodeId(), 0);
  bec::NodeId table1_node = tree->get_child(mydb_node, 0);
  tree->get_field(table1_node, DiffTreeBE::ModelObjectName, value);

  p.set_next_apply_direction(table1_node);
  p.set_next_apply_direction(table1_node);

  p.apply_changes_to_model();

  db_TableRef t2 = data->tester->getCatalog()->schemata().get(0)->tables().get(0);
  EXPECT_EQ(GRT::get()->get("/wb/doc/physicalModels/0/diagrams/0/figures/0/table"),
    t2) << "before update table is referenced from figure 0";

  EXPECT_EQ(GRT::get()->get("/wb/doc/physicalModels/0/diagrams/1/figures/0/table"),
    t2) << "before update table is referenced from figure 1";

  data->tester->wb->close_document();
  data->tester->wb->close_document_finish();
}

TEST_F(db_mysql_plugin_testTest, Column_type_change) {
  std::string sql1 =
    "CREATE SCHEMA IF NOT EXISTS `mydb` DEFAULT CHARACTER SET latin1 COLLATE latin1_swedish_ci;\n"
    "USE `mydb`;\n"
    "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` TINYINT NOT NULL ,\n"
    "  PRIMARY KEY (`idtable1`) )\n"
    "ENGINE = InnoDB;";
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/diff_table_replace_test.mwb"));
  data->applySqlToModel(sql1);

  db_TableRef t2 = data->tester->getCatalog()->schemata().get(0)->tables().get(0);
  db_ColumnRef col = t2->columns().get(0);
  db_SimpleDatatypeRef dtype = col->simpleType();
  EXPECT_EQ(*dtype->name(), "TINYINT") << "Column type not changed";

  data->tester->wb->close_document();
  data->tester->wb->close_document_finish();
}

TEST_F(db_mysql_plugin_testTest, Schema_collation_charset_change) {
  std::string sql1 = "CREATE schema IF NOT EXISTS `mydb` DEFAULT CHARACTER SET latin1 COLLATE latin1_swedish_ci;";
  ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/diff_table_replace_test.mwb"));
  data->applySqlToModel(sql1);
  EXPECT_EQ(data->tester->getCatalog()->schemata().get(0)->tables().count(), 0U);

  data->tester->wb->close_document();
  data->tester->wb->close_document_finish();
}
