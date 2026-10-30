/*
 * Copyright (c) 2025, dev4fun. All rights reserved.
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

#include "context.h"
#include <fstream>
#include <stdexcept>
#include <filesystem>
#include "rapidjson/document.h"

// Helper: load a JSON file if present.
void load_json_config(const std::string& path, rapidjson::Document& doc) {
  std::ifstream in(path);
  if (!in.is_open())
    return; // Silently ignore missing config.
  std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (content.empty())
    return;
  doc.Parse(content.c_str());
  if (doc.HasParseError()) {
    // Reset to empty object on parse error.
    doc.SetObject();
  }
}

namespace testing {

  Context::Context() {
    _baseDir = std::filesystem::absolute("..").lexically_normal().u8string();
    std::filesystem::create_directories(outputDir());
    auto* doc = new rapidjson::Document(rapidjson::kObjectType);
    load_json_config(_baseDir + "/test-config.json", *doc);
    _configuration_impl = doc;
  }

  // Walks a dotted path like "a.b.c" into the configuration document.
  static const rapidjson::Value* lookupPath(const rapidjson::Document* doc, const std::string& path) {
    if (!doc || !doc->IsObject())
      return nullptr;
    const rapidjson::Value* node = doc;
    size_t start = 0;
    while (start <= path.size()) {
      size_t dot = path.find('.', start);
      std::string key = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
      if (!node->IsObject() || !node->HasMember(key.c_str()))
        return nullptr;
      node = &(*node)[key.c_str()];
      if (dot == std::string::npos)
        break;
      start = dot + 1;
    }
    return node;
  }

  std::string Context::getConfigurationStringValue(std::string const& path, std::string const& defaultValue) const {
    auto doc = static_cast<const rapidjson::Document*>(_configuration_impl);
    if (auto v = lookupPath(doc, path)) {
      if (v->IsString())
        return v->GetString();
    }
    auto it = settings.find(path);
    if (it != settings.end())
      return it->second;
    return defaultValue;
  }

  int Context::getConfigurationIntValue(std::string const& path, int defaultValue) const {
    auto doc = static_cast<const rapidjson::Document*>(_configuration_impl);
    if (auto v = lookupPath(doc, path)) {
      if (v->IsInt())
        return v->GetInt();
      if (v->IsInt64())
        return static_cast<int>(v->GetInt64());
      if (v->IsDouble())
        return static_cast<int>(v->GetDouble());
    }
    auto it = settings.find(path);
    if (it != settings.end()) {
      try {
        return std::stoi(it->second);
      } catch (...) {
      }
    }
    return defaultValue;
  }

  double Context::getConfigurationDoubleValue(std::string const& path, double defaultValue) const {
    auto doc = static_cast<const rapidjson::Document*>(_configuration_impl);
    if (auto v = lookupPath(doc, path)) {
      if (v->IsDouble())
        return v->GetDouble();
      if (v->IsInt())
        return static_cast<double>(v->GetInt());
      if (v->IsInt64())
        return static_cast<double>(v->GetInt64());
    }
    auto it = settings.find(path);
    if (it != settings.end()) {
      try {
        return std::stod(it->second);
      } catch (...) {
      }
    }
    return defaultValue;
  }

  bool Context::getConfigurationBoolValue(std::string const& path, bool defaultValue) const {
    auto doc = static_cast<const rapidjson::Document*>(_configuration_impl);
    if (auto v = lookupPath(doc, path)) {
      if (v->IsBool())
        return v->GetBool();
      if (v->IsInt())
        return v->GetInt() != 0;
    }
    auto it = settings.find(path);
    if (it != settings.end()) {
      std::string s = it->second;
      for (auto& c : s)
        c = static_cast<char>(tolower(c));
      if (s == "true" || s == "1" || s == "yes" || s == "on")
        return true;
      if (s == "false" || s == "0" || s == "no" || s == "off")
        return false;
    }
    return defaultValue;
  }

  Context::~Context() {
    delete static_cast<rapidjson::Document*>(_configuration_impl);
  }

  Context& Context::get() {
    static Context instance; // Thread-safe in C++11 and later.
    return instance;
  }

} // namespace testing
