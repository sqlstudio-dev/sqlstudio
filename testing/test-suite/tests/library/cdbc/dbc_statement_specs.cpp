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

#include "connector_test_fixture.h"
#include "cdbc/src/sql_batch_exec.h"
#include "grtsqlparser/sql_facade.h"

namespace {
using namespace testing;
using namespace sql::test;

// Exercise connector contracts through an injected driver; no database is contacted.
class DbcStatementTest : public MockConnectorFixture {
protected:
  sql::ConnectionWrapper connection;
  MockStatement *mockStatement = nullptr;
  std::unique_ptr<sql::Statement> statement;

  void SetUp() override {
    MockConnectorFixture::SetUp();
    auto mockConnection = expectConnection();
    connection = manager->getConnection(connectionProperties);
    mockStatement = newMock<MockStatement>();
    EXPECT_CALL(*mockConnection, createStatement()).WillOnce(transferOwnership(mockStatement));
    statement.reset(connection->createStatement());
    ASSERT_NE(nullptr, statement.get());
  }

  void TearDown() override {
    statement.reset();
    connection.reset();
    MockConnectorFixture::TearDown();
  }
};

TEST_F(DbcStatementTest, SimpleUpdateStatement) {
  const sql::SQLString query("UPDATE test_function SET a = 2, b = 222 WHERE b = 111");
  EXPECT_CALL(*mockStatement, execute(query)).WillOnce(Return(false));
  EXPECT_FALSE(statement->execute(query));
}

TEST_F(DbcStatementTest, SimpleQueryStatement) {
  const sql::SQLString query("SELECT * FROM test_function");
  EXPECT_CALL(*mockStatement, execute(query)).WillOnce(Return(true));
  EXPECT_TRUE(statement->execute(query));
}

TEST_F(DbcStatementTest, ExecuteQueryReturnsResultSet) {
  auto result = newMock<MockResultSet>();
  EXPECT_CALL(*mockStatement, executeQuery(sql::SQLString("SELECT a, b FROM test_function")))
    .WillOnce(transferOwnership(result));
  {
    InSequence sequence;
    EXPECT_CALL(*result, next()).WillOnce(Return(true));
    EXPECT_CALL(*result, getInt(uint32_t(1))).WillOnce(Return(1));
    EXPECT_CALL(*result, getInt(uint32_t(2))).WillOnce(Return(111));
    EXPECT_CALL(*result, next()).WillOnce(Return(false));
  }
  std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT a, b FROM test_function"));
  ASSERT_NE(nullptr, rows.get());
  ASSERT_TRUE(rows->next());
  EXPECT_EQ(1, rows->getInt(1));
  EXPECT_EQ(111, rows->getInt(2));
  EXPECT_FALSE(rows->next());
}

TEST_F(DbcStatementTest, ExecuteQueryReturnsEmptyResultSet) {
  auto result = newMock<MockResultSet>();
  EXPECT_CALL(*mockStatement, executeQuery(sql::SQLString("SELECT * FROM test_function WHERE 1=2")))
    .WillOnce(transferOwnership(result));
  EXPECT_CALL(*result, next()).WillOnce(Return(false));
  std::unique_ptr<sql::ResultSet> rows(statement->executeQuery("SELECT * FROM test_function WHERE 1=2"));
  ASSERT_NE(nullptr, rows.get());
  EXPECT_FALSE(rows->next());
}

TEST_F(DbcStatementTest, ExecuteQueryInsertShouldThrow) {
  const sql::SQLString query("INSERT INTO test_function (a,b) VALUES(2,200)");
  EXPECT_CALL(*mockStatement, executeQuery(query))
    .WillOnce(Throw(sql::SQLException("Statement did not produce a result set")));
  EXPECT_THROW(statement->executeQuery(query), sql::SQLException);
}

TEST_F(DbcStatementTest, ExecuteUpdateReturnsRowCount) {
  const sql::SQLString query("UPDATE test_function SET a = 123");
  EXPECT_CALL(*mockStatement, executeUpdate(query)).WillOnce(Return(1));
  EXPECT_EQ(1, statement->executeUpdate(query));
}

TEST_F(DbcStatementTest, ExecuteUpdateSelectShouldThrow) {
  const sql::SQLString query("SELECT * FROM test_function");
  EXPECT_CALL(*mockStatement, executeUpdate(query))
    .WillOnce(Throw(sql::SQLException("Statement produced a result set")));
  EXPECT_THROW(statement->executeUpdate(query), sql::SQLException);
}

TEST_F(DbcStatementTest, GetFetchSize) {
  EXPECT_CALL(*mockStatement, getFetchSize()).WillOnce(Return(size_t(64)));
  EXPECT_EQ(size_t(64), statement->getFetchSize());
}

TEST_F(DbcStatementTest, GetResultSetAfterExecuteQuery) {
  auto result = newMock<MockResultSet>();
  {
    InSequence sequence;
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("SELECT * FROM test_function"))).WillOnce(Return(true));
    EXPECT_CALL(*mockStatement, getResultSet()).WillOnce(transferOwnership(result));
  }
  ASSERT_TRUE(statement->execute("SELECT * FROM test_function"));
  std::unique_ptr<sql::ResultSet> rows(statement->getResultSet());
  EXPECT_EQ(result, rows.get());
}

TEST_F(DbcStatementTest, GetResultSetAfterExecuteUpdate) {
  {
    InSequence sequence;
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("UPDATE test_function SET a = 222")))
      .WillOnce(Return(false));
    EXPECT_CALL(*mockStatement, getResultSet()).WillOnce(Return(nullptr));
  }
  ASSERT_FALSE(statement->execute("UPDATE test_function SET a = 222"));
  std::unique_ptr<sql::ResultSet> rows(statement->getResultSet());
  EXPECT_EQ(nullptr, rows.get());
}

TEST_F(DbcStatementTest, SetFetchSize) {
  InSequence sequence;
  EXPECT_CALL(*mockStatement, setFetchSize(size_t(64)));
  EXPECT_CALL(*mockStatement, getFetchSize()).WillOnce(Return(size_t(64)));
  statement->setFetchSize(64);
  EXPECT_EQ(size_t(64), statement->getFetchSize());
}

// The connector takes unsigned sizes/timeouts; test error propagation without
// implying that the interface accepts negative values.
TEST_F(DbcStatementTest, SetFetchSizeFailurePropagates) {
  EXPECT_CALL(*mockStatement, setFetchSize(size_t(64)))
    .WillOnce(Throw(sql::SQLException("Fetch size is unsupported")));
  EXPECT_THROW(statement->setFetchSize(64), sql::SQLException);
}

TEST_F(DbcStatementTest, SetQueryTimeoutFailurePropagates) {
  EXPECT_CALL(*mockStatement, setQueryTimeout(10u))
    .WillOnce(Throw(sql::SQLException("Query timeout is unsupported")));
  EXPECT_THROW(statement->setQueryTimeout(10), sql::SQLException);
}

TEST_F(DbcStatementTest, AddBatchExecuteBatch) {
  auto splitter = SqlFacade::instance_for_rdbms_name("Mysql");
  ASSERT_NE(nullptr, splitter);
  std::list<std::string> statements;
  splitter->splitSqlScript("CREATE TABLE table1 (id int);SELECT 1;INSERT INTO table1 VALUES (1);SELECT 2;", statements);
  const std::list<std::string> expected = {
    "CREATE TABLE table1 (id int)", "SELECT 1", "INSERT INTO table1 VALUES (1)", "SELECT 2"};
  ASSERT_EQ(expected, statements);
  auto first = new StrictMock<MockResultSet>();
  auto second = new StrictMock<MockResultSet>();
  // Destruction must happen before executing the next SQL statement.
  {
    InSequence sequence;
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("CREATE TABLE table1 (id int)"))).WillOnce(Return(false));
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("SELECT 1"))).WillOnce(Return(true));
    EXPECT_CALL(*mockStatement, getResultSet()).WillOnce(transferOwnership(first));
    EXPECT_CALL(*first, destroyed());
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("INSERT INTO table1 VALUES (1)"))).WillOnce(Return(false));
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("SELECT 2"))).WillOnce(Return(true));
    EXPECT_CALL(*mockStatement, getResultSet()).WillOnce(transferOwnership(second));
    EXPECT_CALL(*second, destroyed());
  }
  sql::SqlBatchExec batch;
  long successes = -1, errors = -1;
  batch.batch_exec_stat_cb([&](long ok, long failed) { successes = ok; errors = failed; return 0; });
  EXPECT_EQ(0, batch(statement.get(), statements));
  EXPECT_EQ(4, successes);
  EXPECT_EQ(0, errors);
  EXPECT_EQ(expected, batch.sql_log());
}

TEST_F(DbcStatementTest, BatchFailurePropagatesAndStopsExecution) {
  std::list<std::string> statements = {"UPDATE table1 SET id = 2", "SELECT 1"};
  EXPECT_CALL(*mockStatement, execute(sql::SQLString(statements.front())))
    .WillOnce(Throw(sql::SQLException("Mock connection lost", "HY000", 2013)));
  sql::SqlBatchExec batch;
  EXPECT_THROW(batch(statement.get(), statements), sql::SQLException);
  EXPECT_EQ(std::list<std::string>({statements.front()}), batch.sql_log());
}

TEST_F(DbcStatementTest, BatchCanContinueAfterReportedError) {
  std::list<std::string> statements = {"UPDATE table1 SET id = 2", "SELECT 1"};
  auto result = newMock<MockResultSet>();
  {
    InSequence sequence;
    EXPECT_CALL(*mockStatement, execute(sql::SQLString(statements.front())))
      .WillOnce(Throw(sql::SQLException("Mock query failure", "HY000", 1234)));
    EXPECT_CALL(*mockStatement, execute(sql::SQLString("SELECT 1"))).WillOnce(Return(true));
    EXPECT_CALL(*mockStatement, getResultSet()).WillOnce(transferOwnership(result));
  }
  sql::SqlBatchExec batch;
  batch.stop_on_error(false);
  int reports = 0;
  batch.error_cb([&](long long code, const std::string &message, const std::string &query) {
    ++reports;
    EXPECT_EQ(1234, code);
    EXPECT_EQ("Mock query failure", message);
    EXPECT_EQ(statements.front(), query);
    return 0;
  });
  long successes = -1, errors = -1;
  batch.batch_exec_stat_cb([&](long ok, long failed) { successes = ok; errors = failed; return 0; });
  EXPECT_EQ(1, batch(statement.get(), statements));
  EXPECT_EQ(1, reports);
  EXPECT_EQ(1, successes);
  EXPECT_EQ(1, errors);
  EXPECT_EQ(statements, batch.sql_log());
}
} // namespace
