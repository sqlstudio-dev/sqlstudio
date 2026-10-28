// Incremental keyword additions implemented by SqlStudio's ANTLR grammar.
// Keep introduction versions synchronized with MySQLLexer.g4.
#pragma once

struct MySQLKeywordUpdate {
  const char *word;
  int introduced;
  bool reserved;
};

static const MySQLKeywordUpdate keyword_updates80[] = {
  {"JSON_VALUE", 80021, false},
  {"RETURNING", 80021, false},
  {"REPLICA", 80022, false},
  {"REPLICAS", 80022, false},
  {"GET_SOURCE_PUBLIC_KEY", 80023, false},
  {"SOURCE_AUTO_POSITION", 80023, false},
  {"SOURCE_BIND", 80023, false},
  {"SOURCE_COMPRESSION_ALGORITHMS", 80023, false},
  {"SOURCE_CONNECTION_AUTO_FAILOVER", 80023, false},
  {"SOURCE_CONNECT_RETRY", 80023, false},
  {"SOURCE_DELAY", 80023, false},
  {"SOURCE_HEARTBEAT_PERIOD", 80023, false},
  {"SOURCE_HOST", 80023, false},
  {"SOURCE_LOG_FILE", 80023, false},
  {"SOURCE_LOG_POS", 80023, false},
  {"SOURCE_PASSWORD", 80023, false},
  {"SOURCE_PORT", 80023, false},
  {"SOURCE_PUBLIC_KEY_PATH", 80023, false},
  {"SOURCE_RETRY_COUNT", 80023, false},
  {"SOURCE_SSL_CAPATH", 80023, false},
  {"SOURCE_SSL_CA", 80023, false},
  {"SOURCE_SSL_CERT", 80023, false},
  {"SOURCE_SSL_CIPHER", 80023, false},
  {"SOURCE_SSL_CRLPATH", 80023, false},
  {"SOURCE_SSL_CRL", 80023, false},
  {"SOURCE_SSL_KEY", 80023, false},
  {"SOURCE_SSL", 80023, false},
  {"SOURCE_SSL_VERIFY_SERVER_CERT", 80023, false},
  {"SOURCE_TLS_CIPHERSUITES", 80023, false},
  {"SOURCE_TLS_VERSION", 80023, false},
  {"SOURCE_USER", 80023, false},
  {"SOURCE_ZSTD_COMPRESSION_LEVEL", 80023, false},
  {"INTERSECT", 80031, true},
  {"LOG", 80200, false},
  {"GTIDS", 80200, false},
};
