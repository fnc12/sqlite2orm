cmake_minimum_required(VERSION 3.16)
project(sqlite2orm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Consumers (e.g. Android, where the NDK ships no libsqlite3) may provide their own
# SQLite::SQLite3 target before adding this project.
if(NOT TARGET SQLite::SQLite3)
    find_package(SQLite3 REQUIRED)
endif()

include(FetchContent)
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SqliteOrmGitRevision.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/cmake/SqliteOrmHeadersRevisionNote.cmake")
message(STATUS "[sqlite2orm] Fetching fmt…")
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 11.0.2
)
FetchContent_MakeAvailable(fmt)
message(STATUS "[sqlite2orm] fmt ready.")

# Single-header nlohmann/json (avoids cloning the full git repo at configure time).
set(SQLITE2ORM_NLOHMANN_JSON_DIR "${CMAKE_CURRENT_BINARY_DIR}/_nlohmann_single")
set(SQLITE2ORM_NLOHMANN_JSON_HPP "${SQLITE2ORM_NLOHMANN_JSON_DIR}/nlohmann/json.hpp")
if(NOT EXISTS "${SQLITE2ORM_NLOHMANN_JSON_HPP}")
    file(MAKE_DIRECTORY "${SQLITE2ORM_NLOHMANN_JSON_DIR}/nlohmann")
    file(DOWNLOAD
        "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
        "${SQLITE2ORM_NLOHMANN_JSON_HPP}"
        TLS_VERIFY ON
    )
endif()

add_library(sqlite2orm_lib
    src/ast.cpp
    src/utils.cpp
    src/tokenizer.cpp
    src/token_stream.cpp
    src/parser.cpp
    src/parser_expression.cpp
    src/parser_select.cpp
    src/parser_dml.cpp
    src/parser_ddl.cpp
    src/validator.cpp
    src/codegen.cpp
    src/codegen_utils.cpp
    src/codegen_context.cpp
    src/codegen_expression.cpp
    src/codegen_select.cpp
    src/codegen_dml.cpp
    src/codegen_ddl.cpp
    src/codegen_with.cpp
    src/codegen_pragma.cpp
    src/pragma_sqlite_orm.cpp
    src/process.cpp
    src/schema_reader.cpp
    src/schema_process.cpp
    src/schema_header.cpp
    src/json_emit.cpp
    src/schema_report.cpp
)

target_include_directories(sqlite2orm_lib PUBLIC include "${SQLITE2ORM_NLOHMANN_JSON_DIR}")
target_link_libraries(sqlite2orm_lib PUBLIC SQLite::SQLite3)

# For GUI / other apps: target_link_libraries(your_app PRIVATE sqlite2orm::sqlite2orm)
add_library(sqlite2orm::sqlite2orm ALIAS sqlite2orm_lib)

option(SQLITE2ORM_BUILD_CLI "Build sqlite2orm command-line tool" ON)

if(SQLITE2ORM_BUILD_CLI)
    add_executable(sqlite2orm_cli src/cli_main.cpp)
    target_link_libraries(sqlite2orm_cli PRIVATE sqlite2orm_lib fmt::fmt)
    set_target_properties(sqlite2orm_cli PROPERTIES OUTPUT_NAME sqlite2orm)
endif()

add_subdirectory(examples)

option(SQLITE2ORM_BUILD_TESTS "Build tests" ON)

if(SQLITE2ORM_BUILD_TESTS)
    message(STATUS "[sqlite2orm] Downloading Catch2 v3.8.0...")
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.0
    )
    FetchContent_MakeAvailable(Catch2)
    message(STATUS "[sqlite2orm] Catch2 ready.")

    find_program(SQLITE2ORM_GIT_EXECUTABLE git)
    # find_program answers `<var>-NOTFOUND`, which if() reads as false but everything else reads as
    # a path; the layers below are handed an empty string instead, so "there is no git here" is the
    # same value in cmake, in the note and in the test that skips over it.
    set(sqlite2orm_git_executable "")
    if(SQLITE2ORM_GIT_EXECUTABLE)
        set(sqlite2orm_git_executable "${SQLITE2ORM_GIT_EXECUTABLE}")
    endif()

    # The runtime tests compile the generated code against these headers, so following the moving
    # `dev` branch made the same commit of this repository pass or fail depending on when the
    # checkout was populated, and the failures read like a bug in whatever was under test. The
    # revision lives in its own module, together with the rule that decides which one a tree uses;
    # a commit hash rules out GIT_SHALLOW, which CMake accepts only for branch and tag names.
    include("${CMAKE_CURRENT_LIST_DIR}/cmake/SqliteOrmPinnedRevision.cmake")
    message(STATUS "[sqlite2orm] Fetching sqlite_orm headers (${SQLITE2ORM_SQLITE_ORM_REVISION})...")
    FetchContent_Declare(
        sqlite_orm_headers
        GIT_REPOSITORY https://github.com/fnc12/sqlite_orm.git
        GIT_TAG ${SQLITE2ORM_SQLITE_ORM_REVISION}
    )
    FetchContent_Populate(sqlite_orm_headers)
    message(STATUS "[sqlite2orm] sqlite_orm headers ready.")

    # A build tree configured before the pin keeps whatever it populated back then, and only a
    # reconfigure moves it; report the revision the tests really see instead of leaving a stale
    # checkout to surface as a compile error inside a generated program.
    sqlite2orm_git_revision_of(
        "${sqlite_orm_headers_SOURCE_DIR}" "${sqlite2orm_git_executable}" sqlite2orm_sqlite_orm_head)

    # Headers that are not a checkout at all -- and a machine that has no git to ask -- leave the
    # guard with nothing to compare, and it can then only skip: say so here, or the pin quietly
    # stops holding for the trees that most need it. Repopulating a dependency takes removing the
    # sub-build that stamps the clone step as done, so the note is handed that directory too;
    # FETCHCONTENT_BASE_DIR is where FetchContent puts both.
    sqlite2orm_headers_revision_note(
        "${sqlite_orm_headers_SOURCE_DIR}" "${FETCHCONTENT_BASE_DIR}/sqlite_orm_headers-subbuild"
        "${FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS}" "${sqlite2orm_git_executable}"
        "${SQLITE2ORM_SQLITE_ORM_REVISION}" "${sqlite2orm_sqlite_orm_head}"
        sqlite2orm_sqlite_orm_headers_note)
    if(sqlite2orm_sqlite_orm_headers_note)
        message(STATUS "[sqlite2orm] ${sqlite2orm_sqlite_orm_headers_note}")
    endif()

    enable_testing()
    add_executable(sqlite2orm_tests
        tests/tokenizer_tests.cpp
        tests/parser_tests_common.cpp
        tests/parser_tests_expressions.cpp
        tests/parser_tests_create_table.cpp
        tests/parser_tests_select.cpp
        tests/parser_tests_subquery.cpp
        tests/parser_tests_compound.cpp
        tests/parser_tests_with.cpp
        tests/parser_tests_dml.cpp
        tests/parser_tests_upsert.cpp
        tests/parser_tests_trigger.cpp
        tests/parser_tests_index.cpp
        tests/parser_tests_virtual_table.cpp
        tests/parser_tests_from_derived.cpp
        tests/parser_tests_create_view.cpp
        tests/parser_tests_misc_statements.cpp
        tests/codegen_tests_common.cpp
        tests/codegen_tests_expressions.cpp
        tests/codegen_tests_runtime_values.cpp
        tests/codegen_tests_create_table.cpp
        tests/codegen_tests_create_view.cpp
        tests/codegen_tests_select.cpp
        tests/codegen_tests_dml.cpp
        tests/codegen_tests_virtual_table.cpp
        tests/codegen_tests_with.cpp
        tests/codegen_tests_pragma.cpp
        tests/codegen_tests_placeholders.cpp
        tests/codegen_tests_savepoint.cpp
        tests/validator_tests.cpp
        tests/process_tests.cpp
        tests/ast_lifetime_tests.cpp
        tests/schema_reader_tests.cpp
        tests/schema_pipeline_tests.cpp
        tests/schema_report_tests.cpp
        tests/readme_tests.cpp
        tests/sqlite_orm_revision_tests.cpp
        tests/temp_build_dir.cpp
    )
    # A probe that runs the generated code needs the same libsqlite3 the library links, which is not
    # always the one a bare `-lsqlite3` finds.
    if(SQLite3_LIBRARIES)
        set(sqlite2orm_test_sqlite3_link "${SQLite3_LIBRARIES}")
    else()
        set(sqlite2orm_test_sqlite3_link "-lsqlite3")
    endif()
    target_compile_definitions(sqlite2orm_tests PRIVATE
        SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE="${sqlite_orm_headers_SOURCE_DIR}/include"
        SQLITE2ORM_TEST_SQLITE_ORM_REVISION="${SQLITE2ORM_SQLITE_ORM_REVISION}"
        SQLITE2ORM_TEST_SQLITE_ORM_HEAD="${sqlite2orm_sqlite_orm_head}"
        SQLITE2ORM_TEST_SQLITE_ORM_SOURCE_DIR="${sqlite_orm_headers_SOURCE_DIR}"
        SQLITE2ORM_TEST_SQLITE_ORM_SOURCE_OVERRIDE="${FETCHCONTENT_SOURCE_DIR_SQLITE_ORM_HEADERS}"
        SQLITE2ORM_TEST_GIT_EXECUTABLE="${sqlite2orm_git_executable}"
        SQLITE2ORM_TEST_SQLITE3_LINK="${sqlite2orm_test_sqlite3_link}"
        SQLITE2ORM_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(sqlite2orm_tests PRIVATE sqlite2orm_lib Catch2::Catch2WithMain)
    add_test(NAME sqlite2orm_tests COMMAND sqlite2orm_tests)

    if(UNIX)
        if(SQLITE2ORM_GIT_EXECUTABLE)
            add_test(
                NAME sqlite2orm_tests_worktree_guard
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/git_worktree_guard.sh"
                        "${CMAKE_CURRENT_SOURCE_DIR}/scripts/git-worktree-guard.sh"
                        "${SQLITE2ORM_GIT_EXECUTABLE}")
            add_test(
                NAME sqlite2orm_tests_git_revision_of
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake_git_revision_test.sh"
                        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SqliteOrmGitRevision.cmake"
                        "${SQLITE2ORM_GIT_EXECUTABLE}"
                        "${CMAKE_COMMAND}")
        endif()
        add_test(
            NAME sqlite2orm_tests_pinned_revision
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake_pinned_revision_test.sh"
                    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SqliteOrmPinnedRevision.cmake"
                    "${CMAKE_COMMAND}")
        add_test(
            NAME sqlite2orm_tests_headers_revision_note
            COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake_headers_revision_note_test.sh"
                    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SqliteOrmHeadersRevisionNote.cmake"
                    "${CMAKE_COMMAND}")
    endif()

    if(SQLITE2ORM_BUILD_CLI)
        add_test(NAME sqlite2orm_cli_help COMMAND $<TARGET_FILE:sqlite2orm_cli> --help)
        set_tests_properties(sqlite2orm_cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage:")
        add_test(NAME sqlite2orm_cli_select_one COMMAND $<TARGET_FILE:sqlite2orm_cli> -e "SELECT 1")
        set_tests_properties(sqlite2orm_cli_select_one PROPERTIES PASS_REGULAR_EXPRESSION "storage")
        add_test(
            NAME sqlite2orm_cli_create_table
            COMMAND $<TARGET_FILE:sqlite2orm_cli> -e "CREATE TABLE t (id INTEGER PRIMARY KEY);")
        set_tests_properties(sqlite2orm_cli_create_table PROPERTIES PASS_REGULAR_EXPRESSION "make_storage")
        add_test(
            NAME sqlite2orm_cli_read_file
            COMMAND $<TARGET_FILE:sqlite2orm_cli> "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_fixtures/one_table.sql")
        set_tests_properties(sqlite2orm_cli_read_file PROPERTIES PASS_REGULAR_EXPRESSION "struct Demo")
        add_test(
            NAME sqlite2orm_cli_pragma_value_error_exit_code
            COMMAND $<TARGET_FILE:sqlite2orm_cli> -e "PRAGMA recursive_triggers = NULL;")
        set_tests_properties(sqlite2orm_cli_pragma_value_error_exit_code PROPERTIES WILL_FAIL TRUE)
        if(UNIX)
            find_program(SQLITE2ORM_SQLITE3_EXECUTABLE sqlite3)
            if(SQLITE2ORM_SQLITE3_EXECUTABLE)
                add_test(
                    NAME sqlite2orm_cli_db_json_exit_code
                    COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_db_json_exit_code.sh"
                            "$<TARGET_FILE:sqlite2orm_cli>" "${SQLITE2ORM_SQLITE3_EXECUTABLE}")
            endif()
            add_test(
                NAME sqlite2orm_cli_stdin
                COMMAND sh -c "printf 'SELECT 7\\n' | \"$<TARGET_FILE:sqlite2orm_cli>\"")
            set_tests_properties(sqlite2orm_cli_stdin PROPERTIES PASS_REGULAR_EXPRESSION "select\\(7\\)")
        endif()
    endif()

    # The formatting tests need clang-format 19 exactly: other versions format this tree
    # differently, so an older one in PATH would only produce noise.
    if(UNIX)
        find_program(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE NAMES clang-format-19 clang-format)
        if(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE)
            execute_process(
                COMMAND "${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}" --version
                OUTPUT_VARIABLE sqlite2orm_clang_format_version
                ERROR_QUIET)
            if(NOT sqlite2orm_clang_format_version MATCHES "clang-format version 19\\.")
                message(STATUS "[sqlite2orm] clang-format 19 not found, skipping the formatting tests.")
                set(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE "")
            endif()
        endif()
        if(SQLITE2ORM_CLANG_FORMAT_EXECUTABLE)
            add_test(
                NAME sqlite2orm_clang_format
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_format_check.sh"
                        "${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(
                NAME sqlite2orm_clang_format_report
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/clang_format_report_test.sh"
                        "${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(
                NAME sqlite2orm_git_hooks
                COMMAND sh "${CMAKE_CURRENT_SOURCE_DIR}/tests/git_hooks_test.sh"
                        "${SQLITE2ORM_CLANG_FORMAT_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}")
        endif()
    endif()
endif()
