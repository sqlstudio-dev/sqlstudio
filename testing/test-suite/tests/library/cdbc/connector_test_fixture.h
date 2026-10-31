#pragma once

#include "connector_mocks.h"
#include "cdbc/src/driver_manager.h"
#include "wb_test_helpers.h"

#include <functional>
#include <memory>

namespace sql {
namespace test {

template <typename T>
::testing::StrictMock<T> *newMock() {
  auto mock = new ::testing::StrictMock<T>();
  EXPECT_CALL(*mock, destroyed());
  return mock;
}

// Keep unconsumed results owned if connection initialization fails before requesting them.
template <typename T>
auto transferOwnership(T *mock) {
  auto owner = std::make_shared<std::unique_ptr<T>>(mock);
  return [owner] { return owner->release(); };
}

class MockConnectorFixture : public ::testing::Test {
protected:
  std::unique_ptr<SqlStudioTester> tester;
  ::testing::StrictMock<MockDriver> driver;
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
    EXPECT_CALL(driver, getMajorVersion()).WillRepeatedly(::testing::Return(8));
    EXPECT_CALL(driver, getMinorVersion()).WillRepeatedly(::testing::Return(0));
    EXPECT_CALL(driver, getPatchVersion()).WillRepeatedly(::testing::Return(42));
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
    EXPECT_CALL(driver, connect(::testing::A<sql::ConnectOptionsMap &>()))
      .WillOnce([takeConnection, inspectOptions](sql::ConnectOptionsMap &options) -> sql::Connection * {
        if (inspectOptions)
          inspectOptions(options);
        return takeConnection();
      }).RetiresOnSaturation();

    auto userStatement = newMock<MockStatement>();
    auto userResult = newMock<MockResultSet>();
    EXPECT_CALL(*userStatement, executeQuery(sql::SQLString("SELECT current_user()")))
      .WillOnce(transferOwnership(userResult));
    EXPECT_CALL(*userResult, next()).WillOnce(::testing::Return(true));
    EXPECT_CALL(*userResult, getString(uint32_t(1))).WillOnce(::testing::Return(sql::SQLString("test_user@mock")));

    auto charsetStatement = newMock<MockStatement>();
    auto charsetResult = newMock<MockResultSet>();
    EXPECT_CALL(*charsetStatement, executeQuery(sql::SQLString("show character set where charset = 'utf8mb4'")))
      .WillOnce(transferOwnership(charsetResult));
    EXPECT_CALL(*charsetResult, rowsCount()).WillOnce(::testing::Return(utf8mb4 ? 1 : 0));
    EXPECT_CALL(*charsetStatement, executeUpdate(sql::SQLString(utf8mb4 ? "SET NAMES 'utf8mb4'" : "SET NAMES 'utf8'")))
      .WillOnce(::testing::Return(0));
    EXPECT_CALL(*connection, createStatement())
      .WillOnce(transferOwnership(userStatement))
      .WillOnce(transferOwnership(charsetStatement));
    return connection;
  }
};

} // namespace test
} // namespace sql
