// Copyright (c) 2026 dev4fun. All rights reserved.
// Licensed under the GNU General Public License, version 2.0.

// Verifies that the third-party bundle produced by build\windows\build_3rdparty.ps1 was compiled and
// linked with the same C runtime switches SqlStudio uses (/MD vs /MDd, _ITERATOR_DEBUG_LEVEL,
// RuntimeLibrary, target machine). This test binary is built with the SqlStudio property sheets, so
// its own compile-time settings are the reference.
//
// Static libraries are checked through the linker directives (/DEFAULTLIB, /FAILIFMISMATCH) stored in
// every object's .drectve section. Import libraries are checked through the Visual C++ runtime DLLs
// imported by the DLL they refer to.

#include "gtest/gtest.h"

#ifdef _MSC_VER

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ostream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

#define WB_STRINGIFY_(x) #x
#define WB_STRINGIFY(x) WB_STRINGIFY_(x)

struct RuntimeFlavor {
  bool debug;
  bool dynamic;

  bool operator==(const RuntimeFlavor &other) const {
    return debug == other.debug && dynamic == other.dynamic;
  }
  bool operator!=(const RuntimeFlavor &other) const {
    return !(*this == other);
  }
};

const char *describe(RuntimeFlavor flavor) {
  if (flavor.dynamic)
    return flavor.debug ? "/MDd" : "/MD";
  return flavor.debug ? "/MTd" : "/MT";
}

std::ostream &operator<<(std::ostream &out, RuntimeFlavor flavor) {
  return out << describe(flavor);
}

// Reference values: what this binary, and therefore SqlStudio, was compiled with.
constexpr RuntimeFlavor kOwnFlavor{
#ifdef _DEBUG
  true,
#else
  false,
#endif
#ifdef _DLL
  true
#else
  false
#endif
};

const std::string kOwnIteratorDebugLevel = WB_STRINGIFY(_ITERATOR_DEBUG_LEVEL);
const std::string kOwnRuntimeLibraryTag = kOwnFlavor.dynamic ? (kOwnFlavor.debug ? "MDd_DynamicDebug" : "MD_DynamicRelease")
                                                             : (kOwnFlavor.debug ? "MTd_StaticDebug" : "MT_StaticRelease");
#if _MSC_VER >= 1900
const std::string kOwnMscVer = "1900"; // All VS 2015+ toolsets share this ABI tag.
#else
const std::string kOwnMscVer = WB_STRINGIFY(_MSC_VER);
#endif

#if defined(_M_X64)
constexpr WORD kOwnMachine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
constexpr WORD kOwnMachine = IMAGE_FILE_MACHINE_ARM64;
#else
constexpr WORD kOwnMachine = IMAGE_FILE_MACHINE_I386;
#endif

// ClassID of ANON_OBJECT_HEADER_BIGOBJ members (/bigobj); winnt.h only documents it in a comment.
const GUID kBigObjClassId = {0xD1BAA1C7, 0xBAEE, 0x4BA9, {0xAF, 0x20, 0xFA, 0xF6, 0x6A, 0xA4, 0xDC, 0xB8}};

//----------------------------------------------------------------------------------------------------------------------

// Third-party libraries SqlStudio links, as listed in vsprops\*.props and the vcxproj AdditionalDependencies.
struct LinkedLibrary {
  const char *name;
  const char *libDir; // Relative to MSS_3DPARTY_PATH, mirroring AdditionalLibraryDirectories.
  const char *origin;
};

std::ostream &operator<<(std::ostream &out, const LinkedLibrary &library) {
  return out << library.libDir << "\\" << library.name << " (" << library.origin << ")";
}

#ifdef _DEBUG
#define WB_CONFIG_LIB(debugName, releaseName) debugName
const char *const kConfigLibDir = "debug\\lib";
const char *const kConfigBinDir = "debug\\bin";
#else
#define WB_CONFIG_LIB(debugName, releaseName) releaseName
const char *const kConfigLibDir = "lib";
const char *const kConfigBinDir = "bin";
#endif

const LinkedLibrary kLinkedLibraries[] = {
  {"antlr4-runtime.lib", kConfigLibDir, "vsprops/wb_antlr4.props"},
  {"cairo.lib", kConfigLibDir, "vsprops/wb_cairo.props"},
  {WB_CONFIG_LIB("gdald.lib", "gdal.lib"), kConfigLibDir, "vsprops/wb_gdal.props"},
  {"glib-2.0.lib", kConfigLibDir, "vsprops/wb_glib.props"},
  {"gmodule-2.0.lib", kConfigLibDir, "library/grt, library/cdbc"},
  {"gthread-2.0.lib", kConfigLibDir, "library/mysql.canvas"},
  {"gtest.lib", kConfigLibDir, "vsprops/wb_gtest.props"},
  {"gmock.lib", kConfigLibDir, "vsprops/wb_gtest.props"},
  {WB_CONFIG_LIB("libxml2d.lib", "libxml2.lib"), kConfigLibDir, "vsprops/wb_libxml.props"},
  {"mysqlclient.lib", kConfigLibDir, "vsprops/wb_mysql.props"},
  {"mysqlcppconn.lib", kConfigLibDir, "vsprops/wb_cppconn.props"},
  {WB_CONFIG_LIB("sqlite3_d.lib", "sqlite3.lib"), kConfigLibDir, "vsprops/wb_sqlite.props"},
  {"ssh.lib", kConfigLibDir, "vsprops/wb_libssh.props"},
  {"vsqlitepp.lib", kConfigLibDir, "vsprops/wb_vsqlite++.props"},
  {"zip.lib", kConfigLibDir, "vsprops/wb_libzip.props"},
  {WB_CONFIG_LIB("zlibd.lib", "zlib.lib"), kConfigLibDir, "vsprops/wb_zlib.props"},
  {WB_CONFIG_LIB("python312_d.lib", "python312.lib"), "Python\\libs", "vsprops/wb_python.props"},
};

//----------------------------------------------------------------------------------------------------------------------

std::string toLower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return text;
}

bool startsWith(const std::string &text, const std::string &prefix) {
  return text.compare(0, prefix.size(), prefix) == 0;
}

std::vector<uint8_t> readFile(const fs::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    throw std::runtime_error("cannot open " + path.string());
  return std::vector<uint8_t>(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

// Reads a NUL terminated string without running past the end of the buffer.
std::string readCString(const uint8_t *data, size_t size, size_t offset) {
  if (offset >= size)
    throw std::runtime_error("string offset out of bounds");
  size_t end = offset;
  while (end < size && data[end] != 0)
    ++end;
  return std::string(reinterpret_cast<const char *>(data + offset), end - offset);
}

template <typename T>
const T *viewAt(const uint8_t *data, size_t size, size_t offset) {
  if (offset > size || size - offset < sizeof(T))
    throw std::runtime_error("structure out of bounds");
  return reinterpret_cast<const T *>(data + offset);
}

//----------------------------------------------------------------------------------------------------------------------

struct ObjectInfo {
  std::string member;
  WORD machine = 0;
  std::set<std::string> defaultLibs;    // Lower case, without the .lib extension.
  std::set<std::string> failIfMismatch; // KEY=VALUE as written by the compiler.
};

struct LibraryInfo {
  std::vector<ObjectInfo> objects;
  std::set<std::string> dllNames;         // Lower case, from import members.
  std::vector<std::string> opaqueMembers; // LTCG (/GL) objects whose directives are not readable.
};

// Splits a .drectve payload into tokens, honouring double quotes.
std::vector<std::string> tokenizeDirectives(std::string text) {
  if (startsWith(text, "\xEF\xBB\xBF"))
    text.erase(0, 3);

  std::vector<std::string> tokens;
  std::string current;
  bool quoted = false;
  for (char c : text) {
    if (c == '"') {
      quoted = !quoted;
    } else if (!quoted && std::isspace(static_cast<unsigned char>(c))) {
      if (!current.empty())
        tokens.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty())
    tokens.push_back(current);
  return tokens;
}

void collectDirectives(const std::string &directives, ObjectInfo &object) {
  for (const std::string &token : tokenizeDirectives(directives)) {
    if (token.empty() || (token[0] != '/' && token[0] != '-'))
      continue;
    size_t colon = token.find(':');
    if (colon == std::string::npos)
      continue;
    std::string key = toLower(token.substr(1, colon - 1));
    std::string value = token.substr(colon + 1);
    if (key == "defaultlib") {
      value = toLower(value);
      if (value.size() > 4 && value.compare(value.size() - 4, 4, ".lib") == 0)
        value.erase(value.size() - 4);
      object.defaultLibs.insert(value);
    } else if (key == "failifmismatch") {
      object.failIfMismatch.insert(value);
    }
  }
}

void collectSections(const uint8_t *data, size_t size, size_t sectionOffset, size_t sectionCount, ObjectInfo &object) {
  for (size_t i = 0; i < sectionCount; ++i) {
    auto *section = viewAt<IMAGE_SECTION_HEADER>(data, size, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER));
    if (std::memcmp(section->Name, ".drectve", IMAGE_SIZEOF_SHORT_NAME) != 0)
      continue;
    size_t start = section->PointerToRawData;
    size_t length = section->SizeOfRawData;
    if (start > size || size - start < length)
      throw std::runtime_error(".drectve section out of bounds in " + object.member);
    collectDirectives(std::string(reinterpret_cast<const char *>(data + start), length), object);
  }
}

void parseMember(const uint8_t *data, size_t size, const std::string &name, LibraryInfo &info) {
  auto *importHeader = viewAt<IMPORT_OBJECT_HEADER>(data, size, 0);
  if (importHeader->Sig1 == IMAGE_FILE_MACHINE_UNKNOWN && importHeader->Sig2 == IMPORT_OBJECT_HDR_SIG2) {
    if (importHeader->Version == 0) {
      // Short import object: header, symbol name, DLL name.
      std::string symbol = readCString(data, size, sizeof(IMPORT_OBJECT_HEADER));
      std::string dll = readCString(data, size, sizeof(IMPORT_OBJECT_HEADER) + symbol.size() + 1);
      info.dllNames.insert(toLower(dll));

      ObjectInfo object;
      object.member = name;
      object.machine = importHeader->Machine;
      info.objects.push_back(std::move(object));
      return;
    }

    if (importHeader->Version >= 2 && size >= sizeof(ANON_OBJECT_HEADER_BIGOBJ)) {
      auto *bigObject = viewAt<ANON_OBJECT_HEADER_BIGOBJ>(data, size, 0);
      if (std::memcmp(&bigObject->ClassID, &kBigObjClassId, sizeof(GUID)) == 0) {
        ObjectInfo object;
        object.member = name;
        object.machine = bigObject->Machine;
        collectSections(data, size, sizeof(ANON_OBJECT_HEADER_BIGOBJ), bigObject->NumberOfSections, object);
        info.objects.push_back(std::move(object));
        return;
      }
    }

    info.opaqueMembers.push_back(name);
    return;
  }

  auto *fileHeader = viewAt<IMAGE_FILE_HEADER>(data, size, 0);
  ObjectInfo object;
  object.member = name;
  object.machine = fileHeader->Machine;
  collectSections(data, size, sizeof(IMAGE_FILE_HEADER) + fileHeader->SizeOfOptionalHeader, fileHeader->NumberOfSections,
                  object);
  info.objects.push_back(std::move(object));
}

LibraryInfo parseArchive(const std::vector<uint8_t> &file) {
  const uint8_t *data = file.data();
  size_t size = file.size();
  if (size < IMAGE_ARCHIVE_START_SIZE || std::memcmp(data, IMAGE_ARCHIVE_START, IMAGE_ARCHIVE_START_SIZE) != 0)
    throw std::runtime_error("not a COFF archive");

  LibraryInfo info;
  std::string longNames;
  size_t offset = IMAGE_ARCHIVE_START_SIZE;
  while (offset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER) <= size) {
    auto *header = viewAt<IMAGE_ARCHIVE_MEMBER_HEADER>(data, size, offset);
    std::string name(reinterpret_cast<const char *>(header->Name), sizeof(header->Name));
    name.erase(name.find_last_not_of(' ') + 1);
    size_t memberSize =
      std::strtoul(std::string(reinterpret_cast<const char *>(header->Size), sizeof(header->Size)).c_str(), nullptr, 10);
    size_t body = offset + sizeof(IMAGE_ARCHIVE_MEMBER_HEADER);
    if (body > size || size - body < memberSize)
      throw std::runtime_error("archive member " + name + " out of bounds");

    if (name == "//") {
      longNames.assign(reinterpret_cast<const char *>(data + body), memberSize);
    } else if (name != "/" && name != "/<ECSYMBOLS>/") {
      if (name.size() > 1 && name[0] == '/' && std::isdigit(static_cast<unsigned char>(name[1]))) {
        size_t index = std::strtoul(name.c_str() + 1, nullptr, 10);
        if (index < longNames.size())
          name = longNames.c_str() + index;
      } else if (!name.empty() && name.back() == '/') {
        name.pop_back();
      }
      parseMember(data + body, memberSize, name, info);
    }

    offset = body + memberSize + (memberSize & 1);
  }
  return info;
}

//----------------------------------------------------------------------------------------------------------------------

struct PeInfo {
  WORD machine = 0;
  std::set<std::string> imports; // Lower case DLL names.
};

PeInfo parsePortableExecutable(const std::vector<uint8_t> &file) {
  const uint8_t *data = file.data();
  size_t size = file.size();

  auto *dosHeader = viewAt<IMAGE_DOS_HEADER>(data, size, 0);
  if (dosHeader->e_magic != IMAGE_DOS_SIGNATURE)
    throw std::runtime_error("missing MZ signature");
  size_t ntOffset = static_cast<size_t>(dosHeader->e_lfanew);
  if (*viewAt<DWORD>(data, size, ntOffset) != IMAGE_NT_SIGNATURE)
    throw std::runtime_error("missing PE signature");

  auto *fileHeader = viewAt<IMAGE_FILE_HEADER>(data, size, ntOffset + sizeof(DWORD));
  size_t optionalOffset = ntOffset + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER);
  WORD magic = *viewAt<WORD>(data, size, optionalOffset);
  IMAGE_DATA_DIRECTORY importDirectory;
  if (magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
    importDirectory = viewAt<IMAGE_OPTIONAL_HEADER64>(data, size, optionalOffset)->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  else if (magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC)
    importDirectory = viewAt<IMAGE_OPTIONAL_HEADER32>(data, size, optionalOffset)->DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  else
    throw std::runtime_error("unknown optional header magic");

  size_t sectionOffset = optionalOffset + fileHeader->SizeOfOptionalHeader;
  auto rvaToOffset = [&](DWORD rva) -> size_t {
    for (WORD i = 0; i < fileHeader->NumberOfSections; ++i) {
      auto *section = viewAt<IMAGE_SECTION_HEADER>(data, size, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER));
      DWORD extent = std::max(section->Misc.VirtualSize, section->SizeOfRawData);
      if (rva >= section->VirtualAddress && rva < section->VirtualAddress + extent)
        return rva - section->VirtualAddress + section->PointerToRawData;
    }
    throw std::runtime_error("RVA not mapped to any section");
  };

  PeInfo info;
  info.machine = fileHeader->Machine;
  if (importDirectory.VirtualAddress == 0)
    return info;

  size_t descriptorOffset = rvaToOffset(importDirectory.VirtualAddress);
  while (true) {
    auto *descriptor = viewAt<IMAGE_IMPORT_DESCRIPTOR>(data, size, descriptorOffset);
    if (descriptor->Name == 0 && descriptor->FirstThunk == 0)
      break;
    info.imports.insert(toLower(readCString(data, size, rvaToOffset(descriptor->Name))));
    descriptorOffset += sizeof(IMAGE_IMPORT_DESCRIPTOR);
  }
  return info;
}

//----------------------------------------------------------------------------------------------------------------------

// Flavor implied by a /DEFAULTLIB directive, if it names a Visual C++ runtime component.
std::optional<RuntimeFlavor> crtFlavorOfDefaultLib(const std::string &lowerName) {
  static const std::regex pattern("^(lib)?(cmt|cpmt|ucrt|vcruntime|concrt)(d)?$|^(msvcrt|msvcprt)(d)?$");
  std::smatch match;
  if (!std::regex_match(lowerName, match, pattern))
    return std::nullopt;
  if (match[2].matched) {
    // libcmt/libcpmt/libucrt/libvcruntime/libconcrt are static, ucrt/vcruntime/concrt are dynamic.
    bool isStatic = match[1].matched || match[2] == "cmt" || match[2] == "cpmt";
    return RuntimeFlavor{match[3].matched, !isStatic};
  }
  return RuntimeFlavor{match[5].matched, true};
}

// Flavor implied by a DLL import, if it is a Visual C++ runtime DLL.
std::optional<RuntimeFlavor> crtFlavorOfImport(const std::string &lowerName) {
  static const std::regex debugPattern("^(vcruntime|msvcp|concrt|msvcr)[0-9]+(_[0-9]+)?d(_[a-z0-9_]+)?\\.dll$");
  static const std::regex releasePattern("^(vcruntime|msvcp|concrt|msvcr)[0-9]+(_[0-9]+)?(_[a-z0-9_]+)?\\.dll$");
  if (lowerName == "ucrtbased.dll")
    return RuntimeFlavor{true, true};
  if (lowerName == "ucrtbase.dll" || startsWith(lowerName, "api-ms-win-crt-"))
    return RuntimeFlavor{false, true};
  if (std::regex_match(lowerName, debugPattern))
    return RuntimeFlavor{true, true};
  if (std::regex_match(lowerName, releasePattern))
    return RuntimeFlavor{false, true};
  return std::nullopt;
}

std::string machineName(WORD machine) {
  switch (machine) {
    case IMAGE_FILE_MACHINE_AMD64:
      return "x64";
    case IMAGE_FILE_MACHINE_I386:
      return "x86";
    case IMAGE_FILE_MACHINE_ARM64:
      return "ARM64";
    default:
      return "0x" + std::to_string(machine);
  }
}

//----------------------------------------------------------------------------------------------------------------------

class ThirdPartyRuntime : public testing::TestWithParam<LinkedLibrary> {
protected:
  fs::path bundle;

  void SetUp() override {
    const char *value = std::getenv("MSS_3DPARTY_PATH");
    if (value == nullptr || *value == 0)
      GTEST_SKIP() << "MSS_3DPARTY_PATH is not set; the third-party bundle cannot be located.";
    bundle = value;
    if (!fs::is_directory(bundle))
      GTEST_SKIP() << "MSS_3DPARTY_PATH does not point to a directory: " << bundle;
  }

  // Import libraries are staged next to their DLLs; Python keeps its DLLs one level above libs\.
  fs::path locateDll(const fs::path &libDir, const std::string &dllName) const {
    const fs::path candidates[] = {libDir, libDir.parent_path(), bundle / kConfigBinDir, bundle / "Python"};
    for (const fs::path &directory : candidates) {
      fs::path candidate = directory / dllName;
      if (fs::is_regular_file(candidate))
        return candidate;
    }
    return {};
  }
};

TEST_P(ThirdPartyRuntime, MatchesSqlStudioCompileAndLinkSwitches) {
  const LinkedLibrary &library = GetParam();
  fs::path libDir = bundle / library.libDir;
  fs::path libPath = libDir / library.name;
  ASSERT_TRUE(fs::is_regular_file(libPath)) << "Linked library is missing from the bundle: " << libPath;

  LibraryInfo info = parseArchive(readFile(libPath));
  ASSERT_FALSE(info.objects.empty() && info.opaqueMembers.empty()) << libPath << " contains no object members";

  EXPECT_TRUE(info.opaqueMembers.empty()) << libPath << " contains " << info.opaqueMembers.size()
                                          << " LTCG (/GL) object(s) whose runtime settings cannot be inspected, e.g. "
                                          << info.opaqueMembers.front();

  bool sawCrtDefaultLib = false;
  for (const ObjectInfo &object : info.objects) {
    SCOPED_TRACE(libPath.string() + " :: " + object.member);
    EXPECT_EQ(object.machine, kOwnMachine) << "object targets " << machineName(object.machine) << " but SqlStudio is "
                                           << machineName(kOwnMachine);

    for (const std::string &defaultLib : object.defaultLibs) {
      if (auto flavor = crtFlavorOfDefaultLib(defaultLib)) {
        sawCrtDefaultLib = true;
        EXPECT_EQ(*flavor, kOwnFlavor) << "/DEFAULTLIB:" << defaultLib << " was produced by " << describe(*flavor)
                                       << " but SqlStudio compiles with " << describe(kOwnFlavor);
      }
    }

    for (const std::string &entry : object.failIfMismatch) {
      size_t equals = entry.find('=');
      if (equals == std::string::npos)
        continue;
      std::string key = entry.substr(0, equals);
      std::string value = entry.substr(equals + 1);
      if (key == "_ITERATOR_DEBUG_LEVEL")
        EXPECT_EQ(value, kOwnIteratorDebugLevel) << "/FAILIFMISMATCH:" << entry;
      else if (key == "RuntimeLibrary")
        EXPECT_EQ(value, kOwnRuntimeLibraryTag) << "/FAILIFMISMATCH:" << entry;
      else if (key == "_MSC_VER")
        EXPECT_EQ(value, kOwnMscVer) << "/FAILIFMISMATCH:" << entry;
    }
  }

  for (const std::string &dllName : info.dllNames) {
    fs::path dllPath = locateDll(libDir, dllName);
    if (dllPath.empty()) {
      ADD_FAILURE() << libPath << " imports " << dllName << " but that DLL was not staged in the bundle";
      continue;
    }
    SCOPED_TRACE(dllPath.string());

    PeInfo pe = parsePortableExecutable(readFile(dllPath));
    EXPECT_EQ(pe.machine, kOwnMachine) << "DLL targets " << machineName(pe.machine) << " but SqlStudio is "
                                       << machineName(kOwnMachine);

    bool sawCrtImport = false;
    for (const std::string &import : pe.imports) {
      if (auto flavor = crtFlavorOfImport(import)) {
        sawCrtImport = true;
        EXPECT_EQ(*flavor, kOwnFlavor) << "DLL imports " << import << " (" << describe(*flavor)
                                       << ") but SqlStudio links with " << describe(kOwnFlavor);
      }
    }
    EXPECT_TRUE(sawCrtImport) << "DLL imports no Visual C++ runtime DLL at all; it was probably built with "
                              << (kOwnFlavor.debug ? "/MTd" : "/MT") << " or a different compiler";
  }

  EXPECT_TRUE(sawCrtDefaultLib || !info.dllNames.empty())
    << libPath << " carries no runtime information (no CRT /DEFAULTLIB directives and no import members)";
}

INSTANTIATE_TEST_SUITE_P(Bundle, ThirdPartyRuntime, testing::ValuesIn(kLinkedLibraries),
                         [](const testing::TestParamInfo<LinkedLibrary> &param) {
                           std::string name = param.param.name;
                           name.erase(name.size() - 4); // ".lib"
                           for (char &c : name)
                             if (!std::isalnum(static_cast<unsigned char>(c)))
                               c = '_';
                           return name;
                         });

} // namespace

#endif // _MSC_VER
