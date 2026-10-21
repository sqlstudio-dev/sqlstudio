#include <gtest/gtest.h>

#include "base/symbol-info.h"
#include "base/string_utilities.h"
#include "base/sqlstring.h"
#include "grtdb/db_helpers.h"
#include "mysql/MySQLLexer.h"
#include "mysql/MySQLParser.h"

#include <mysql_driver.h>
#include <cppconn/connection.h>
#include <cppconn/metadata.h>
#include <cppconn/resultset.h>
#include <cppconn/statement.h>
#include <cppconn/exception.h>

#include <cstdlib>
#include <memory>

TEST(MySQLCompatibility, ReleaseLinesAndFeatureGates) {
  for (const auto *version : {"5.6.51", "5.7.44", "8.0.36", "8.2.0", "8.4.0",
                             "9.0.0", "9.7.0", "26.7.0", "26.7.1-commercial", "26.10.0", "9.7.2", "8.4.13", "8.0.46"}) {
    SCOPED_TRACE(version);
    EXPECT_TRUE(bec::is_supported_mysql_version(version));
    EXPECT_TRUE(bec::is_supported_mysql_version_at_least(version, 5, 6, 0));
  }
  for (const auto *version : {"5.5.62", "6.0.0", "8.5.0", "9.8.0", "10.11.0-MariaDB",
                             "11.4.0-MariaDB", "26.6.0", "26.11.0", "27.7.0", "invalid"}) {
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
  for (long version : {80036, 80200, 80400, 90000, 90700, 260700, 261000}) {
    SCOPED_TRACE(version);
    auto symbols = base::MySQLSymbolInfo::numberToVersion(version);
    EXPECT_EQ(symbols, version >= 80200 ? base::MySQLVersion::MySQL82 : base::MySQLVersion::MySQL8031);
    EXPECT_TRUE(base::MySQLSymbolInfo::isKeyword("SELECT", symbols));
    EXPECT_FALSE(base::MySQLSymbolInfo::systemFunctionsForVersion(symbols).empty());
  }
  EXPECT_EQ(base::MySQLSymbolInfo::numberToVersion(101100), base::MySQLVersion::Unknown);
}

TEST(MySQLCompatibility, CommonStatementsParseOnNewServers) {
  for (unsigned version : {80200, 80400, 90700, 260700, 261000}) {
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
  for (unsigned version : {80200, 90700, 260700, 261000}) {
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

namespace {
size_t syntaxErrors(const std::string &query, unsigned version) {
  antlr4::ANTLRInputStream input(query);
  parsers::MySQLLexer lexer(&input);
  lexer.serverVersion = version;
  lexer.removeErrorListeners();
  antlr4::CommonTokenStream tokens(&lexer);
  parsers::MySQLParser parser(&tokens);
  parser.serverVersion = version;
  parser.removeErrorListeners();
  parser.query();
  return lexer.getNumberOfSyntaxErrors() + parser.getNumberOfSyntaxErrors();
}
}

TEST(MySQLCompatibility, ModernSyntaxAndVersionBoundaries) {
  struct Example {
    unsigned introduced;
    const char *sql;
  };
  const Example examples[] = {
    {80021, R"(SELECT JSON_VALUE('{"n": 7}', '$.n' RETURNING UNSIGNED))"},
    {80021, R"(SELECT JSON_VALUE('{}', '$.n' RETURNING DECIMAL(10,2) DEFAULT -1.5 ON EMPTY ERROR ON ERROR))"},
    {80021, R"(SELECT JSON_VALUE('{}', '$.n' RETURNING CHAR(20) CHARACTER SET utf8mb4 NULL ON EMPTY))"},
    {80022, R"(SELECT JSON_VALUE('{"y": 2026}', '$.y' RETURNING YEAR))"},
    {80022, "SHOW REPLICA STATUS FOR CHANNEL 'test'"},
    {80022, "SHOW REPLICAS"},
    {80022, "START REPLICA SQL_THREAD UNTIL SQL_AFTER_GTIDS = 'uuid:1' FOR CHANNEL 'test'"},
    {80022, "STOP REPLICA IO_THREAD FOR CHANNEL 'test'"},
    {80022, "RESET REPLICA ALL FOR CHANNEL 'test'"},
    {80023, "CHANGE REPLICATION SOURCE TO SOURCE_HOST='localhost', SOURCE_PORT=3306, "
            "SOURCE_USER='repl', SOURCE_PASSWORD='test', SOURCE_AUTO_POSITION=1 FOR CHANNEL 'test'"},
    {80023, "CHANGE REPLICATION SOURCE TO SOURCE_SSL=1, SOURCE_TLS_VERSION='TLSv1.3', "
            "SOURCE_TLS_CIPHERSUITES=NULL, SOURCE_HEARTBEAT_PERIOD=0.5, GET_SOURCE_PUBLIC_KEY=1"},
    {80023, "START REPLICA UNTIL SOURCE_LOG_FILE='binlog.000001', SOURCE_LOG_POS=4"},
    {80031, "SELECT 1 INTERSECT SELECT 1"},
    {80031, "SELECT 1 EXCEPT ALL SELECT 2"},
    {80031, "(SELECT 1 UNION ALL SELECT 2) INTERSECT DISTINCT (SELECT 2 ORDER BY 1 LIMIT 1)"},
    {80031, "WITH c AS (SELECT 1 AS n) SELECT n FROM c EXCEPT SELECT 2 ORDER BY n"},
    {80200, "SHOW BINARY LOG STATUS"},
    {80200, "RESET BINARY LOGS AND GTIDS TO 42"},
  };
  for (const auto &example : examples) {
    SCOPED_TRACE(example.sql);
    EXPECT_GT(syntaxErrors(example.sql, example.introduced - 1), 0U);
    for (unsigned version : {example.introduced, 80200U, 260700U, 261000U}) {
      SCOPED_TRACE(version);
      EXPECT_EQ(syntaxErrors(example.sql, version), 0U);
    }
  }
}

TEST(MySQLCompatibility, RejectMalformedModernSyntax) {
  for (const auto *sql : {
         "SELECT 1 INTERSECT",
         "SELECT 1 EXCEPT ALL DISTINCT SELECT 2",
         "SHOW REPLICA",
         "CHANGE REPLICATION SOURCE TO SOURCE_HOST=",
         "CHANGE REPLICATION SOURCE TO MASTER_HOST='localhost'",
         R"(SELECT JSON_VALUE('{}', '$.x' RETURNING))",
         R"(SELECT JSON_VALUE('{}', '$.x' RETURNING INT))",
         R"(SELECT JSON_VALUE('{}', '$.x' NULL ON ERROR NULL ON EMPTY))",
         R"(SELECT JSON_VALUE('{}', '$.x' DEFAULT ON EMPTY))"}) {
    SCOPED_TRACE(sql);
    EXPECT_GT(syntaxErrors(sql, 260700), 0U);
  }
}

TEST(MySQLCompatibility, SetOperationPrecedence) {
  antlr4::ANTLRInputStream input("SELECT 1 UNION SELECT 2 INTERSECT SELECT 3 EXCEPT SELECT 4");
  parsers::MySQLLexer lexer(&input);
  lexer.serverVersion = 260700;
  antlr4::CommonTokenStream tokens(&lexer);
  parsers::MySQLParser parser(&tokens);
  parser.serverVersion = 260700;
  auto *body = parser.queryExpression()->queryExpressionBody();
  ASSERT_NE(body, nullptr);
  ASSERT_EQ(body->queryExpressionTerm().size(), 3U);
  EXPECT_EQ(body->queryExpressionTerm(1)->INTERSECT_SYMBOL().size(), 1U);
  EXPECT_EQ(body->queryExpressionTerm(1)->queryExpressionPrimary().size(), 2U);
  EXPECT_EQ(parser.getNumberOfSyntaxErrors(), 0U);
}

TEST(MySQLCompatibility, KeywordsAndIdentifiersRespectVersionBoundaries) {
  using base::MySQLSymbolInfo;
  EXPECT_FALSE(MySQLSymbolInfo::isKeyword("INTERSECT", MySQLSymbolInfo::numberToVersion(80030)));
  EXPECT_TRUE(MySQLSymbolInfo::isReservedKeyword("INTERSECT", MySQLSymbolInfo::numberToVersion(80031)));
  EXPECT_TRUE(MySQLSymbolInfo::isReservedKeyword("SELECT", MySQLSymbolInfo::numberToVersion(260700)));
  EXPECT_FALSE(MySQLSymbolInfo::isReservedKeyword("REPLICA", MySQLSymbolInfo::numberToVersion(260700)));
  EXPECT_EQ(base::quoteIdentifierIfNeeded("intersect", '`', MySQLSymbolInfo::numberToVersion(80030)), "intersect");
  EXPECT_EQ(base::quoteIdentifierIfNeeded("intersect", '`', MySQLSymbolInfo::numberToVersion(80031)), "`intersect`");
  EXPECT_EQ(base::quoteIdentifierIfNeeded("rEpLiCa", '`', MySQLSymbolInfo::numberToVersion(260700)), "rEpLiCa");
  EXPECT_EQ(std::string(base::sqlstring("SELECT * FROM !", base::QuoteOnlyIfNeeded) << "intersect"),
            "SELECT * FROM `intersect`");
  EXPECT_EQ(MySQLSymbolInfo::systemFunctionsForVersion(MySQLSymbolInfo::numberToVersion(80021)).count("json_value"), 1U);
  for (unsigned version : {50744, 80020, 80200, 260700, 261000}) {
    SCOPED_TRACE(version);
    EXPECT_EQ(syntaxErrors("SELECT replica, returning, source_host, json_value FROM sample", version), 0U);
    EXPECT_EQ(syntaxErrors("SELECT `intersect`, `except` FROM sample", version), 0U);
    EXPECT_EQ(syntaxErrors("SELECT 1 UNION ALL SELECT 2", version), 0U);
  }
  EXPECT_EQ(syntaxErrors("SELECT intersect FROM sample", 80030), 0U);
  EXPECT_GT(syntaxErrors("SELECT intersect FROM sample", 80031), 0U);
  EXPECT_EQ(syntaxErrors("SHOW SLAVE STATUS", 50744), 0U);
  EXPECT_EQ(syntaxErrors("CHANGE MASTER TO MASTER_HOST='localhost'", 50744), 0U);

  antlr4::ANTLRInputStream input("");
  parsers::MySQLLexer lexer(&input);
  lexer.serverVersion = 260700;
  EXPECT_EQ(lexer.keywordFromText("intersect"), parsers::MySQLLexer::INTERSECT_SYMBOL);
  EXPECT_EQ(lexer.keywordFromText("Source_Host"), parsers::MySQLLexer::SOURCE_HOST_SYMBOL);
  EXPECT_FALSE(lexer.isIdentifier(parsers::MySQLLexer::INTERSECT_SYMBOL));
  EXPECT_FALSE(lexer.isIdentifier(parsers::MySQLLexer::SELECT_SYMBOL));
  EXPECT_FALSE(lexer.isIdentifier(parsers::MySQLLexer::PLUS_OPERATOR));
  EXPECT_TRUE(lexer.isIdentifier(parsers::MySQLLexer::REPLICA_SYMBOL));
  lexer.serverVersion = 80030;
  EXPECT_NE(lexer.keywordFromText("intersect"), parsers::MySQLLexer::INTERSECT_SYMBOL);
}

TEST(MySQLCompatibility, ModernReplicationQueryClassification) {
  const std::pair<const char *, MySQLQueryType> examples[] = {
    {"SHOW REPLICA STATUS", QtShowSlaveStatus},
    {"SHOW REPLICAS", QtShowSlaveHosts},
    {"RESET REPLICA ALL", QtResetSlave},
    {"START REPLICA", QtStartSlave},
    {"STOP REPLICA", QtStopSlave},
    {"CHANGE REPLICATION SOURCE TO SOURCE_HOST='localhost'", QtChangeMaster},
    {"SHOW BINARY LOG STATUS", QtShowMasterStatus},
    {"SHOW BINARY LOGS", QtShowBinaryLogs},
    {"RESET BINARY LOGS AND GTIDS", QtResetMaster}
  };
  for (const auto &example : examples) {
    SCOPED_TRACE(example.first);
    antlr4::ANTLRInputStream input(example.first);
    parsers::MySQLLexer lexer(&input);
    lexer.serverVersion = 260700;
    EXPECT_EQ(lexer.determineQueryType(), example.second);
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
  const std::pair<const char *, std::vector<std::string>> modernQueries[] = {
    {"SELECT 1 AS n UNION SELECT 2 INTERSECT SELECT 2 ORDER BY n", {"1", "2"}},
    {"SELECT 1 UNION ALL SELECT 1 EXCEPT ALL SELECT 1", {"1"}},
    {"(SELECT 1 UNION ALL SELECT 1) INTERSECT ALL (SELECT 1)", {"1"}},
    {R"(SELECT JSON_VALUE('{"n": 7}', '$.n' RETURNING UNSIGNED))", {"7"}},
    {R"(SELECT JSON_VALUE('{}', '$.n' RETURNING SIGNED DEFAULT -1 ON EMPTY ERROR ON ERROR))", {"-1"}},
    {R"(SELECT JSON_VALUE('{"y": 2026}', '$.y' RETURNING YEAR))", {"2026"}}
  };
  for (const auto &query : modernQueries) {
    SCOPED_TRACE(query.first);
    EXPECT_EQ(syntaxErrors(query.first, lexer.serverVersion), 0U);
    std::unique_ptr<sql::ResultSet> result(statement->executeQuery(query.first));
    std::vector<std::string> actual;
    while (result->next())
      actual.push_back(result->getString(1));
    EXPECT_EQ(actual, query.second);
  }
  for (const auto *query : {"SHOW REPLICA STATUS", "SHOW REPLICAS", "SHOW BINARY LOG STATUS"}) {
    SCOPED_TRACE(query);
    EXPECT_EQ(syntaxErrors(query, lexer.serverVersion), 0U);
    try {
      std::unique_ptr<sql::ResultSet> result(statement->executeQuery(query));
      EXPECT_NE(result, nullptr);
    } catch (const sql::SQLException &error) {
      // The development account need not have replication-monitoring privileges.
      // Syntax errors must still fail this test. Never execute replication mutations.
      EXPECT_TRUE(error.getErrorCode() == 1227 || error.getErrorCode() == 1142) << error.what();
    }
  }
  statement->execute("DELETE FROM sqlstudio_compatibility_test WHERE id = 1");
  statement->execute("DROP TEMPORARY TABLE sqlstudio_compatibility_test");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
