/*
 * Copyright (c) 2012, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include <cppconn/prepared_statement.h>
#include <cppconn/connection.h>
#include <cppconn/statement.h>
#include <cppconn/resultset.h>
#include <cppconn/metadata.h>

#include "connector_test_fixture.h"
#include "grtpp_util.h"

namespace {
using namespace testing;
using namespace sql::test;

class DbcResultSetTest : public MockConnectorFixture {
protected:
  void checkTransactionCalls(bool autoCommit) {
    auto connection = expectConnection();
    auto wrapper = manager->getConnection(connectionProperties);
    auto statement = newMock<MockStatement>();
    EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
    EXPECT_CALL(*statement, getConnection()).WillRepeatedly(Return(connection));
    auto before = newMock<MockResultSet>();
    auto deleted = newMock<MockResultSet>();
    auto rolledBack = newMock<MockResultSet>();
    auto committed = newMock<MockResultSet>();
    const sql::SQLString query("SELECT COUNT(*) FROM test_function_tx");
    const sql::SQLString remove("DELETE FROM test_function_tx WHERE 1");
    {
      InSequence sequence;
      EXPECT_CALL(*connection, getAutoCommit()).WillOnce(Return(true));
      EXPECT_CALL(*connection, setAutoCommit(autoCommit));
      EXPECT_CALL(*statement, executeQuery(query)).WillOnce(transferOwnership(before));
      EXPECT_CALL(*before, next()).WillOnce(Return(true));
      EXPECT_CALL(*before, getInt(uint32_t(1))).WillOnce(Return(1));
      EXPECT_CALL(*before, next()).WillOnce(Return(false));
      EXPECT_CALL(*statement, executeUpdate(remove)).WillOnce(Return(1));
      EXPECT_CALL(*statement, executeQuery(query)).WillOnce(transferOwnership(deleted));
      EXPECT_CALL(*deleted, next()).WillOnce(Return(true));
      EXPECT_CALL(*deleted, getInt(uint32_t(1))).WillOnce(Return(0));
      EXPECT_CALL(*deleted, next()).WillOnce(Return(false));
      EXPECT_CALL(*connection, rollback());
      EXPECT_CALL(*statement, executeQuery(query)).WillOnce(transferOwnership(rolledBack));
      EXPECT_CALL(*rolledBack, next()).WillOnce(Return(true));
      EXPECT_CALL(*rolledBack, getInt(uint32_t(1))).WillOnce(Return(autoCommit ? 0 : 1));
      EXPECT_CALL(*rolledBack, next()).WillOnce(Return(false));
      EXPECT_CALL(*statement, executeUpdate(remove)).WillOnce(Return(autoCommit ? 0 : 1));
      EXPECT_CALL(*connection, commit());
      EXPECT_CALL(*statement, executeQuery(query)).WillOnce(transferOwnership(committed));
      EXPECT_CALL(*committed, next()).WillOnce(Return(true));
      EXPECT_CALL(*committed, getInt(uint32_t(1))).WillOnce(Return(0));
      EXPECT_CALL(*committed, next()).WillOnce(Return(false));
      EXPECT_CALL(*connection, setAutoCommit(true));
    }

    std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
    const bool oldMode = wrapper->getAutoCommit();
    wrapper->setAutoCommit(autoCommit);
    auto countRows = [&] {
      std::unique_ptr<sql::ResultSet> rows(owned->executeQuery(query));
      EXPECT_TRUE(rows->next());
      const int count = rows->getInt(1);
      EXPECT_FALSE(rows->next());
      return count;
    };
    EXPECT_EQ(1, countRows());
    EXPECT_EQ(1, owned->executeUpdate(remove));
    EXPECT_EQ(0, countRows());
    owned->getConnection()->rollback();
    EXPECT_EQ(autoCommit ? 0 : 1, countRows());
    EXPECT_EQ(autoCommit ? 0 : 1, owned->executeUpdate(remove));
    owned->getConnection()->commit();
    EXPECT_EQ(0, countRows());
    wrapper->setAutoCommit(oldMode);
  }
};

TEST_F(DbcResultSetTest, TestConnection) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  ASSERT_NE(nullptr, wrapper.get());
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  EXPECT_CALL(*statement, getConnection()).WillOnce(Return(connection));
  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  ASSERT_NE(nullptr, owned.get());
  EXPECT_EQ(wrapper.get(), owned->getConnection());
}

TEST_F(DbcResultSetTest, TestPreparation) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto prepared = newMock<MockPreparedStatement>();
  auto result = newMock<MockResultSet>();
  {
    InSequence sequence;
    EXPECT_CALL(*connection, prepareStatement(sql::SQLString("SELECT a, b, c FROM test_function_tx")))
      .WillOnce(transferOwnership(prepared));
    EXPECT_CALL(*prepared, executeQuery()).WillOnce(transferOwnership(result));
    EXPECT_CALL(*result, next()).WillOnce(Return(true));
    EXPECT_CALL(*result, getInt(uint32_t(1))).WillOnce(Return(1));
    EXPECT_CALL(*result, getInt(uint32_t(2))).WillOnce(Return(111));
    EXPECT_CALL(*result, isNull(uint32_t(3))).WillOnce(Return(true));
    EXPECT_CALL(*result, next()).WillOnce(Return(false));
  }
  std::unique_ptr<sql::PreparedStatement> owned(wrapper->prepareStatement("SELECT a, b, c FROM test_function_tx"));
  ASSERT_NE(nullptr, owned.get());
  std::unique_ptr<sql::ResultSet> rows(owned->executeQuery());
  ASSERT_NE(nullptr, rows.get());
  ASSERT_TRUE(rows->next());
  EXPECT_EQ(1, rows->getInt(1));
  EXPECT_EQ(111, rows->getInt(2));
  EXPECT_TRUE(rows->isNull(3));
  EXPECT_FALSE(rows->next());
}

TEST_F(DbcResultSetTest, TestExecuteQuerySameStatement) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  auto first = newMock<MockResultSet>();
  auto second = newMock<MockResultSet>();
  {
    InSequence sequence;
    EXPECT_CALL(*statement, executeQuery(sql::SQLString("SELECT 1 FROM DUAL")))
      .WillOnce(transferOwnership(first));
    EXPECT_CALL(*first, next()).WillOnce(Return(true));
    EXPECT_CALL(*first, getInt(uint32_t(1))).WillOnce(Return(1));
    EXPECT_CALL(*first, next()).WillOnce(Return(false));
    EXPECT_CALL(*statement, executeQuery(sql::SQLString("SELECT 2 FROM DUAL")))
      .WillOnce(transferOwnership(second));
    EXPECT_CALL(*second, next()).WillOnce(Return(true));
    EXPECT_CALL(*second, getInt(uint32_t(1))).WillOnce(Return(2));
    EXPECT_CALL(*second, next()).WillOnce(Return(false));
  }
  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  for (int value = 1; value <= 2; ++value) {
    std::unique_ptr<sql::ResultSet> rows(owned->executeQuery(
      value == 1 ? "SELECT 1 FROM DUAL" : "SELECT 2 FROM DUAL"));
    ASSERT_NE(nullptr, rows.get());
    ASSERT_TRUE(rows->next());
    EXPECT_EQ(value, rows->getInt(1));
    EXPECT_FALSE(rows->next());
  }
}

TEST_F(DbcResultSetTest, TestExecuteTwoQueriesSameStatement) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  auto result = newMock<MockResultSet>();
  {
    InSequence sequence;
    EXPECT_CALL(*statement, executeQuery(sql::SQLString("SELECT 1 FROM DUAL")))
      .WillOnce(transferOwnership(result));
    EXPECT_CALL(*result, next()).WillOnce(Return(true));
    EXPECT_CALL(*result, next()).WillOnce(Return(false));
    EXPECT_CALL(*statement, executeUpdate(sql::SQLString("UPDATE test_function SET a = 2")))
      .WillOnce(Return(1));
  }
  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  {
    std::unique_ptr<sql::ResultSet> rows(owned->executeQuery("SELECT 1 FROM DUAL"));
    ASSERT_NE(nullptr, rows.get());
    ASSERT_TRUE(rows->next());
    EXPECT_FALSE(rows->next());
  }
  EXPECT_EQ(1, owned->executeUpdate("UPDATE test_function SET a = 2"));
}

TEST_F(DbcResultSetTest, TestCommitRollbackAutocommitOff) {
  checkTransactionCalls(false);
}

TEST_F(DbcResultSetTest, TestCommitRollbackAutocommitOn) {
  checkTransactionCalls(true);
}

TEST_F(DbcResultSetTest, TestMultistatementOff) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  const sql::SQLString query("SELECT 1; SELECT 2");
  EXPECT_CALL(*connection, prepareStatement(query))
    .WillOnce(Throw(sql::SQLException("Multiple statements are not supported", "42000", 1064)));
  EXPECT_THROW({
    std::unique_ptr<sql::PreparedStatement> statement(wrapper->prepareStatement(query));
  }, sql::SQLException);
}

TEST_F(DbcResultSetTest, TestOutOfBoundExtraction) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  auto result = newMock<MockResultSet>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  EXPECT_CALL(*statement, executeQuery(sql::SQLString("SELECT 1 AS 'count of rows'")))
    .WillOnce(transferOwnership(result));
  EXPECT_CALL(*result, next()).WillOnce(Return(true)).WillOnce(Return(false));
  const auto invalid = Throw(sql::InvalidArgumentException("Invalid column"));
  EXPECT_CALL(*result, getInt(A<uint32_t>())).WillRepeatedly(invalid);
  EXPECT_CALL(*result, getString(A<uint32_t>())).WillRepeatedly(invalid);
  EXPECT_CALL(*result, getDouble(A<uint32_t>())).WillRepeatedly(invalid);
  EXPECT_CALL(*result, isNull(A<uint32_t>())).WillRepeatedly(invalid);
  EXPECT_CALL(*result, getInt(sql::SQLString("no_such_column"))).WillOnce(invalid);
  EXPECT_CALL(*result, getString(sql::SQLString("no_such_column"))).WillOnce(invalid);
  EXPECT_CALL(*result, getDouble(sql::SQLString("no_such_column"))).WillOnce(invalid);
  EXPECT_CALL(*result, isNull(sql::SQLString("no_such_column"))).WillOnce(invalid);
  EXPECT_CALL(*result, getInt(uint32_t(1))).Times(2).WillRepeatedly(Return(1));
  EXPECT_CALL(*result, getInt(sql::SQLString("count of rows"))).WillOnce(Return(1));
  EXPECT_CALL(*result, getString(uint32_t(1))).WillOnce(Return(sql::SQLString("1")));
  EXPECT_CALL(*result, getString(sql::SQLString("count of rows"))).WillOnce(Return(sql::SQLString("1")));
  EXPECT_CALL(*result, getDouble(uint32_t(1))).WillOnce(Return(1.0L));
  EXPECT_CALL(*result, getDouble(sql::SQLString("count of rows"))).WillOnce(Return(1.0L));
  EXPECT_CALL(*result, isNull(uint32_t(1))).WillOnce(Return(false));
  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  std::unique_ptr<sql::ResultSet> rows(owned->executeQuery("SELECT 1 AS 'count of rows'"));
  ASSERT_NE(nullptr, rows.get());
  ASSERT_TRUE(rows->next());
  for (uint32_t index : {uint32_t(-123), uint32_t(123)}) {
    EXPECT_THROW(rows->getInt(index), sql::InvalidArgumentException);
    EXPECT_THROW(rows->getString(index), sql::InvalidArgumentException);
    EXPECT_THROW(rows->getDouble(index), sql::InvalidArgumentException);
    EXPECT_THROW(rows->isNull(index), sql::InvalidArgumentException);
  }
  EXPECT_THROW(rows->getInt("no_such_column"), sql::InvalidArgumentException);
  EXPECT_THROW(rows->getString("no_such_column"), sql::InvalidArgumentException);
  EXPECT_THROW(rows->getDouble("no_such_column"), sql::InvalidArgumentException);
  EXPECT_THROW(rows->isNull("no_such_column"), sql::InvalidArgumentException);
  EXPECT_THROW(rows->getInt(rows->getInt(1) + 1), sql::InvalidArgumentException);
  EXPECT_EQ(1, rows->getInt(1));
  EXPECT_EQ(1, rows->getInt("count of rows"));
  EXPECT_NEAR(1.0, rows->getDouble(1), 0.1);
  EXPECT_NEAR(1.0, rows->getDouble("count of rows"), 0.1);
  EXPECT_EQ(sql::SQLString("1"), rows->getString(1));
  EXPECT_EQ(sql::SQLString("1"), rows->getString("count of rows"));
  EXPECT_FALSE(rows->isNull(1));
  EXPECT_FALSE(rows->next());
}

static bool populate_test_table(std::unique_ptr<sql::Statement> &stmt) {
  stmt->execute("DROP TABLE IF EXISTS test_function");
  if (true == stmt->execute("CREATE TABLE test_function (a integer, b integer, c integer default null)"))
    return false;

  if (true == stmt->execute("INSERT INTO test_function (a,b,c) VALUES(1, 111, NULL)")) {
    stmt->execute("DROP TABLE test_function");
    return false;
  }
  return true;
}

static bool populate_tx_test_table(std::unique_ptr<sql::Statement> &stmt) {
  stmt->execute("DROP TABLE IF EXISTS test_function_tx");
  if (true ==
      stmt->execute("CREATE TABLE test_function_tx (a integer, b integer, c integer default null) engine = innodb"))
    return false;

  if (true == stmt->execute("INSERT INTO test_function_tx (a,b,c) VALUES(1, 111, NULL)")) {
    stmt->execute("DROP TABLE test_function_tx");
    return false;
  }
  stmt->getConnection()->commit();
  return true;
}

class DbcResultSetIntegrationTest : public LiveConnectorFixture {
protected:
  std::unique_ptr<sql::Statement> cleanupStatement;
  std::string schema;
  bool schemaCreated = false;

  void SetUp() override {
    LiveConnectorFixture::SetUp();
    if (IsSkipped() || HasFatalFailure())
      return;
    cleanupStatement.reset(connection->createStatement());
    ASSERT_NE(nullptr, cleanupStatement.get());
    std::string guid;
    for (char c : grt::get_guid()) {
      if (c >= 'A' && c <= 'F')
        guid += static_cast<char>(c - 'A' + 'a');
      else if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
        guid += c;
    }
    ASSERT_EQ(32U, guid.size());
    schema = "sqlstudio_dbc_result_" + guid;
    cleanupStatement->execute("CREATE SCHEMA `" + schema + "`");
    schemaCreated = true;
    connection->setSchema(schema);
  }

  void TearDown() override {
    if (schemaCreated) {
      try {
        connection->rollback();
        cleanupStatement->execute("DROP SCHEMA `" + schema + "`");
      } catch (const sql::SQLException &error) {
        ADD_FAILURE() << "Could not clean up test schema " << schema << ": " << error.what();
      }
    }
    cleanupStatement.reset();
    LiveConnectorFixture::TearDown();
  }
};

TEST_F(DbcResultSetIntegrationTest, TestConnection) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt(connection->createStatement());
  ASSERT_NE(nullptr, stmt.get());

  ASSERT_EQ(connection, stmt->getConnection());

  stmt->execute("CREATE TABLE connection_probe (id INT)");
  stmt->execute("DROP TABLE connection_probe");
}

TEST_F(DbcResultSetIntegrationTest, TestPreparation) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt1(connection->createStatement());
  ASSERT_NE(nullptr, stmt1.get());

  ASSERT_EQ(wrapper1.get(), stmt1->getConnection());
  ASSERT_TRUE(populate_tx_test_table(stmt1));

  std::unique_ptr<sql::PreparedStatement> ps1(wrapper1->prepareStatement("SELECT a, b, c FROM test_function_tx"));
  ASSERT_NE(nullptr, ps1.get());

  std::unique_ptr<sql::ResultSet> rset(ps1->executeQuery());
  ASSERT_NE(nullptr, rset.get());
  ASSERT_TRUE(rset->next());
  EXPECT_EQ(1, rset->getInt(1));
  EXPECT_EQ(111, rset->getInt(2));
  EXPECT_TRUE(rset->isNull(3));
  EXPECT_FALSE(rset->next());
  rset.reset();
  ps1.reset();

  stmt1->execute("DROP TABLE test_function_tx");
}

TEST_F(DbcResultSetIntegrationTest, TestExecuteQuerySameStatement) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt1(connection->createStatement());
  ASSERT_NE(nullptr, stmt1.get());

  std::unique_ptr<sql::ResultSet> rset1(stmt1->executeQuery("SELECT 1 FROM DUAL"));
  ASSERT_NE(nullptr, rset1.get());
  ASSERT_TRUE(rset1->next());
  EXPECT_EQ(1, rset1->getInt(1));
  EXPECT_FALSE(rset1->next());
  rset1.reset(stmt1->executeQuery("SELECT 2 FROM DUAL"));
  ASSERT_NE(nullptr, rset1.get());
  ASSERT_TRUE(rset1->next());
  EXPECT_EQ(2, rset1->getInt(1));
  EXPECT_FALSE(rset1->next());
}

TEST_F(DbcResultSetIntegrationTest, TestExecuteTwoQueriesSameStatement) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt1(connection->createStatement());
  ASSERT_NE(nullptr, stmt1.get());

  ASSERT_TRUE(populate_test_table(stmt1));

  std::unique_ptr<sql::ResultSet> rset1(stmt1->executeQuery("SELECT 1 FROM DUAL"));
  ASSERT_NE(nullptr, rset1.get());
  ASSERT_TRUE(rset1->next());
  ASSERT_FALSE(rset1->next());
  rset1.reset();

  ASSERT_GT(stmt1->executeUpdate("UPDATE test_function SET a = 2"), 0);

  stmt1->execute("DROP TABLE test_function");
}

TEST_F(DbcResultSetIntegrationTest, TestCommitRollbackAutocommitOff) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt1(connection->createStatement());
  ASSERT_NE(nullptr, stmt1.get());

  ASSERT_EQ(wrapper1.get(), stmt1->getConnection());

  bool old_commit_mode = wrapper1->getAutoCommit();
  wrapper1->setAutoCommit(0);
  ASSERT_TRUE(populate_tx_test_table(stmt1));

  std::unique_ptr<sql::ResultSet> rset1(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset1.get());
  ASSERT_TRUE(rset1->next());
  int count_full_before = rset1->getInt(1);
  ASSERT_FALSE(rset1->next());

  ASSERT_EQ(stmt1->executeUpdate("DELETE FROM test_function_tx WHERE 1"), count_full_before);

  std::unique_ptr<sql::ResultSet> rset2(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset2.get());
  ASSERT_TRUE(rset2->next());
  ASSERT_EQ(rset2->getInt(1), 0);
  ASSERT_FALSE(rset2->next());

  stmt1->getConnection()->rollback();

  std::unique_ptr<sql::ResultSet> rset3(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset3.get());
  ASSERT_TRUE(rset3->next());
  int count_full_after = rset3->getInt(1);
  ASSERT_FALSE(rset3->next());

  ASSERT_EQ(count_full_before, count_full_after);

  ASSERT_EQ(stmt1->executeUpdate("DELETE FROM test_function_tx WHERE 1"), count_full_before);
  stmt1->getConnection()->commit();

  std::unique_ptr<sql::ResultSet> rset4(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset4.get());
  ASSERT_TRUE(rset4->next());
  ASSERT_EQ(rset4->getInt(1), 0);
  ASSERT_FALSE(rset4->next());

  stmt1->execute("DROP TABLE test_function_tx");
  wrapper1->setAutoCommit(old_commit_mode);
}

TEST_F(DbcResultSetIntegrationTest, TestCommitRollbackAutocommitOn) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt1(connection->createStatement());
  ASSERT_NE(nullptr, stmt1.get());

  ASSERT_EQ(wrapper1.get(), stmt1->getConnection());

  bool old_commit_mode = wrapper1->getAutoCommit();
  wrapper1->setAutoCommit(1);
  ASSERT_TRUE(populate_tx_test_table(stmt1));

  std::unique_ptr<sql::ResultSet> rset1(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset1.get());
  ASSERT_TRUE(rset1->next());
  int count_full_before = rset1->getInt(1);
  ASSERT_FALSE(rset1->next());

  ASSERT_EQ(stmt1->executeUpdate("DELETE FROM test_function_tx WHERE 1"), count_full_before);

  std::unique_ptr<sql::ResultSet> rset2(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset2.get());
  ASSERT_TRUE(rset2->next());
  ASSERT_EQ(rset2->getInt(1), 0);
  ASSERT_FALSE(rset2->next());

  stmt1->getConnection()->rollback();

  std::unique_ptr<sql::ResultSet> rset3(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset3.get());
  ASSERT_TRUE(rset3->next());
  ASSERT_EQ(rset3->getInt(1), 0);
  ASSERT_FALSE(rset3->next());

  ASSERT_TRUE(populate_tx_test_table(stmt1));
  ASSERT_EQ(stmt1->executeUpdate("DELETE FROM test_function_tx WHERE 1"), count_full_before);
  stmt1->getConnection()->commit();

  std::unique_ptr<sql::ResultSet> rset4(stmt1->executeQuery("SELECT COUNT(*) FROM test_function_tx"));
  ASSERT_NE(nullptr, rset4.get());
  ASSERT_TRUE(rset4->next());
  ASSERT_EQ(rset4->getInt(1), 0);
  ASSERT_FALSE(rset4->next());

  stmt1->execute("DROP TABLE test_function_tx");
  wrapper1->setAutoCommit(old_commit_mode);
}

TEST_F(DbcResultSetIntegrationTest, TestMultistatementOff) {
  // Native prepared statements reject multiple statements even when regular statements allow them.
  EXPECT_THROW({
    std::unique_ptr<sql::PreparedStatement> statement(connection->prepareStatement("SELECT 1; SELECT 2"));
  }, sql::SQLException);
}

TEST_F(DbcResultSetIntegrationTest, TestOutOfBoundExtraction) {
  sql::ConnectionWrapper wrapper1 = this->connection;
  ASSERT_NE(nullptr, wrapper1.get());

  sql::Connection *connection = wrapper1.get();
  std::unique_ptr<sql::Statement> stmt1(connection->createStatement());
  ASSERT_NE(nullptr, stmt1.get());

  ASSERT_EQ(wrapper1.get(), stmt1->getConnection());
  ASSERT_TRUE(populate_tx_test_table(stmt1));

  std::unique_ptr<sql::ResultSet> rset1(
    stmt1->executeQuery("SELECT COUNT(*) AS 'count of rows' FROM test_function_tx"));
  ASSERT_NE(nullptr, rset1.get());
  ASSERT_TRUE(rset1->next());

  // Out of bounds and invalid column tests
  ASSERT_THROW(rset1->getInt(-123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getInt(123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getInt("no_such_column"), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getString(-123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getString(123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getString("no_such_column"), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getDouble(-123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getDouble(123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getDouble("no_such_column"), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->getInt(rset1->getInt(1) + 1), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->isNull(-123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->isNull(123), sql::InvalidArgumentException);
  ASSERT_THROW(rset1->isNull("no_such_column"), sql::InvalidArgumentException);

  ASSERT_EQ(rset1->getInt(1), 1);
  ASSERT_EQ(rset1->getInt("count of rows"), 1);
  ASSERT_NEAR(1.0, rset1->getDouble(1), 0.1);
  ASSERT_NEAR(1.0, rset1->getDouble("count of rows"), 0.1);
  ASSERT_EQ(rset1->getString(1), std::string("1"));
  ASSERT_EQ(rset1->getString("count of rows"), std::string("1"));
  ASSERT_FALSE(rset1->isNull(1));
  ASSERT_FALSE(rset1->next());

  stmt1->execute("DROP TABLE test_function_tx");
}
} // namespace
