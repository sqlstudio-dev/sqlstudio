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

#include "backend/db_mysql_sql_script_sync.h"
#include "backend/db_mysql_sql_export.h"
#include "module_db_mysql.h"

#include "wb_test_helpers.h"
#include "gtest/gtest.h"
#include <filesystem>

namespace {

/*
  We override get_model_catalog() method of the original plugin,
  to make testing setup easier, in real life the model catalog
  is taken from the GRT tree, and this is not tested here
*/

class DbMySQLScriptSyncTest : public DbMySQLScriptSync {
protected:
  db_mysql_CatalogRef model_catalog;
  virtual db_mysql_CatalogRef get_model_catalog() {
    return model_catalog;
  }

public:
  void set_model_catalog(const db_mysql_CatalogRef& catalog) {
    model_catalog = catalog;
  }
  DbMySQLScriptSyncTest() : DbMySQLScriptSync() {
  }
};

class DbMySQLSQLExportTest : public DbMySQLSQLExport {
protected:
  db_mysql_CatalogRef model_catalog;
  grt::DictRef options;

  virtual db_mysql_CatalogRef get_model_catalog() {
    return model_catalog;
  }
  virtual grt::DictRef get_options_as_dict() {
    return options;
  }

public:
  DbMySQLSQLExportTest(db_mysql_CatalogRef cat) : DbMySQLSQLExport(cat) {
    set_model_catalog(cat);
  }

  void set_model_catalog(db_mysql_CatalogRef catalog) {
    model_catalog = catalog;
  }
  void set_options_as_dict(grt::DictRef options_dict) {
    options = options_dict;
  }
};

struct AllObjectsMWB {
  db_SchemaRef schema;
  db_TableRef t1;
  db_TableRef t2;
  db_ViewRef view;
  db_RoutineRef routine;
  db_ForeignKeyRef FK;
  db_TriggerRef trigger;
};

static const char* create_schema = "CREATE SCHEMA IF NOT EXISTS `mydb` DEFAULT CHARACTER SET latin1 COLLATE latin1_swedish_ci ;\n"
  "USE `mydb` ;";

static const char* create_table1 = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
  "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
  "  `name` VARCHAR(45) NULL DEFAULT 'noname' ,\n"
  "  `email` VARCHAR(45) NULL ,\n"
  "  PRIMARY KEY (`idtable1`) );\n";

static const char* create_table1_trigger_delim = "DELIMITER $$\nUSE `mydb`$$";

static const char* create_table1_trigger = "CREATE\n"
  "DEFINER=`root`@`localhost`\n"
  "TRIGGER `mydb`.`tr1`\n"
  "BEFORE INSERT ON `mydb`.`table1`\n"
  "FOR EACH ROW\n"
  "set new.idtable1 = 1";

static const char* create_table1_trigger_end_delim = "$$\nDELIMITER ;\n";

static const char* create_table2 = "CREATE  TABLE IF NOT EXISTS `mydb`.`table2` (\n"
  "  `table1_idtable1` INT NULL ,\n"
  "  INDEX `fktable1_idx` (`table1_idtable1` ASC) ,\n"
  "  CONSTRAINT `fktable1`\n"
  "    FOREIGN KEY (`table1_idtable1` )\n"
  "    REFERENCES `mydb`.`table1` (`idtable1` )\n"
  "    ON DELETE NO ACTION\n"
  "    ON UPDATE NO ACTION);\n";

static const char* create_procedure_delim = "DELIMITER $$\nUSE `mydb`$$\n";

static const char* create_procedure = "CREATE DEFINER=`root`@`localhost` PROCEDURE `routine1`(OUT p INT)\r\n"
  "BEGIN\r\n"
  "select 1 into p;\r\n"
  "END";

static const char* create_procedure_end_delim = "$$\nDELIMITER ;\n";

static const char* create_view_use = "USE `mydb`;\n";

static const char* create_view = "CREATE  OR REPLACE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `mydb`.`view1` AS "
  "select 1 AS `1`";

struct AllObjectsSQL {
  std::string schema_sql;
  std::string table1_sql;
  std::string table2_sql;
  std::string trigger_sql;
  std::string procedure_sql;
  std::string view_sql;
  AllObjectsSQL()
  : schema_sql(create_schema),
  table1_sql(create_table1),
  table2_sql(create_table2),
  trigger_sql(create_table1_trigger),
  procedure_sql(create_procedure),
  view_sql(create_view) {
  }
  std::string get_sql() {
    return schema_sql.append(table1_sql)
    .append(table2_sql)
    .append(create_table1_trigger_delim)
    .append(trigger_sql)
    .append(create_table1_trigger_end_delim)
    .append(create_procedure_delim)
    .append(procedure_sql)
    .append(create_procedure_end_delim)
    .append(create_view_use)
    .append(view_sql)
    .append(";\n");
  };
};

class ValidateProperty {
  bool enabled;

public:
  ValidateProperty(const bool enabled_flag = true) : enabled(enabled_flag){};
  virtual ~ValidateProperty(){};
  void enable() {
    enabled = true;
  };
  void disable() {
    enabled = false;
  };
  virtual void validate(const AllObjectsMWB& objects) = 0;
  void operator()(const AllObjectsMWB& objects) {
    if (enabled)
      validate(objects);
  }
};

class Table1Validator : public ValidateProperty {
public:
  virtual void validate(const AllObjectsMWB& objects) {
    EXPECT_TRUE(objects.t1.is_valid()) << "t1 is invalid";
    EXPECT_EQ(objects.t1->columns().count(), 3U) << "unexpected column count for t1";
  };
};

class Table2Validator : public ValidateProperty {
public:
  virtual void validate(const AllObjectsMWB& objects) {
    EXPECT_TRUE(objects.t2.is_valid()) << "t2 is invalid";
    EXPECT_EQ(objects.t2->columns().count(), 1U) << "unexpected column count for t2";
  };
};

class ForeignKeyValidator : public ValidateProperty {
public:
  virtual void validate(const AllObjectsMWB& objects) {
    EXPECT_TRUE(objects.FK.is_valid()) << "FK is invalid";
    EXPECT_EQ(objects.FK->referencedColumns().count(), 1U) << "Wrong referenced column count";

    db_ColumnRef refcol = objects.FK->referencedColumns().get(0);
    db_ColumnRef t1col = find_named_object_in_list(objects.t1->columns(), "idtable1");
    EXPECT_EQ(refcol, t1col) << "Wrong column reference in t2 FK";
  };
};

class ViewValidator : public ValidateProperty {
public:
  virtual void validate(const AllObjectsMWB& objects) {
    EXPECT_TRUE(objects.view.is_valid()) << "View is invalid";

    std::string viewdef = objects.view->sqlDefinition();
    EXPECT_EQ(viewdef, create_view) << "View definition doesn't match";
  };
};

class RoutineValidator : public ValidateProperty {
public:
  virtual void validate(const AllObjectsMWB& objects) {
    EXPECT_TRUE(objects.routine.is_valid()) << "Routine is invalid";
    std::string sqldef = objects.routine->sqlDefinition();
    if (sqldef.find("\n\n") == 0)
      sqldef = sqldef.substr(2);

    EXPECT_EQ(sqldef, create_procedure) << "Routine definition doesn't match";
  };
};

class TriggerValidator : public ValidateProperty {
public:
  virtual void validate(const AllObjectsMWB& objects) {
    EXPECT_TRUE(objects.trigger.is_valid()) << "Trigger is invalid";
    std::string sqldef = objects.trigger->sqlDefinition();
    if (sqldef.find("\n\n") == 0)
      sqldef = sqldef.substr(2);

    EXPECT_EQ(sqldef, create_table1_trigger) << "Trigger definition doesn't match";
  };
};

struct AllObjectsMWBValidator {
  Table1Validator validate_t1;
  Table2Validator validate_t2;
  ForeignKeyValidator validate_FK;
  ViewValidator validate_view;
  RoutineValidator validate_routine;
  TriggerValidator validate_trigger;
  void validate(const AllObjectsMWB& objects) {
    validate_t1(objects);
    validate_t2(objects);
    validate_FK(objects);
    validate_view(objects);
    validate_routine(objects);
    validate_trigger(objects);
  };
};

struct ModelDiffApplyData {
  std::unique_ptr<SqlStudioTester> tester;
  std::unique_ptr<DbMySQLScriptSync> syncPlugin;
  std::unique_ptr<DbMySQLSQLExport> fwePlugin;
  SqlFacade::Ref sqlParser;
  grt::DbObjectMatchAlterOmf omf;

  std::string dataDir;

  //--------------------------------------------------------------------------------------------------------------------

  db_mysql_CatalogRef createCatalogFromScript(const std::string& sql) {
    db_mysql_CatalogRef cat= createEmptyCatalog();
    sqlParser->parseSqlScriptString(cat, sql);
    return cat;
  }

  //--------------------------------------------------------------------------------------------------------------------

  void applyToModel(const std::vector<std::string>& schemata, db_mysql_CatalogRef org_cat, db_mysql_CatalogRef mod_cat) {
    syncPlugin.reset(new DbMySQLScriptSyncTest());
    static_cast<DbMySQLScriptSyncTest*>(syncPlugin.get())->set_model_catalog(mod_cat);
    syncPlugin->init_diff_tree(std::vector<std::string>(), mod_cat, org_cat, grt::StringListRef());
    syncPlugin->apply_changes_to_model();
  }

  //--------------------------------------------------------------------------------------------------------------------

  void applySqlToModel(const std::string& sql) {
    db_mysql_CatalogRef org_cat = createCatalogFromScript(sql);

    std::vector<std::string> schemata;
    schemata.push_back("mydb");

    db_mysql_CatalogRef mod_cat = db_mysql_CatalogRef::cast_from(tester->getCatalog());

    DbMySQLSQLExportTest plugin(mod_cat);

    grt::DictRef options(true);
    options.set("UseFilteredLists", grt::IntegerRef(0));
    plugin.set_options_as_dict(options);

    std::string value;

    DbMySQLScriptSyncTest p;
    p.set_model_catalog(mod_cat);
    std::shared_ptr<DiffTreeBE> tree =
    p.init_diff_tree(std::vector<std::string>(), mod_cat, org_cat, grt::StringListRef());

    // apply everything back to model
    tree->set_apply_direction(tree->get_root(), DiffNode::ApplyToModel, true);
    bec::NodeId mydb_node = tree->get_child(NodeId(), 0);
    bec::NodeId table1_node = tree->get_child(mydb_node, 0);
    tree->get_field(table1_node, DiffTreeBE::ModelObjectName, value);

    p.apply_changes_to_model();
  }

  //--------------------------------------------------------------------------------------------------------------------

  AllObjectsMWB getModelObjects() {
    AllObjectsMWB objects;
    if (tester->getCatalog()->schemata().count() == 0)
      return objects;

    objects.schema = tester->getCatalog()->schemata().get(0);

    objects.t1 = find_named_object_in_list(objects.schema->tables(), "table1");
    objects.t2 = find_named_object_in_list(objects.schema->tables(), "table2");
    if (objects.t1.is_valid())
      objects.trigger = find_named_object_in_list(objects.t1->triggers(), "tr1");
    if (objects.t2.is_valid())
      objects.FK = find_named_object_in_list(objects.t2->foreignKeys(), "fktable1");

    objects.view = find_named_object_in_list(objects.schema->views(), "view1");

    objects.routine = find_named_object_in_list(objects.schema->routines(), "routine1");
    return objects;
  }

}; // struct ModelDiffApplyData

} // namespace

class db_mysql_pluginTest : public ::testing::Test {
protected:
  static std::unique_ptr<ModelDiffApplyData> data;

  static void SetUpTestSuite() {
    data = std::make_unique<ModelDiffApplyData>();
    data->dataDir = std::filesystem::absolute("data").lexically_normal().u8string();

    data->tester.reset(new SqlStudioTester());
    data->tester->initializeRuntime();

    data->omf.dontdiff_mask = 3;

    data->sqlParser = SqlFacade::instance_for_rdbms_name("Mysql");
    ASSERT_NE(data->sqlParser, nullptr);
  }

  static void TearDownTestSuite() {
    data.reset();
  }

  void SetUp() override {
    ASSERT_TRUE(data->tester->wb->open_document(data->dataDir + "/studio/all_objects.mwb"));
  }

  void TearDown() override {
    EXPECT_TRUE(data->tester->closeDocument());
    data->tester->wb->close_document_finish();
  }

};

std::unique_ptr<ModelDiffApplyData> db_mysql_pluginTest::data;

TEST_F(db_mysql_pluginTest, Check_unaltered_model) {
  AllObjectsSQL sql;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Drop_model) {
  data->applySqlToModel("");

  AllObjectsMWB objects = data->getModelObjects();
  EXPECT_FALSE(objects.schema.is_valid()) << "Schema wasn't dropped";
}

TEST_F(db_mysql_pluginTest, Tables_rename_column) {
  static const char* create_table1_altered = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_altered;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  EXPECT_EQ(objects.t1->columns().count(), 2U) << "remove table column";

  AllObjectsMWBValidator validator;
  validator.validate_t1.disable();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Tables_rename) {
  AllObjectsSQL sql;
  AllObjectsMWB initial_objects = data->getModelObjects();

  // rename t1
  initial_objects.t1->name("table1_renamed");

  // now t1 should get it's initial name back
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Table_rename_Plus_restore) {
  AllObjectsSQL sql;
  AllObjectsMWBValidator validator;
  AllObjectsMWB initial_objects = data->getModelObjects();
  validator.validate(initial_objects);

  // rename t2
  initial_objects.t2->name("table2_renamed");

  // now t2 should get it's initial name back
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Table_multi_rename_Plus_restore) {
  AllObjectsSQL sql;
  AllObjectsMWBValidator validator;
  AllObjectsMWB initial_objects = data->getModelObjects();
  validator.validate(initial_objects);

  // rename t1 and t2
  initial_objects.t1->name("table1_renamed");
  initial_objects.t2->name("table2_renamed");

  // now t1 and t2 should get it's initial names back
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Column_drop) {
  static const char* create_table1_altered = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_altered;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  EXPECT_EQ(objects.t1->columns().count(), 1U) << "remove table column";
}

TEST_F(db_mysql_pluginTest, Column_reorder) {
  static const char* create_table1_altered = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  `name` VARCHAR(45) NULL DEFAULT 'noname' ,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_altered;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
  EXPECT_EQ(objects.t1->columns().get(2)->name(), "name") << "Column order wasn't changed";
}

TEST_F(db_mysql_pluginTest, Table_add) {
  static const char* create_table3 = "CREATE  TABLE IF NOT EXISTS `mydb`.`table3` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  data->applySqlToModel(sql.get_sql().append(create_table3));

  AllObjectsMWB objects = data->getModelObjects();
  EXPECT_EQ(objects.schema->tables()->count(), 3U) << "add Table failed";

  AllObjectsMWBValidator validator;
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Column_add) {
  static const char* create_table1_added_col = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `name` VARCHAR(45) NULL DEFAULT 'noname' ,\n"
    "  `newcol` VARCHAR(45) NULL DEFAULT 'newcol' ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_added_col;
  AllObjectsMWB initial_objects = data->getModelObjects();
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_t1.disable();
  validator.validate(objects);
  EXPECT_EQ(objects.t1->columns()->count(), 4U) << "error adding column";
  EXPECT_EQ(objects.t1->columns().get(2)->name(), "newcol") << "Column added in wrong position";
}

TEST_F(db_mysql_pluginTest, Column_type) {
  static const char* create_table1_altered = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  `name` TINYINT,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_altered;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
  EXPECT_EQ(objects.t1->columns().get(2)->name(), "name") << "Column order wasn't changed";

  db_ColumnRef col = objects.t1->columns().get(2);
  db_SimpleDatatypeRef dtype = col->simpleType();
  EXPECT_EQ(dtype->name(), "TINYINT") << "Column type not changed";
}

TEST_F(db_mysql_pluginTest, Column_multi_add) {
  static const char* create_table1_added_col = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `name` VARCHAR(45) NULL DEFAULT 'noname' ,\n"
    "  `newcol` VARCHAR(45) NULL DEFAULT 'newcol' ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  `newcol2` VARCHAR(45) NULL DEFAULT 'newcol' ,\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_added_col;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_t1.disable();
  validator.validate(objects);

  EXPECT_EQ(objects.t1->columns()->count(), 5U) << "error adding column";
  EXPECT_EQ(objects.t1->columns().get(2)->name(), "newcol") << "column added in wrong position";
  EXPECT_EQ(objects.t1->columns().get(4)->name(), "newcol2") << "column added in wrong position";
}

TEST_F(db_mysql_pluginTest, Column_multi_add_all_possible_types) {
  static const char* create_table1_added_col = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
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
    "`st` set('on','off') DEFAULT NULL, "
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_added_col;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_t1.disable();
  validator.validate(objects);
  EXPECT_EQ(objects.t1->columns()->count(), 30U) << "error adding columns";
}

TEST_F(db_mysql_pluginTest, Foreign_key_auto_remove_column) {
  static const char* create_table1_new_PK = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1_renamed` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `name` VARCHAR(45) NULL DEFAULT 'noname' ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  PRIMARY KEY (`idtable1_renamed`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_new_PK;
  AllObjectsMWB initial_objects = data->getModelObjects();
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_FK.disable();
  validator.validate(objects);
  EXPECT_EQ(objects.t2->foreignKeys().count(), 0U) << "FK not removed after referenced col is gone";
}

TEST_F(db_mysql_pluginTest, Foreign_key_rename_Plus_restore) {
  AllObjectsSQL sql;
  AllObjectsMWBValidator validator;
  AllObjectsMWB initial_objects = data->getModelObjects();
  validator.validate(initial_objects);
  initial_objects.t2->foreignKeys().get(0)->name("fk1_newname");
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Foreign_key_remove) {
  AllObjectsSQL sql;
  AllObjectsMWB initial_objects = data->getModelObjects();
  initial_objects.t1->oldName("newname");
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_FK.disable();
  validator.validate(objects);
  EXPECT_EQ(objects.t2->foreignKeys().count(), 0U) << "FK not removed";
}

TEST_F(db_mysql_pluginTest, Table_stub) {
  AllObjectsSQL sql;
  sql.table1_sql = ""; // Remove table.
  AllObjectsMWB initial_objects = data->getModelObjects();
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Index_to_a_new_column) {
  static const char* create_table1_altered = "CREATE  TABLE IF NOT EXISTS `mydb`.`table1` (\n"
    "  `idtable1` INT NOT NULL AUTO_INCREMENT ,\n"
    "  `name` VARCHAR(45) NULL ,\n"
    "  `email` VARCHAR(45) NULL ,\n"
    "  `newcol` INT ,\n"
    "  KEY newindex (newcol),\n"
    "  PRIMARY KEY (`idtable1`) );\n";

  AllObjectsSQL sql;
  sql.table1_sql = create_table1_altered;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();

  EXPECT_EQ(objects.t1->columns().count(), 4U) << "new column not added";
  EXPECT_EQ(*objects.t1->columns()[3]->name(), "newcol") << "new column name is wrong";
  EXPECT_EQ(objects.t1->indices().count(), 2U) << "new index not added";
  EXPECT_EQ(*objects.t1->indices()[0]->name(), "newindex") << "new index name is wrong";

  // BUG #14588524 - SqlStudio SEGFAULTS WHEN UPDATING MODEL FROM A DATABASE
  EXPECT_EQ(objects.t1->indices()[0]->columns()[0]->referencedColumn().id(),
    objects.t1->columns()[3]->id()) << "refcolumn from new index";
}

TEST_F(db_mysql_pluginTest, Foreign_key_actions) {
  static const char* create_table2 = "CREATE  TABLE IF NOT EXISTS `mydb`.`table2` (\n"
    "  `table1_idtable1` INT NULL ,\n"
    "  INDEX `fktable1_idx` (`table1_idtable1` ASC) ,\n"
    "  CONSTRAINT `fktable1`\n"
    "    FOREIGN KEY (`table1_idtable1` )\n"
    "    REFERENCES `mydb`.`table1` (`idtable1` )\n"
    "    ON DELETE RESTRICT\n"
    "    ON UPDATE RESTRICT);\n";

  AllObjectsSQL sql;
  sql.table2_sql = create_table2;
  AllObjectsMWB initial_objects = data->getModelObjects();
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_FK.disable();
  validator.validate(objects);

  EXPECT_EQ(objects.t2->foreignKeys().get(0)->updateRule(), "RESTRICT") << "FK ON UPDATE wasn't changed";
  EXPECT_EQ(objects.t2->foreignKeys().get(0)->deleteRule(), "RESTRICT") << "FK ON DELETE wasn't changed";
}

TEST_F(db_mysql_pluginTest, View_replace) {
  static const char* create_renamed_view = "CREATE OR REPLACE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW "
    "`mydb`.`view1_renamed` AS select 1 AS `1`;";

  AllObjectsSQL sql;
  sql.view_sql = create_renamed_view;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_view.disable();

  EXPECT_NE(objects.schema->views().count(), 0U) << "view removed instead of replace";
  EXPECT_EQ(objects.schema->views().count(), 1U) << "view added instead of replace";
  EXPECT_FALSE(objects.view.is_valid()) << "View wasn't replaced";
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, View_alter) {
  static const char* create_altered_view = "CREATE  OR REPLACE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `mydb`.`view1` AS "
    "select 1 AS `2`";

  AllObjectsSQL sql;
  sql.view_sql = create_altered_view;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_view.disable();

  EXPECT_NE(objects.schema->views().count(), 0U) << "view removed instead of alter";
  EXPECT_EQ(objects.schema->views().count(), 1U) << "view added instead of alter";
  std::string view_sql = objects.schema->views().get(0)->sqlDefinition();
  EXPECT_EQ(view_sql, create_altered_view) << "view sql doesn't updated";

  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, View_remove) {
  static const char* dont_create = "";

  AllObjectsSQL sql;
  sql.view_sql = dont_create;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate_view.disable();
  EXPECT_EQ(objects.schema->views().count(), 0U) << "view wasn't removed";
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, View_add) {
  static const char* create_2views = "CREATE  OR REPLACE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW `mydb`.`view1` AS "
    "select 1 AS `1`;\n"
    "USE `mydb`;\nCREATE  OR REPLACE ALGORITHM=UNDEFINED DEFINER=`root`@`localhost` SQL SECURITY DEFINER VIEW "
    "`mydb`.`view2` AS select 1 AS `1`;\n";

  AllObjectsSQL sql;
  sql.view_sql = create_2views;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;

  EXPECT_EQ(objects.schema->views().count(), 2U) << "View not added";
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Procedure_replace) {
  static const char* create_renamed_procedure = "CREATE DEFINER=`root`@`localhost` PROCEDURE `routine2`(OUT p INT)\n"
    "BEGIN\n"
    "select 1 into p;\n"
    "END";

  AllObjectsSQL sql;
  sql.procedure_sql = create_renamed_procedure;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;

  EXPECT_NE(objects.schema->routines().count(), 0U) << "routine removed insead of replace";
  EXPECT_EQ(objects.schema->routines().count(), 1U) << "routine added insead of replace";
  EXPECT_FALSE(objects.routine.is_valid()) << "routine wasn't replaced";
  validator.validate_routine.disable();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Procedure_alter) {
  static const char* create_altered_procedure = "CREATE DEFINER=`root`@`localhost` PROCEDURE `routine1`(OUT p INT)\n"
    "BEGIN\n"
    "select 2 into p;\n"
    "END";

  AllObjectsSQL sql;
  sql.procedure_sql = create_altered_procedure;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;

  EXPECT_NE(objects.schema->routines().count(), 0U) << "routine removed instead of alter";
  EXPECT_EQ(objects.schema->routines().count(), 1U) << "routine added instead of alter";

  std::string proc_sql = objects.schema->routines().get(0)->sqlDefinition();
  EXPECT_EQ(proc_sql, create_altered_procedure) << "routine sql wasn't updated";

  validator.validate_routine.disable();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Trigger_replace) {
  static const char* create_table1_trigger_renamed = "CREATE\n"
    "DEFINER=`root`@`localhost`\n"
    "TRIGGER `mydb`.`tr1_renamed`\n"
    "BEFORE INSERT ON `mydb`.`table1`\n"
    "FOR EACH ROW\n"
    "set new.idtable1 = 1";

  AllObjectsSQL sql;
  sql.trigger_sql = create_table1_trigger_renamed;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  EXPECT_NE(objects.t1->triggers().count(), 0U) << "trigger removed instead of replace";
  EXPECT_EQ(objects.t1->triggers().count(), 1U) << "trigger added instead of replace";
  EXPECT_FALSE(objects.trigger.is_valid()) << "trigger wasn't replaced";
  AllObjectsMWBValidator validator;
  validator.validate_trigger.disable();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Trigger_alter) {
  static const char* create_table1_trigger_altered = "CREATE\n"
    "DEFINER=`root`@`localhost`\n"
    "TRIGGER `mydb`.`tr1`\n"
    "BEFORE INSERT ON `mydb`.`table1`\n"
    "FOR EACH ROW\n"
    "set new.idtable1 = 2";

  AllObjectsSQL sql;
  sql.trigger_sql = create_table1_trigger_altered;
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();

  EXPECT_NE(objects.t1->triggers().count(), 0U) << "trigger removed instead of alter";
  EXPECT_EQ(objects.t1->triggers().count(), 1U) << "trigger added instead of alter";
  std::string trigger_sql = objects.trigger->sqlDefinition();
  EXPECT_EQ(trigger_sql, create_table1_trigger_altered) << "trigger sql wasn't updated";

  AllObjectsMWBValidator validator;
  validator.validate_trigger.disable();
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Trigger_rename) {
  AllObjectsSQL sql;
  AllObjectsMWB initial_objects = data->getModelObjects();

  // Rename tr1.
  initial_objects.trigger->name("tr1_renamed");

  // Now tr1 should get its initial name back.
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
}

TEST_F(db_mysql_pluginTest, Trigger_rename_old_name) {
  AllObjectsSQL sql;
  AllObjectsMWB initial_objects = data->getModelObjects();

  initial_objects.trigger->oldName("tr1_renamed");
  data->applySqlToModel(sql.get_sql());

  AllObjectsMWB objects = data->getModelObjects();
  AllObjectsMWBValidator validator;
  validator.validate(objects);
}
