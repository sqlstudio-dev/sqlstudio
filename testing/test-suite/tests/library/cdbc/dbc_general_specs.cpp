/*
 * Copyright (c) 2012, 2020, Oracle and/or its affiliates.
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

#include "connector_test_fixture.h"
#include "grtpp_util.h"
#include "wb_connection_helpers.h"

#include <cppconn/exception.h>

#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {
using namespace testing;
using namespace sql::test;

class DbcGeneralTest : public MockConnectorFixture {};

TEST_F(DbcGeneralTest, ChecksInitialFunctionality) {
  EXPECT_EQ(static_cast<size_t>(INT_METACLASS_COUNT), grt::GRT::get()->get_metaclasses().size());
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));

  auto before = newMock<MockResultSet>();
  auto after = newMock<MockResultSet>();
  const sql::SQLString lookup("SHOW DATABASES LIKE 'sqlstudio_dbc_general'");
  {
    InSequence sequence;
    EXPECT_CALL(*statement, executeQuery(lookup)).WillOnce(transferOwnership(before));
    EXPECT_CALL(*before, rowsCount()).WillOnce(Return(0));
    EXPECT_CALL(*statement, execute(sql::SQLString("CREATE SCHEMA sqlstudio_dbc_general")))
      .WillOnce(Return(false));
    EXPECT_CALL(*statement, executeQuery(lookup)).WillOnce(transferOwnership(after));
    EXPECT_CALL(*after, rowsCount()).WillOnce(Return(1));
  }

  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  {
    std::unique_ptr<sql::ResultSet> result(owned->executeQuery(lookup));
    EXPECT_EQ(0U, result->rowsCount());
  }
  owned->execute("CREATE SCHEMA sqlstudio_dbc_general");
  {
    std::unique_ptr<sql::ResultSet> result(owned->executeQuery(lookup));
    EXPECT_EQ(1U, result->rowsCount());
  }
}

TEST_F(DbcGeneralTest, MetadataFetchTest) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  StrictMock<MockDatabaseMetaData> metadata;
  EXPECT_CALL(metadata, destroyed());
  auto schemata = newMock<MockResultSet>();
  auto objects = newMock<MockResultSet>();
  {
    InSequence sequence;
    EXPECT_CALL(*connection, getMetaData()).WillOnce(Return(&metadata));
    EXPECT_CALL(metadata, getSchemata(sql::SQLString(""))).WillOnce(transferOwnership(schemata));
    EXPECT_CALL(*schemata, next()).WillOnce(Return(true));
    EXPECT_CALL(*schemata, getString(sql::SQLString("Database")))
      .WillOnce(Return(sql::SQLString("sqlstudio_dbc_general")));
    EXPECT_CALL(metadata, getSchemaObjects(sql::SQLString(""), sql::SQLString("sqlstudio_dbc_general"),
                                           sql::SQLString(""), true, sql::SQLString(""), sql::SQLString("")))
      .WillOnce(transferOwnership(objects));
    EXPECT_CALL(*objects, next()).WillOnce(Return(true));
    EXPECT_CALL(*objects, getString(sql::SQLString("OBJECT_TYPE"))).WillOnce(Return(sql::SQLString("table")));
    EXPECT_CALL(*objects, getString(sql::SQLString("NAME"))).WillOnce(Return(sql::SQLString("product")));
    EXPECT_CALL(*objects, getString(sql::SQLString("DDL")))
      .WillOnce(Return(sql::SQLString("CREATE TABLE product (id INT)")));
    EXPECT_CALL(*objects, next()).WillOnce(Return(false));
    EXPECT_CALL(*schemata, next()).WillOnce(Return(false));
  }

  auto meta = wrapper->getMetaData();
  std::unique_ptr<sql::ResultSet> schemaRows(meta->getSchemata());
  ASSERT_TRUE(schemaRows->next());
  const auto schema = schemaRows->getString("Database");
  EXPECT_EQ(sql::SQLString("sqlstudio_dbc_general"), schema);
  std::unique_ptr<sql::ResultSet> objectRows(meta->getSchemaObjects("", schema));
  ASSERT_TRUE(objectRows->next());
  EXPECT_EQ(sql::SQLString("table"), objectRows->getString("OBJECT_TYPE"));
  EXPECT_EQ(sql::SQLString("product"), objectRows->getString("NAME"));
  EXPECT_EQ(sql::SQLString("CREATE TABLE product (id INT)"), objectRows->getString("DDL"));
  EXPECT_FALSE(objectRows->next());
  EXPECT_FALSE(schemaRows->next());
}

TEST_F(DbcGeneralTest, TransactionTests) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  auto prepared = newMock<MockPreparedStatement>();
  auto before = newMock<MockResultSet>();
  auto after = newMock<MockResultSet>();
  const sql::SQLString insert("INSERT INTO product(idproduct, name) VALUES(?, ?)");
  const sql::SQLString query("SELECT idproduct, name FROM product");
  {
    InSequence sequence;
    EXPECT_CALL(*connection, setAutoCommit(false));
    EXPECT_CALL(*connection, prepareStatement(insert)).WillOnce(transferOwnership(prepared));
    EXPECT_CALL(*prepared, setInt(1, 1));
    EXPECT_CALL(*prepared, setString(2, sql::SQLString("Harry Potter")));
    EXPECT_CALL(*prepared, executeUpdate()).WillOnce(Return(1));
    EXPECT_CALL(*statement, executeQuery(query)).WillOnce(transferOwnership(before));
    EXPECT_CALL(*before, next()).WillOnce(Return(true));
    EXPECT_CALL(*before, getString(uint32_t(2))).WillOnce(Return(sql::SQLString("Harry Potter")));
    EXPECT_CALL(*before, next()).WillOnce(Return(false));
    EXPECT_CALL(*connection, rollback());
    EXPECT_CALL(*statement, executeQuery(query)).WillOnce(transferOwnership(after));
    EXPECT_CALL(*after, next()).WillOnce(Return(false));
  }

  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  wrapper->setAutoCommit(false);
  std::unique_ptr<sql::PreparedStatement> insertStatement(wrapper->prepareStatement(insert));
  insertStatement->setInt(1, 1);
  insertStatement->setString(2, "Harry Potter");
  EXPECT_EQ(1, insertStatement->executeUpdate());
  {
    std::unique_ptr<sql::ResultSet> result(owned->executeQuery(query));
    ASSERT_TRUE(result->next());
    EXPECT_EQ(sql::SQLString("Harry Potter"), result->getString(2));
    EXPECT_FALSE(result->next());
  }
  wrapper->rollback();
  {
    std::unique_ptr<sql::ResultSet> result(owned->executeQuery(query));
    EXPECT_FALSE(result->next());
  }
}

TEST_F(DbcGeneralTest, RollbackFailurePropagates) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  EXPECT_CALL(*connection, rollback())
    .WillOnce(Throw(sql::SQLException("Mock rollback failed", "HY000", 2013)));
  try {
    wrapper->rollback();
    FAIL() << "Expected rollback failure";
  } catch (const sql::SQLException &error) {
    EXPECT_EQ(2013, error.getErrorCode());
    EXPECT_EQ(sql::SQLString("HY000"), error.getSQLState());
    EXPECT_STREQ("Mock rollback failed", error.what());
  }
}

class DbcGeneralIntegrationTest : public Test {
protected:
  std::unique_ptr<SqlStudioTester> tester;
  std::unique_ptr<sql::DriverManager> manager;
  db_mgmt_ConnectionRef connectionProperties;
  sql::ConnectionWrapper connection;
  std::unique_ptr<sql::Statement> statement;
  std::string schema;
  bool schemaCreated = false;

  void SetUp() override {
    const char *enabled = std::getenv("SQLSTUDIO_DBC_LIVE_TESTS");
    if (!enabled)
      GTEST_SKIP() << "Set SQLSTUDIO_DBC_LIVE_TESTS=1 to run the MySQL integration tests";
    ASSERT_STREQ("1", enabled) << "SQLSTUDIO_DBC_LIVE_TESTS must be 1 when set";

    grt::GRT::get();
    tester = std::make_unique<SqlStudioTester>();
    manager = std::make_unique<sql::DriverManager>();
    manager->set_testing();
    connectionProperties = db_mgmt_ConnectionRef(grt::Initialized);
    setupConnectionEnvironment(connectionProperties);
    connection = manager->getConnection(connectionProperties);
    ASSERT_NE(nullptr, connection.get());
    statement.reset(connection->createStatement());
    ASSERT_NE(nullptr, statement.get());

    std::string guid;
    for (char c : grt::get_guid()) {
      if (c >= 'A' && c <= 'F')
        guid += static_cast<char>(c - 'A' + 'a');
      else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
        guid += c;
    }
    ASSERT_EQ(32U, guid.size());
    schema = "sqlstudio_dbc_" + guid;
  }

  void TearDown() override {
    if (schemaCreated) {
      try {
        statement->execute("DROP SCHEMA `" + schema + "`");
      } catch (const sql::SQLException &error) {
        ADD_FAILURE() << "Could not drop test schema " << schema << ": " << error.what();
      }
    }
    statement.reset();
    connection.reset();
    connectionProperties = db_mgmt_ConnectionRef();
    manager.reset();
    tester.reset();
  }

  void createSchema() {
    statement->execute("CREATE SCHEMA `" + schema + "`");
    schemaCreated = true;
  }

  bool schemaExists() {
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery(
      "SELECT SCHEMA_NAME FROM INFORMATION_SCHEMA.SCHEMATA WHERE SCHEMA_NAME = '" + schema + "'"));
    if (!result)
      throw std::runtime_error("Schema lookup returned no result");
    return result->next();
  }
};

TEST_F(DbcGeneralIntegrationTest, ChecksInitialFunctionality) {
  ASSERT_FALSE(schemaExists());
  createSchema();
  EXPECT_TRUE(schemaExists());
}

TEST_F(DbcGeneralIntegrationTest, MetadataFetchTest) {
  createSchema();
  statement->execute("CREATE TABLE `" + schema + "`.product (id INT PRIMARY KEY) ENGINE=InnoDB");
  auto metadata = connection->getMetaData();
  ASSERT_NE(nullptr, metadata);
  std::unique_ptr<sql::ResultSet> schemata(metadata->getSchemata());
  ASSERT_NE(nullptr, schemata.get());
  bool foundSchema = false;
  while (schemata->next()) {
    if (schemata->getString("Database").asStdString() == schema) {
      foundSchema = true;
      break;
    }
  }
  ASSERT_TRUE(foundSchema);

  std::unique_ptr<sql::ResultSet> objects(metadata->getSchemaObjects("", schema));
  ASSERT_NE(nullptr, objects.get());
  bool foundTable = false;
  while (objects->next()) {
    if (objects->getString("NAME") == "product") {
      EXPECT_EQ(sql::SQLString("table"), objects->getString("OBJECT_TYPE"));
      EXPECT_NE(std::string::npos, objects->getString("DDL").asStdString().find("product"));
      foundTable = true;
      break;
    }
  }
  EXPECT_TRUE(foundTable);
}

TEST_F(DbcGeneralIntegrationTest, TransactionTests) {
  createSchema();
  const std::string table = "`" + schema + "`.product";
  statement->execute("CREATE TABLE " + table +
                     " (idproduct INT NOT NULL AUTO_INCREMENT PRIMARY KEY, name VARCHAR(80)) ENGINE=InnoDB");
  connection->setAutoCommit(false);
  std::unique_ptr<sql::PreparedStatement> insert(connection->prepareStatement(
    "INSERT INTO " + table + " (idproduct, name) VALUES (?, ?)"));
  ASSERT_NE(nullptr, insert.get());
  insert->setInt(1, 1);
  insert->setString(2, "Harry Potter");
  ASSERT_EQ(1, insert->executeUpdate());
  {
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT name FROM " + table));
    ASSERT_NE(nullptr, rows.get());
    ASSERT_TRUE(rows->next());
    EXPECT_EQ(sql::SQLString("Harry Potter"), rows->getString(1));
    EXPECT_FALSE(rows->next());
  }

  connection->rollback();
  {
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT name FROM " + table));
    ASSERT_NE(nullptr, rows.get());
    EXPECT_FALSE(rows->next());
  }
  insert->setInt(1, 2);
  insert->setString(2, "Committed");
  ASSERT_EQ(1, insert->executeUpdate());
  connection->commit();
  {
    std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT name FROM " + table));
    ASSERT_NE(nullptr, rows.get());
    ASSERT_TRUE(rows->next());
    EXPECT_EQ(sql::SQLString("Committed"), rows->getString(1));
    EXPECT_FALSE(rows->next());
  }
}
} // namespace
