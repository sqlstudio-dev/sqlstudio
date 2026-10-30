/*
 * Copyright (c) 2011, 2019, Oracle and/or its affiliates. All rights reserved.
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

#include "connector_mocks.h"
#include "cdbc/src/driver_manager.h"
#include "wb_test_helpers.h"

namespace {
using namespace testing;
using namespace sql::test;

template <typename T>
StrictMock<T> *newMock() {
  auto mock = new StrictMock<T>();
  EXPECT_CALL(*mock, destroyed());
  return mock;
}

// Keep unconsumed results owned if connection initialization fails before requesting them.
template <typename T>
auto transferOwnership(T *mock) {
  auto owner = std::make_shared<std::unique_ptr<T>>(mock);
  return [owner] { return owner->release(); };
}

class DbcConnectionTest : public Test {
protected:
  std::unique_ptr<SqlStudioTester> tester;
  StrictMock<MockDriver> driver;
  std::unique_ptr<sql::DriverManager> manager;
  db_mgmt_ConnectionRef connectionProperties;

  void SetUp() override {
    grt::GRT::get();
    tester = std::make_unique<SqlStudioTester>();
    manager = std::make_unique<sql::DriverManager>([this] { return &driver; });
    connectionProperties = db_mgmt_ConnectionRef(grt::Initialized);
    db_mgmt_DriverRef profile(grt::Initialized);
    profile->name("MysqlNative");
    profile->driverLibraryName("mysqlcppconn");
    connectionProperties->driver(profile);
    auto params = connectionProperties->parameterValues();
    params.set("hostName", grt::StringRef("mock.invalid"));
    params.set("userName", grt::StringRef("test_user"));
    params.set("port", grt::IntegerRef(3306));
    EXPECT_CALL(driver, getMajorVersion()).WillRepeatedly(Return(8));
    EXPECT_CALL(driver, getMinorVersion()).WillRepeatedly(Return(0));
    EXPECT_CALL(driver, getPatchVersion()).WillRepeatedly(Return(42));
    EXPECT_CALL(driver, destroyed());
  }

  void TearDown() override {
    manager.reset();
    connectionProperties = db_mgmt_ConnectionRef();
    tester.reset();
  }

  MockConnection *expectConnection(bool utf8mb4 = true,
                                   std::function<void(sql::ConnectOptionsMap &)> inspectOptions = {}) {
    auto connection = newMock<MockConnection>();
    auto takeConnection = transferOwnership(connection);
    EXPECT_CALL(driver, connect(A<sql::ConnectOptionsMap &>()))
      .WillOnce([takeConnection, inspectOptions](sql::ConnectOptionsMap &options) -> sql::Connection * {
        if (inspectOptions)
          inspectOptions(options);
        return takeConnection();
      }).RetiresOnSaturation();

    auto userStatement = newMock<MockStatement>();
    auto userResult = newMock<MockResultSet>();
    EXPECT_CALL(*userStatement, executeQuery(sql::SQLString("SELECT current_user()")))
      .WillOnce(transferOwnership(userResult));
    EXPECT_CALL(*userResult, next()).WillOnce(Return(true));
    EXPECT_CALL(*userResult, getString(uint32_t(1))).WillOnce(Return(sql::SQLString("test_user@mock")));

    auto charsetStatement = newMock<MockStatement>();
    auto charsetResult = newMock<MockResultSet>();
    EXPECT_CALL(*charsetStatement, executeQuery(sql::SQLString("show character set where charset = 'utf8mb4'")))
      .WillOnce(transferOwnership(charsetResult));
    EXPECT_CALL(*charsetResult, rowsCount()).WillOnce(Return(utf8mb4 ? 1 : 0));
    EXPECT_CALL(*charsetStatement, executeUpdate(sql::SQLString(utf8mb4 ? "SET NAMES 'utf8mb4'" : "SET NAMES 'utf8'")))
      .WillOnce(Return(0));
    EXPECT_CALL(*connection, createStatement())
      .WillOnce(transferOwnership(userStatement))
      .WillOnce(transferOwnership(charsetStatement));
    return connection;
  }
};

TEST_F(DbcConnectionTest, TestInitializationOfConnectionAndDestruction) {
  auto connection = expectConnection();
  std::weak_ptr<sql::Connection> lifetime;
  {
    auto wrapper = manager->getConnection(connectionProperties);
    EXPECT_EQ(connection, wrapper.get());
    EXPECT_EQ("C++ 8.0.42", manager->getClientLibVersion());
    lifetime = wrapper.get_ptr();
    auto copy = wrapper;
    wrapper.reset();
    EXPECT_EQ(nullptr, wrapper.get());
    EXPECT_FALSE(lifetime.expired());
    EXPECT_EQ(connection, copy.get());
  }
  EXPECT_TRUE(lifetime.expired());
  EXPECT_CALL(driver, threadEnd());
  manager->thread_cleanup();
}

TEST_F(DbcConnectionTest, TestInitializationOfStatementAndDestruction) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  std::unique_ptr<sql::Statement> owned(wrapper->createStatement());
  EXPECT_EQ(statement, owned.get());
}

TEST_F(DbcConnectionTest, TestConstructionOfMetadataObject) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  StrictMock<MockDatabaseMetaData> metadata;
  EXPECT_CALL(metadata, destroyed());
  EXPECT_CALL(*connection, getMetaData()).WillOnce(Return(&metadata));
  EXPECT_EQ(&metadata, wrapper->getMetaData());
}

TEST_F(DbcConnectionTest, TestAutocommit) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  InSequence sequence;
  EXPECT_CALL(*connection, commit());
  EXPECT_CALL(*connection, rollback());
  EXPECT_CALL(*connection, getAutoCommit()).WillOnce(Return(false));
  EXPECT_CALL(*connection, setAutoCommit(true));
  EXPECT_CALL(*connection, getAutoCommit()).WillOnce(Return(true));
  EXPECT_CALL(*connection, commit());
  EXPECT_CALL(*connection, setAutoCommit(false));
  EXPECT_CALL(*connection, getAutoCommit()).WillOnce(Return(false));

  wrapper->commit();
  wrapper->rollback();
  const bool original = wrapper->getAutoCommit();
  wrapper->setAutoCommit(true);
  EXPECT_TRUE(wrapper->getAutoCommit());
  wrapper->commit();
  wrapper->setAutoCommit(original);
  EXPECT_EQ(original, wrapper->getAutoCommit());
}

TEST_F(DbcConnectionTest, TestClearWarnings) {
  auto connection = expectConnection();
  auto wrapper = manager->getConnection(connectionProperties);
  EXPECT_CALL(*connection, clearWarnings()).Times(3);
  wrapper->clearWarnings();
  wrapper->clearWarnings();
  wrapper->clearWarnings();
}

TEST_F(DbcConnectionTest, Test2Connections) {
  auto first = expectConnection();
  auto firstWrapper = manager->getConnection(connectionProperties);
  auto second = expectConnection();
  auto secondWrapper = manager->getConnection(connectionProperties);
  EXPECT_EQ(first, firstWrapper.get());
  EXPECT_EQ(second, secondWrapper.get());
  EXPECT_NE(firstWrapper.get(), secondWrapper.get());
  firstWrapper.reset();
  EXPECT_EQ(second, secondWrapper.get());
}

TEST_F(DbcConnectionTest, ConnectionFailurePropagates) {
  EXPECT_CALL(driver, connect(A<sql::ConnectOptionsMap &>()))
    .WillOnce(Throw(sql::SQLException("Mock connection rejected", "HY000", 2003)));
  try {
    manager->getConnection(connectionProperties);
    FAIL() << "Expected connector failure";
  } catch (const sql::SQLException &error) {
    EXPECT_EQ(2003, error.getErrorCode());
    EXPECT_EQ(sql::SQLString("HY000"), error.getSQLState());
    EXPECT_STREQ("Mock connection rejected", error.what());
  }
}

TEST_F(DbcConnectionTest, QueryFailurePropagatesWithoutInvalidatingOtherConnection) {
  auto first = expectConnection();
  auto firstWrapper = manager->getConnection(connectionProperties);
  auto second = expectConnection();
  auto secondWrapper = manager->getConnection(connectionProperties);
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(*first, createStatement()).WillOnce(transferOwnership(statement));
  std::unique_ptr<sql::Statement> owned(firstWrapper->createStatement());
  EXPECT_CALL(*statement, executeQuery(_))
    .WillOnce(Throw(sql::SQLException("Mock connection lost", "HY000", 2013)));
  EXPECT_THROW(owned->executeQuery("SELECT 1"), sql::SQLException);
  EXPECT_CALL(*second, clearWarnings());
  secondWrapper->clearWarnings();
}

TEST_F(DbcConnectionTest, SessionInitializationFailureReleasesConnectionAndStatement) {
  auto connection = newMock<MockConnection>();
  auto statement = newMock<MockStatement>();
  EXPECT_CALL(driver, connect(A<sql::ConnectOptionsMap &>())).WillOnce(transferOwnership(connection));
  EXPECT_CALL(*connection, createStatement()).WillOnce(transferOwnership(statement));
  EXPECT_CALL(*statement, executeQuery(_))
    .WillOnce(Throw(sql::SQLException("Mock session initialization failed", "HY000", 2013)));
  EXPECT_THROW(manager->getConnection(connectionProperties), sql::SQLException);
}

TEST_F(DbcConnectionTest, ConnectionOptionsAreConvertedAndInitializerRuns) {
  auto params = connectionProperties->parameterValues();
  params.set("schema", grt::StringRef("test_schema"));
  params.set("CLIENT_COMPRESS", grt::IntegerRef(1));
  // Inspect the options without dumping their contents (which can include passwords).
  auto connection = expectConnection(true, [](sql::ConnectOptionsMap &options) {
    EXPECT_EQ(sql::SQLString("[mock.invalid]"), *options.at("hostName").get<sql::SQLString>());
    EXPECT_EQ(3306, *options.at("port").get<int>());
    EXPECT_EQ(60, *options.at("OPT_CONNECT_TIMEOUT").get<int>());
    EXPECT_EQ(30, *options.at("OPT_READ_TIMEOUT").get<int>());
    EXPECT_TRUE(*options.at("CLIENT_COMPRESS").get<bool>());
  });
  EXPECT_CALL(*connection, setSchema(sql::SQLString("test_schema")));
  EXPECT_CALL(*connection, clearWarnings());
  bool initialized = false;
  auto wrapper = manager->getConnection(connectionProperties,
    [&](sql::Connection *actual, const db_mgmt_ConnectionRef &properties) {
      EXPECT_EQ(connection, actual);
      EXPECT_EQ(connectionProperties, properties);
      actual->clearWarnings();
      initialized = true;
    });
  EXPECT_TRUE(initialized);
  EXPECT_EQ(connection, wrapper.get());
}

TEST_F(DbcConnectionTest, FallsBackToUtf8ForOlderServers) {
  expectConnection(false);
  auto wrapper = manager->getConnection(connectionProperties);
  EXPECT_NE(nullptr, wrapper.get());
}

TEST_F(DbcConnectionTest, RejectsMissingDriverBeforeConnecting) {
  connectionProperties->driver(db_mgmt_DriverRef());
  EXPECT_THROW(manager->getConnection(connectionProperties), sql::SQLException);
}

TEST_F(DbcConnectionTest, RejectsNullDriverFactoryResult) {
  sql::DriverManager unavailable([]() -> sql::Driver * { return nullptr; });
  EXPECT_THROW(unavailable.getConnection(connectionProperties), sql::SQLException);
}
} // namespace
