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

#include "connector_test_fixture.h"

#include <cppconn/exception.h>

namespace {
using namespace testing;
using namespace sql::test;

class DbcMetadataTest : public MockConnectorFixture {
protected:
  StrictMock<MockDatabaseMetaData> metadata;
  sql::ConnectionWrapper connection;
  sql::DatabaseMetaData *meta = nullptr;

  void SetUp() override {
    EXPECT_CALL(metadata, destroyed());
    MockConnectorFixture::SetUp();
    auto mockConnection = expectConnection();
    connection = manager->getConnection(connectionProperties);
    EXPECT_CALL(*mockConnection, getMetaData()).WillOnce(Return(&metadata));
    meta = connection->getMetaData();
    ASSERT_NE(nullptr, meta);
  }

  void TearDown() override {
    meta = nullptr;
    connection.reset();
    MockConnectorFixture::TearDown();
  }
};

TEST_F(DbcMetadataTest, TestDatabaseMetaDataGetCatalogs) {
  auto result = newMock<MockResultSet>();
  EXPECT_CALL(metadata, getCatalogs()).WillOnce(transferOwnership(result));
  EXPECT_CALL(*result, rowsCount()).WillOnce(Return(1));
  EXPECT_CALL(*result, next()).WillOnce(Return(true)).WillOnce(Return(false));
  EXPECT_CALL(*result, getString(sql::SQLString("TABLE_CAT"))).WillOnce(Return(sql::SQLString("def")));

  std::unique_ptr<sql::ResultSet> catalogs(meta->getCatalogs());
  ASSERT_NE(nullptr, catalogs.get());
  EXPECT_EQ(1U, catalogs->rowsCount());
  ASSERT_TRUE(catalogs->next());
  EXPECT_EQ(sql::SQLString("def"), catalogs->getString("TABLE_CAT"));
  EXPECT_FALSE(catalogs->next());
}

TEST_F(DbcMetadataTest, TestDatabaseMetaDataGetDatabaseProductName) {
  const sql::SQLString name("MySQL");
  EXPECT_CALL(metadata, getDatabaseProductName()).WillOnce(ReturnRef(name));
  EXPECT_EQ(sql::SQLString("MySQL"), meta->getDatabaseProductName());
}

TEST_F(DbcMetadataTest, TestDatabaseMetaDataGetDatabaseProductVersion) {
  EXPECT_CALL(metadata, getDatabaseProductVersion()).WillOnce(Return(sql::SQLString("8.0.44")));
  EXPECT_EQ(sql::SQLString("8.0.44"), meta->getDatabaseProductVersion());
}

TEST_F(DbcMetadataTest, TestDatabaseMetaDataGetDriverMajorVersion) {
  EXPECT_CALL(metadata, getDriverMajorVersion()).WillOnce(Return(8));
  EXPECT_EQ(8U, meta->getDriverMajorVersion());
}

TEST_F(DbcMetadataTest, TestDatabaseMetaDataGetDriverMinorVersion) {
  EXPECT_CALL(metadata, getDriverMinorVersion()).WillOnce(Return(0));
  EXPECT_EQ(0U, meta->getDriverMinorVersion());
}

TEST_F(DbcMetadataTest, TestDatabaseMetaDataGetDriverName) {
  const sql::SQLString name("MySQL Connector/C++");
  EXPECT_CALL(metadata, getDriverName()).WillOnce(ReturnRef(name));
  EXPECT_EQ(sql::SQLString("MySQL Connector/C++"), meta->getDriverName());
}

TEST_F(DbcMetadataTest, MetadataFailurePropagates) {
  EXPECT_CALL(metadata, getDatabaseProductName())
    .WillOnce(Throw(sql::SQLException("Mock metadata failed", "HY000", 2013)));
  try {
    meta->getDatabaseProductName();
    FAIL() << "Expected metadata failure";
  } catch (const sql::SQLException &error) {
    EXPECT_EQ(2013, error.getErrorCode());
    EXPECT_EQ(sql::SQLString("HY000"), error.getSQLState());
    EXPECT_STREQ("Mock metadata failed", error.what());
  }
}

class DbcMetadataIntegrationTest : public LiveConnectorFixture {
protected:
  sql::DatabaseMetaData *meta = nullptr;

  void SetUp() override {
    LiveConnectorFixture::SetUp();
    if (IsSkipped() || HasFatalFailure())
      return;
    meta = connection->getMetaData();
    ASSERT_NE(nullptr, meta);
  }
};

TEST_F(DbcMetadataIntegrationTest, TestDatabaseMetaDataGetCatalogs) {
  std::unique_ptr<sql::ResultSet> catalogs(meta->getCatalogs());
  ASSERT_NE(nullptr, catalogs.get());
  EXPECT_EQ(1U, catalogs->rowsCount());
  ASSERT_TRUE(catalogs->next());
  EXPECT_EQ(sql::SQLString("def"), catalogs->getString("TABLE_CAT"));
  EXPECT_FALSE(catalogs->next());
}

TEST_F(DbcMetadataIntegrationTest, TestDatabaseMetaDataGetDatabaseProductName) {
  EXPECT_EQ(sql::SQLString("MySQL"), meta->getDatabaseProductName());
}

TEST_F(DbcMetadataIntegrationTest, TestDatabaseMetaDataGetDatabaseProductVersion) {
  std::unique_ptr<sql::Statement> statement(connection->createStatement());
  ASSERT_NE(nullptr, statement.get());
  std::unique_ptr<sql::ResultSet> version(statement->executeQuery("SELECT VERSION()"));
  ASSERT_NE(nullptr, version.get());
  ASSERT_TRUE(version->next());
  EXPECT_EQ(version->getString(1), meta->getDatabaseProductVersion());
}

TEST_F(DbcMetadataIntegrationTest, TestDatabaseMetaDataGetDriverMajorVersion) {
  auto driver = connection->getDriver();
  ASSERT_NE(nullptr, driver);
  EXPECT_EQ(static_cast<unsigned int>(driver->getMajorVersion()), meta->getDriverMajorVersion());
}

TEST_F(DbcMetadataIntegrationTest, TestDatabaseMetaDataGetDriverMinorVersion) {
  auto driver = connection->getDriver();
  ASSERT_NE(nullptr, driver);
  EXPECT_EQ(static_cast<unsigned int>(driver->getMinorVersion()), meta->getDriverMinorVersion());
}

TEST_F(DbcMetadataIntegrationTest, TestDatabaseMetaDataGetDriverName) {
  EXPECT_EQ(sql::SQLString("MySQL Connector/C++"), meta->getDriverName());
}
} // namespace
