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

#include "base/symbol-info.h"

#include "server/system-functions.h"

using namespace base;

#include "server/keyword_list56.h"
#include "server/keyword_list57.h"
#include "server/keyword_list80.h"
#include "server/keyword_list80_updates.h"
#include <algorithm>
#include <cctype>
#include <map>

//----------------------------------------------------------------------------------------------------------------------

static std::set<std::string> empty;

static std::string normalizedKeyword(std::string word) {
  std::transform(word.begin(), word.end(), word.begin(), [](unsigned char c) { return std::toupper(c); });
  return word;
}

static int keywordVersion(MySQLVersion version) {
  switch (version) {
    case MySQLVersion::MySQL82: return 80200;
    case MySQLVersion::MySQL8021: return 80021;
    case MySQLVersion::MySQL8022: return 80022;
    case MySQLVersion::MySQL8023: return 80023;
    case MySQLVersion::MySQL8031: return 80031;
    default: return 80000;
  }
}

std::set<std::string> const& MySQLSymbolInfo::systemFunctionsForVersion(MySQLVersion version) {
  static const auto modernFunctions = [] {
    auto functions = systemFunctions80;
    functions.insert("json_value");
    return functions;
  }();
  switch (version) {
    case MySQLVersion::MySQL56:
      return systemFunctions56;
    case MySQLVersion::MySQL57:
      return systemFunctions57;
    case MySQLVersion::MySQL80:
      return systemFunctions80;
    case MySQLVersion::MySQL8021:
    case MySQLVersion::MySQL8022:
    case MySQLVersion::MySQL8023:
    case MySQLVersion::MySQL8031:
    case MySQLVersion::MySQL82:
      return modernFunctions;

    default:
      return empty;
  }
}

//----------------------------------------------------------------------------------------------------------------------

static std::map<MySQLVersion, std::set<std::string>> keywords;
static std::map<MySQLVersion, std::set<std::string>> reservedKeywords;
std::set<std::string> const& MySQLSymbolInfo::keywordsForVersion(MySQLVersion version) {
  if (keywords.count(version) == 0) {
    std::set<std::string> list;
    std::set<std::string> reservedList;
    switch (version) {
      case MySQLVersion::MySQL56: {
        size_t listSize = sizeof(keyword_list56) / sizeof(keyword_list56[0]);
        for (size_t i = 0; i < listSize; ++i) {
          std::string word = keyword_list56[i].word;
          list.insert(word);
          if (keyword_list56[i].reserved != 0)
            reservedList.insert(word);
        }
        break;
      }

      case MySQLVersion::MySQL57: {
        size_t listSize = sizeof(keyword_list57) / sizeof(keyword_list57[0]);
        for (size_t i = 0; i < listSize; ++i) {
          std::string word = keyword_list57[i].word;
          list.insert(word);
          if (keyword_list57[i].reserved != 0)
            reservedList.insert(word);
        }
        break;
      }

      case MySQLVersion::MySQL80:
      case MySQLVersion::MySQL8021:
      case MySQLVersion::MySQL8022:
      case MySQLVersion::MySQL8023:
      case MySQLVersion::MySQL8031:
      case MySQLVersion::MySQL82: {
        size_t listSize = sizeof(keyword_list80) / sizeof(keyword_list80[0]);
        for (size_t i = 0; i < listSize; ++i) {
          std::string word = keyword_list80[i].word;
          list.insert(word);
          if (keyword_list80[i].reserved != 0)
            reservedList.insert(word);
        }
        for (const auto &keyword : keyword_updates80) {
          if (keyword.introduced <= keywordVersion(version)) {
            list.insert(keyword.word);
            if (keyword.reserved)
              reservedList.insert(keyword.word);
          }
        }
        // MEMBER became non-reserved in 8.0.19, before these additions.
        if (version != MySQLVersion::MySQL80)
          reservedList.erase("MEMBER");
        break;
      }

      default:
        break;
    }
    keywords[version] = list;
    reservedKeywords[version] = reservedList;
  }

  return keywords[version];
}

//----------------------------------------------------------------------------------------------------------------------

bool MySQLSymbolInfo::isReservedKeyword(std::string const& identifier, MySQLVersion version) {
  std::ignore = keywordsForVersion(version);
  return reservedKeywords[version].count(normalizedKeyword(identifier)) > 0;
}

//----------------------------------------------------------------------------------------------------------------------

/**
 * For both, reserved and non-reserved keywords.
 */
bool MySQLSymbolInfo::isKeyword(std::string const& identifier, MySQLVersion version) {
  const auto &keywords = keywordsForVersion(version);
  return keywords.count(normalizedKeyword(identifier)) > 0;
}

//----------------------------------------------------------------------------------------------------------------------

MySQLVersion MySQLSymbolInfo::numberToVersion(long version) {
  long major = version / 10000, minor = (version / 100) % 100;

  if (!isSupportedServerVersion(major, minor))
    return MySQLVersion::Unknown;

  // Newer releases share the implemented SQL vocabulary, without claiming
  // support for every statement introduced by those releases.
  if (major >= 8) {
    if (version >= 80200)
      return MySQLVersion::MySQL82;
    if (version >= 80031)
      return MySQLVersion::MySQL8031;
    if (version >= 80023)
      return MySQLVersion::MySQL8023;
    if (version >= 80022)
      return MySQLVersion::MySQL8022;
    if (version >= 80021)
      return MySQLVersion::MySQL8021;
    return MySQLVersion::MySQL80;
  }

  if (major != 5)
    return MySQLVersion::Unknown;

  switch (minor) {
    case 6:
      return MySQLVersion::MySQL56;
    case 7:
      return MySQLVersion::MySQL57;
    default:
      return MySQLVersion::Unknown;
  }
}

//----------------------------------------------------------------------------------------------------------------------

bool MySQLSymbolInfo::isSupportedServerVersion(int major, int minor) {
  // Keep in sync with studio.utils.Version.is_supported_mysql_version.
  // Explicit release lines avoid treating MariaDB or unknown future servers
  // as MySQL merely because their version number is larger.
  return (major == 5 && (minor == 6 || minor == 7)) ||
         (major == 8 && minor >= 0 && minor <= 4) ||
         (major == 9 && minor >= 0 && minor <= 7) ||
         (major == 26 && (minor == 7 || minor == 10));
}
