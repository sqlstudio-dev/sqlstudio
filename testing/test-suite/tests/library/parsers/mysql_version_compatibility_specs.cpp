#include <gtest/gtest.h>

#include "base/symbol-info.h"
#include "grtdb/db_helpers.h"
#include "mysql/MySQLLexer.h"
#include "mysql/MySQLParser.h"

#include <mysql_driver.h>
#include <cppconn/connection.h>
#include <cppconn/metadata.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>

#include <cstdlib>
#include <memory>

TEST(MySQLCompatibility, ReleaseLinesAndFeatureGates) {
  for (const auto *version : {"5.6.51", "5.7.44", "8.0.36", "8.2.0", "8.4.0",
                             "9.0.0", "9.7.0", "26.7.0", "26.7.1-commercial"}) {
    SCOPED_TRACE(version);
    EXPECT_TRUE(bec::is_supported_mysql_version(version));
    EXPECT_TRUE(bec::is_supported_mysql_version_at_least(version, 5, 6, 0));
  }
  for (const auto *version : {"5.5.62", "6.0.0", "8.5.0", "9.8.0", "10.11.0-MariaDB",
                             "11.4.0-MariaDB", "26.6.0", "26.10.0", "27.7.0", "invalid"}) {
    SCOPED_TRACE(version);
    EXPECT_FALSE(bec::is_supported_mysql_version(version));
    EXPECT_FALSE(bec::is_supported_mysql_version_at_least(version, 8, 0, 22));
  }
  EXPECT_TRUE(bec::is_supported_mysql_version_at_least("26.7.0", 26, 7, 0));
  EXPECT_TRUE(bec::is_supported_mysql_version_at_least("26.7.0", 8, 0, 22));
  EXPECT_FALSE(bec::is_supported_mysql_version_at_least("26.7.0", 26, 7, 1));
  EXPECT_FALSE(bec::is_supported_mysql_version_at_least("8.2.0", 8, 4, 0));
}

TEST(MySQLCompatibility, NewServersRetainSQLVocabulary) {
  for (long version : {80036, 80200, 80400, 90000, 90700, 260700}) {
    SCOPED_TRACE(version);
    auto symbols = base::MySQLSymbolInfo::numberToVersion(version);
    EXPECT_EQ(symbols, base::MySQLVersion::MySQL80);
    EXPECT_TRUE(base::MySQLSymbolInfo::isKeyword("SELECT", symbols));
    EXPECT_FALSE(base::MySQLSymbolInfo::systemFunctionsForVersion(symbols).empty());
  }
  EXPECT_EQ(base::MySQLSymbolInfo::numberToVersion(101100), base::MySQLVersion::Unknown);
}

TEST(MySQLCompatibility, CommonStatementsParseOnNewServers) {
  for (unsigned version : {80200, 80400, 90700, 260700}) {
    for (const auto *query : {
           "SELECT id, JSON_EXTRACT(payload, '$.name') FROM sample WHERE id = 1",
           "WITH cte AS (SELECT 1 AS id) SELECT ROW_NUMBER() OVER (ORDER BY id) FROM cte",
           "CREATE TABLE sample (id BIGINT PRIMARY KEY, payload JSON, name VARCHAR(80))",
           "ALTER TABLE sample ADD COLUMN created_at TIMESTAMP NULL",
           "INSERT INTO sample (id, payload) VALUES (1, JSON_OBJECT('name', 'test'))",
           "UPDATE sample SET payload = JSON_OBJECT('name', 'updated') WHERE id = 1",
           "DELETE FROM sample WHERE id = 1"}) {
      SCOPED_TRACE(version);
      SCOPED_TRACE(query);
      antlr4::ANTLRInputStream input(query);
      parsers::MySQLLexer lexer(&input);
      lexer.serverVersion = version;
      antlr4::CommonTokenStream tokens(&lexer);
      parsers::MySQLParser parser(&tokens);
      parser.serverVersion = version;
      parser.query();
      EXPECT_EQ(lexer.getNumberOfSyntaxErrors(), 0U);
      EXPECT_EQ(parser.getNumberOfSyntaxErrors(), 0U);
    }
  }
}

TEST(MySQLCompatibility, CalendarVersionComments) {
  for (unsigned version : {80200, 90700, 260700}) {
    antlr4::ANTLRInputStream input("SELECT /*!260700 2 + */ 1");
    parsers::MySQLLexer lexer(&input);
    lexer.serverVersion = version;
    antlr4::CommonTokenStream tokens(&lexer);
    tokens.fill();
    bool containsNewVersionExpression = false;
    for (auto *token : tokens.getTokens())
      if (token->getText() == "2")
        containsNewVersionExpression = true;
    EXPECT_EQ(containsNewVersionExpression, version >= 260700);
  }
}

// Opt in with SQLSTUDIO_MYSQL_TEST_URI=tcp://127.0.0.1:33060.
// Uses only a connection-local temporary table in the development schema.
TEST(MySQLCompatibility, LiveConnectorAndTableEditing) {
  const char *uri = std::getenv("SQLSTUDIO_MYSQL_TEST_URI");
  if (!uri)
    GTEST_SKIP() << "Set SQLSTUDIO_MYSQL_TEST_URI to run against a development server";
  const auto env = [](const char *name, const char *fallback) {
    const char *value = std::getenv(name);
    return value ? value : fallback;
  };
  std::unique_ptr<sql::Connection> connection(sql::mysql::get_mysql_driver_instance()->connect(
    uri, env("SQLSTUDIO_MYSQL_TEST_USER", "sqlstudio"), env("SQLSTUDIO_MYSQL_TEST_PASSWORD", "sqlstudio")));
  connection->setSchema(env("SQLSTUDIO_MYSQL_TEST_SCHEMA", "sqlstudio"));
  std::unique_ptr<sql::Statement> statement(connection->createStatement());
  std::unique_ptr<sql::ResultSet> version(statement->executeQuery("SELECT VERSION()"));
  ASSERT_TRUE(version->next());
  EXPECT_TRUE(bec::is_supported_mysql_version(version->getString(1)));
  std::cout << "Connected through Connector/C++ to MySQL " << version->getString(1) << '\n';
  statement->execute("CREATE TEMPORARY TABLE sqlstudio_compatibility_test "
                     "(id BIGINT PRIMARY KEY, payload JSON, name VARCHAR(80))");
  statement->execute("INSERT INTO sqlstudio_compatibility_test VALUES "
                     "(1, JSON_OBJECT('name', 'test'), 'before')");
  statement->execute("ALTER TABLE sqlstudio_compatibility_test ADD COLUMN note VARCHAR(80)");
  statement->execute("UPDATE sqlstudio_compatibility_test SET name = 'after' WHERE id = 1");
  std::unique_ptr<sql::ResultSet> rows(statement->executeQuery(
    "SELECT name, JSON_UNQUOTE(JSON_EXTRACT(payload, '$.name')) FROM sqlstudio_compatibility_test"));
  ASSERT_TRUE(rows->next());
  EXPECT_EQ(rows->getString(1), "after");
  EXPECT_EQ(rows->getString(2), "test");
  std::unique_ptr<sql::ResultSet> ddl(statement->executeQuery("SHOW CREATE TABLE sqlstudio_compatibility_test"));
  ASSERT_TRUE(ddl->next());
  EXPECT_NE(ddl->getString(2).asStdString().find("`note`"), std::string::npos);
  antlr4::ANTLRInputStream input(ddl->getString(2).asStdString());
  parsers::MySQLLexer lexer(&input);
  lexer.serverVersion = connection->getMetaData()->getDatabaseMajorVersion() * 10000 +
                        connection->getMetaData()->getDatabaseMinorVersion() * 100;
  antlr4::CommonTokenStream tokens(&lexer);
  parsers::MySQLParser parser(&tokens);
  parser.serverVersion = lexer.serverVersion;
  parser.query();
  EXPECT_EQ(parser.getNumberOfSyntaxErrors(), 0U);
  for (const auto *query : {"SHOW DATABASES", "SHOW FULL TABLES", "SHOW COLLATION",
                            "SHOW FULL COLUMNS FROM sqlstudio_compatibility_test",
                            "SHOW INDEXES FROM sqlstudio_compatibility_test",
                            "SHOW PROCEDURE STATUS WHERE Db = DATABASE()",
                            "SHOW FUNCTION STATUS WHERE Db = DATABASE()",
                            "EXPLAIN FORMAT=JSON SELECT * FROM sqlstudio_compatibility_test",
                            "SHOW SESSION STATUS LIKE 'Ssl_cipher'",
                            "SELECT COLUMN_NAME, DATA_TYPE FROM information_schema.COLUMNS "
                            "WHERE TABLE_SCHEMA = DATABASE()"}) {
    std::unique_ptr<sql::ResultSet> metadata(statement->executeQuery(query));
    EXPECT_NE(metadata, nullptr);
  }
  statement->execute("DELETE FROM sqlstudio_compatibility_test WHERE id = 1");
  statement->execute("DROP TEMPORARY TABLE sqlstudio_compatibility_test");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
