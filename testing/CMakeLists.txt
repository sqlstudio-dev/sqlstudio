###############################################################################
# testing/CMakeLists.txt — Build the SqlStudio gtest unit-test suite
###############################################################################
#
# This file defines the "wbtests-bin" executable that links against the
# project's libraries and runs all Google Test specs.
#
# To include this in the build, add the following line to the top-level
# CMakeLists.txt (after the other add_subdirectory calls):
#
#     if(BUILD_TESTING)
#       add_subdirectory(testing)
#     endif()
#
###############################################################################

cmake_minimum_required(VERSION 3.5)

# ── Google Test ─────────────────────────────────────────────────────────────
find_package(GTest QUIET)
if(NOT GTest_FOUND AND NOT GTEST_FOUND)
  # Fallback: try pkg-config
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(GTEST gtest)
    pkg_check_modules(GMOCK gmock)
  endif()
endif()

# If GTest was found via find_package we may have imported targets
if(TARGET GTest::gtest)
  set(GTEST_LIBRARIES GTest::gtest GTest::gmock)
elseif(TARGET GTest::GTest)
  set(GTEST_LIBRARIES GTest::GTest GTest::Main)
elseif(GTEST_FOUND)
  # pkg-config path — variables already set
  set(GTEST_LIBRARIES ${GTEST_LIBRARIES} ${GMOCK_LIBRARIES})
else()
  # Manual fallback — headers/libs might be in WB_BUNDLE_DIR or system paths
  find_library(GTEST_LIB  NAMES gtest  PATHS "${WB_BUNDLE_DIR}/lib" /usr/lib /usr/local/lib)
  find_library(GMOCK_LIB  NAMES gmock  PATHS "${WB_BUNDLE_DIR}/lib" /usr/lib /usr/local/lib)
  if(GTEST_LIB)
    set(GTEST_LIBRARIES ${GTEST_LIB} ${GMOCK_LIB})
  else()
    message(FATAL_ERROR "Google Test not found.  Install libgtest-dev / libgmock-dev or set WB_BUNDLE_DIR.")
  endif()
endif()

# ── Source files ────────────────────────────────────────────────────────────

# main + PCH creator
set(TEST_MAIN_SOURCES
  test-suite/main.cpp
  test-suite/pch.cpp
)

# Core test infrastructure
set(TEST_CORE_SOURCES
  test-suite/core/context.cpp
  test-suite/core/grt_test_helpers.cpp
  test-suite/core/helpers.cpp
  test-suite/core/model_mockup.cpp
  test-suite/core/wb_connection_helpers.cpp
  test-suite/core/wb_references.cpp
  test-suite/core/wb_test_helpers.cpp
)

# Stub mforms (headless UI mock for testing without GTK)
set(TEST_STUB_SOURCES
  test-suite/tests/library/forms/stub/src/stub_app.cpp
  test-suite/tests/library/forms/stub/src/stub_base.cpp
  test-suite/tests/library/forms/stub/src/stub_drawbox.cpp
  test-suite/tests/library/forms/stub/src/stub_form.cpp
  test-suite/tests/library/forms/stub/src/stub_listbox.cpp
  test-suite/tests/library/forms/stub/src/stub_menu.cpp
  test-suite/tests/library/forms/stub/src/stub_mforms.cpp
  test-suite/tests/library/forms/stub/src/stub_selector.cpp
  test-suite/tests/library/forms/stub/src/stub_textbox.cpp
  test-suite/tests/library/forms/stub/src/stub_textentry.cpp
  test-suite/tests/library/forms/stub/src/stub_treenode.cpp
  test-suite/tests/library/forms/stub/src/stub_utilities.cpp
  test-suite/tests/library/forms/stub/src/stub_view.cpp
  test-suite/tests/library/forms/stub/src/stub_wizard.cpp
)

# Backend test sources (pulled from the main source tree)
set(TEST_BACKEND_METACLASSES
  ${CMAKE_SOURCE_DIR}/backend/wbprivate/studio/metaclasses.cpp
)

# ── Test spec files ─────────────────────────────────────────────────────────

# library/base
set(TEST_LIBRARY_BASE_SPECS
  test-suite/tests/library/base/commandlineparser_specs.cpp
  test-suite/tests/library/base/config_file_specs.cpp
  test-suite/tests/library/base/fileutilities_specs.cpp
  test-suite/tests/library/base/sqlstring_specs.cpp
  test-suite/tests/library/base/stringutilities_specs.cpp
  test-suite/tests/library/base/threading_specs.cpp
  test-suite/tests/library/base/utf8string_specs.cpp
)

# library/cdbc
set(TEST_LIBRARY_CDBC_SPECS
  test-suite/tests/library/cdbc/dbc_connection_specs.cpp
  test-suite/tests/library/cdbc/dbc_general_specs.cpp
  test-suite/tests/library/cdbc/dbc_metadata_specs.cpp
  test-suite/tests/library/cdbc/dbc_result_set_specs.cpp
  test-suite/tests/library/cdbc/dbc_statement_specs.cpp
)

# library/forms
set(TEST_LIBRARY_FORMS_SPECS
  test-suite/tests/library/forms/code_editor_specs.cpp
  test-suite/tests/library/forms/utilities_specs.cpp
)

# library/grt
set(TEST_LIBRARY_GRT_SPECS
  test-suite/tests/library/grt/comparer_specs.cpp
  test-suite/tests/library/grt/diff_tree_specs.cpp
  test-suite/tests/library/grt/grtdiff_alter_specs.cpp
  test-suite/tests/library/grt/grtdiff_db_specs.cpp
  test-suite/tests/library/grt/grtlistdiff_specs.cpp
  test-suite/tests/library/grt/grtpp_serialization_specs.cpp
  test-suite/tests/library/grt/grtpp_specs.cpp
  test-suite/tests/library/grt/grtpp_util_specs.cpp
  test-suite/tests/library/grt/modulenative_specs.cpp
  test-suite/tests/library/grt/object_specs.cpp
  test-suite/tests/library/grt/struct_specs.cpp
  test-suite/tests/library/grt/sync_profile_specs.cpp
  test-suite/tests/library/grt/value_specs.cpp
)

# library/mtemplates
set(TEST_LIBRARY_MTEMPLATE_SPECS
  test-suite/tests/library/mtemplates/mtemplate_specs.cpp
)

# library/mysql.canvas
set(TEST_LIBRARY_CANVAS_SPECS
  test-suite/tests/library/mysql.canvas/mysqlcanvas_specs.cpp
)

# library/parsers
set(TEST_LIBRARY_PARSERS_SPECS
  test-suite/tests/library/parsers/mysql_parser_specs.cpp
)

# library/sql.parser
set(TEST_LIBRARY_SQLPARSER_SPECS
  test-suite/tests/library/sql.parser/sqlparser_specs.cpp
)

# backend/wbprivate
set(TEST_BACKEND_WBPRIVATE_SPECS
  test-suite/tests/backend/wbprivate/studio/overview_specs.cpp
  test-suite/tests/backend/wbprivate/studio/ssh_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_context_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_copy_paste_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_lowlevel_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_model_file_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_module_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_undo_diagram_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_undo_editors_specs.cpp
  test-suite/tests/backend/wbprivate/studio/wb_undo_others_specs.cpp
  test-suite/tests/backend/wbprivate/sqlide/wb_live_schema_tree_specs.cpp
  test-suite/tests/backend/wbprivate/sqlide/wb_sql_editor_form_specs.cpp
  test-suite/tests/backend/wbprivate/sqlide/wb_sql_editor_help_specs.cpp
)

# backend/wbpublic
set(TEST_BACKEND_WBPUBLIC_SPECS
  test-suite/tests/backend/wbpublic/grt/common_specs.cpp
  test-suite/tests/backend/wbpublic/grt/grt_dispatcher_specs.cpp
  test-suite/tests/backend/wbpublic/grt/grt_inspector_value_specs.cpp
  test-suite/tests/backend/wbpublic/grt/nodeid_specs.cpp
  test-suite/tests/backend/wbpublic/grt/shell_specs.cpp
  test-suite/tests/backend/wbpublic/grt/tree_model_specs.cpp
  test-suite/tests/backend/wbpublic/grtdb/editor_table_specs.cpp
  test-suite/tests/backend/wbpublic/grtdb/table_inserts_specs.cpp
  test-suite/tests/backend/wbpublic/sqlide/recordset_specs.cpp
  test-suite/tests/backend/wbpublic/sqlide/sql_editor_be_autocomplete_specs.cpp
)

# modules
set(TEST_MODULES_SPECS
  test-suite/tests/modules/db.mysql/db_mysql_gen_grant_specs.cpp
  test-suite/tests/modules/db.mysql/sql_create_specs.cpp
  test-suite/tests/modules/db.mysql.parser/mysql_parser_module_specs.cpp
  test-suite/tests/modules/db.mysql.parser/parse_datatypes_specs.cpp
  test-suite/tests/modules/db.mysql.sqlparser/mysql_invalid_sql_parser_specs.cpp
  test-suite/tests/modules/db.mysql.sqlparser/mysql_sql_facade_specs.cpp
  test-suite/tests/modules/db.mysql.sqlparser/mysql_sql_parser_specs.cpp
  test-suite/tests/modules/db.mysql.sqlparser/mysql_sql_statement_decomposer_specs.cpp
)

# plugins
set(TEST_PLUGINS_SPECS
  test-suite/tests/plugins/db.mysql/backend/db_mysql_plugin_specs.cpp
  test-suite/tests/plugins/db.mysql/backend/db_mysql_sql_export_specs.cpp
  test-suite/tests/plugins/db.mysql/backend/model_diff_apply_specs.cpp
  test-suite/tests/plugins/db.mysql.editors/backend/mysql_routinegroup_editor_specs.cpp
  test-suite/tests/plugins/db.mysql.editors/backend/mysql_table_editor_specs.cpp
)

# ── Combine all sources ────────────────────────────────────────────────────
set(ALL_TEST_SOURCES
  ${TEST_MAIN_SOURCES}
  ${TEST_CORE_SOURCES}
  ${TEST_STUB_SOURCES}
  ${TEST_BACKEND_METACLASSES}
  ${TEST_LIBRARY_BASE_SPECS}
  ${TEST_LIBRARY_CDBC_SPECS}
  ${TEST_LIBRARY_FORMS_SPECS}
  ${TEST_LIBRARY_GRT_SPECS}
  ${TEST_LIBRARY_MTEMPLATE_SPECS}
  ${TEST_LIBRARY_CANVAS_SPECS}
  ${TEST_LIBRARY_PARSERS_SPECS}
  ${TEST_LIBRARY_SQLPARSER_SPECS}
  ${TEST_BACKEND_WBPRIVATE_SPECS}
  ${TEST_BACKEND_WBPUBLIC_SPECS}
  ${TEST_MODULES_SPECS}
  ${TEST_PLUGINS_SPECS}
)

# ── Executable ──────────────────────────────────────────────────────────────
add_executable(wbtests-bin ${ALL_TEST_SOURCES})

# ── Include directories ─────────────────────────────────────────────────────
target_include_directories(wbtests-bin PRIVATE
  # Test infrastructure
  ${CMAKE_CURRENT_SOURCE_DIR}/test-suite/core
  ${CMAKE_CURRENT_SOURCE_DIR}/test-suite
  ${CMAKE_CURRENT_SOURCE_DIR}/test-suite/tests/library/forms/stub

  # Project source directories
  ${CMAKE_SOURCE_DIR}/library/base
  ${CMAKE_SOURCE_DIR}/library/base/base
  ${CMAKE_SOURCE_DIR}/library/cdbc/src
  ${CMAKE_SOURCE_DIR}/library/forms
  ${CMAKE_SOURCE_DIR}/library/grt/src
  ${CMAKE_SOURCE_DIR}/library/parsers
  ${CMAKE_SOURCE_DIR}/library/mtemplate
  ${CMAKE_SOURCE_DIR}/library/mysql.canvas/src
  ${CMAKE_SOURCE_DIR}/library/sql.parser/include
  ${CMAKE_SOURCE_DIR}/library/sql.parser/source
  ${CMAKE_SOURCE_DIR}/library
  ${CMAKE_SOURCE_DIR}/backend/wbprivate
  ${CMAKE_SOURCE_DIR}/backend/wbprivate/studio
  ${CMAKE_SOURCE_DIR}/backend/wbpublic
  ${CMAKE_SOURCE_DIR}/generated
  ${CMAKE_SOURCE_DIR}/modules
  ${CMAKE_SOURCE_DIR}/modules/db.mysql/src
  ${CMAKE_SOURCE_DIR}/modules/db.mysql.sqlparser/src
  ${CMAKE_SOURCE_DIR}/plugins/db.mysql
  ${CMAKE_SOURCE_DIR}/plugins/db.mysql/backend
  ${CMAKE_SOURCE_DIR}/plugins/db.mysql.editors/backend
  ${CMAKE_SOURCE_DIR}/ext/scintilla/include

  # External dependencies (from find_package / pkg-config)
  ${GLIB_INCLUDE_DIRS}
  ${GTK3_INCLUDE_DIRS}
  ${CAIRO_INCLUDE_DIRS}
  ${MYSQLCPPCONN_INCLUDE_DIR}
  ${MYSQL_INCLUDE_DIRS}
  ${VSQLITE_INCLUDE_DIR}
  ${ANTLR4_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${RAPIDJSON_INCLUDE_DIR}
  ${GDAL_INCLUDE_DIR}
  ${LIBSSH_INCLUDE_DIR}
  ${ODBC_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIR}
  ${PYTHONLIBS_INCLUDE_DIRS}
  ${PYTHON_INCLUDE_DIRS}
  ${LIBXML2_INCLUDE_DIR}
)

# ── Link libraries ──────────────────────────────────────────────────────────
# Project libraries (targets defined by the main build)
target_link_libraries(wbtests-bin PRIVATE
  # Google Test
  ${GTEST_LIBRARIES}

  # Project libraries
  wbbase
  grt
  cdbc
  wbssh
  mforms
  wbscintilla
  parsers
  sqlparser
  mdcanvas
  mtemplate
  wbprivate
  wbpublic
  db.mysql.grt
  db.mysql.parser.grt
  db.mysql.sqlparser.grt
  db.mysql.wbp
  db.mysql.editors.wbp

  # External libraries
  ${GLIB_LIBRARIES}
  ${GTK3_LIBRARIES}
  ${GTKMM_LIBRARIES}
  ${CAIRO_LIBRARIES}
  ${UUID_LIBRARIES}
  ${LIBZIP_LIBRARIES}
  ${MYSQLCPPCONN_LIBRARIES}
  ${MYSQL_LIBRARIES}
  ${VSQLITE_LIBRARIES}
  ${ANTLR4_LIBRARIES}
  ${GDAL_LIBRARY}
  ${LIBSSH_LIBRARIES}
  ${ODBC_LIBRARIES}
  ${OPENSSL_LIBRARIES}
  ${LIBXML2_LIBRARIES}
  ${PYTHONLIBS_LIBRARIES}
  ${X11_LIBRARIES}
  ${CMAKE_THREAD_LIBS_INIT}
  ${CMAKE_DL_LIBS}
  stdc++fs
)

# ── Compiler flags ──────────────────────────────────────────────────────────
target_compile_options(wbtests-bin PRIVATE ${WB_CXXFLAGS})
target_compile_definitions(wbtests-bin PRIVATE
  RAPIDJSON_HAS_STDSTRING=1
)

if(BUILD_FOR_GCOV)
  target_compile_options(wbtests-bin PRIVATE -fprofile-arcs -ftest-coverage)
  target_link_options(wbtests-bin PRIVATE -fprofile-arcs -ftest-coverage)
endif()

# ── Output location ─────────────────────────────────────────────────────────
set_target_properties(wbtests-bin PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# ── CTest integration ──────────────────────────────────────────────────────
enable_testing()
add_test(NAME SqlStudioTests COMMAND wbtests-bin --gtest_color=yes)
