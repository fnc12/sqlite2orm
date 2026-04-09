cmake_minimum_required(VERSION 3.16)
project(sqlite2orm VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(SQLite3 REQUIRED)

include(FetchContent)
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

    message(STATUS "[sqlite2orm] Fetching sqlite_orm headers (dev)...")
    FetchContent_Declare(
        sqlite_orm_headers
        GIT_REPOSITORY https://github.com/fnc12/sqlite_orm.git
        GIT_TAG dev
        GIT_SHALLOW TRUE
    )
    FetchContent_Populate(sqlite_orm_headers)
    message(STATUS "[sqlite2orm] sqlite_orm headers ready.")

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
        tests/codegen_tests_create_table.cpp
        tests/codegen_tests_select.cpp
        tests/codegen_tests_dml.cpp
        tests/codegen_tests_virtual_table.cpp
        tests/codegen_tests_with.cpp
        tests/codegen_tests_pragma.cpp
        tests/validator_tests.cpp
        tests/process_tests.cpp
        tests/schema_reader_tests.cpp
        tests/schema_pipeline_tests.cpp
    )
    target_compile_definitions(sqlite2orm_tests PRIVATE
        SQLITE2ORM_TEST_SQLITE_ORM_INCLUDE="${sqlite_orm_headers_SOURCE_DIR}/include")
    target_link_libraries(sqlite2orm_tests PRIVATE sqlite2orm_lib Catch2::Catch2WithMain)
    add_test(NAME sqlite2orm_tests COMMAND sqlite2orm_tests)

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
        if(UNIX)
            add_test(
                NAME sqlite2orm_cli_stdin
                COMMAND sh -c "printf 'SELECT 7\\n' | \"$<TARGET_FILE:sqlite2orm_cli>\"")
            set_tests_properties(sqlite2orm_cli_stdin PROPERTIES PASS_REGULAR_EXPRESSION "select\\(7\\)")
        endif()
    endif()
endif()
