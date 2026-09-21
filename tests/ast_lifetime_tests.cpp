#include <sqlite2orm/codegen.h>
#include <sqlite2orm/parser.h>
#include <sqlite2orm/tokenizer.h>

#include <catch2/catch_all.hpp>

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

using namespace sqlite2orm;

// The AST a parse answers with carries text: the name of a column, the spelling of a literal, the
// stretch of source a warning underlines. None of it may be a view into the SQL the tokens were
// made from — a consumer that parses once and generates later (SQLite ORM Studio does) has long
// let that buffer go by then, and reading it back is a read of freed memory.
//
// Every case below parses from a buffer of its own and overwrites that buffer before it generates:
// a surviving view reads `Z`s where it expected SQL, and never the literal the case expects. The
// last case frees the buffer instead of overwriting it, so that a sanitizer build has the read of
// freed memory itself to catch.

namespace {

    /** A parse together with the buffer it came from, overwritten and kept alive at its address. */
    struct ParseApartFromItsSql {
        /** Held by pointer so that moving this around leaves the buffer where it was. */
        std::unique_ptr<std::string> scrubbedSql;
        ParseResult parseResult;
    };

    ParseApartFromItsSql parseThenScrubTheSql(std::string_view sql) {
        auto buffer = std::make_unique<std::string>(sql);
        Tokenizer tokenizer;
        Parser parser;
        auto parseResult = parser.parse(tokenizer.tokenize(*buffer));
        REQUIRE(parseResult);
        std::fill(buffer->begin(), buffer->end(), 'Z');
        return ParseApartFromItsSql{std::move(buffer), std::move(parseResult)};
    }

    CodeGenResult generateAfterScrubbingTheSql(std::string_view sql) {
        const auto parsed = parseThenScrubTheSql(sql);
        CodeGenerator codeGenerator;
        return codeGenerator.generateNode(*parsed.parseResult.astNodePointer);
    }

}  // namespace

TEST_CASE("a column name survives the SQL it was parsed from") {
    REQUIRE(generateAfterScrubbingTheSql("SELECT a FROM t;").code == "auto rows = storage.select(&T::a);");
}

TEST_CASE("literals and qualified names survive the SQL they were parsed from") {
    REQUIRE(generateAfterScrubbingTheSql("SELECT t.a, 1, 2.5, 'x', x'0f', TRUE, :p FROM t;").code ==
            "auto rows = storage.select(columns(&T::a, 1, 2.5, \"x\", std::vector<char>{'\\x0f'}, true, p));");
}

TEST_CASE("a NEW reference survives the SQL it was parsed from") {
    REQUIRE(generateAfterScrubbingTheSql("SELECT new.a FROM t;").code ==
            "auto rows = storage.select(new_(&T::a), from<T>());");
}

TEST_CASE("the span a warning underlines survives the SQL it was parsed from") {
    const auto parsed = parseThenScrubTheSql("DROP VIEW v;");
    REQUIRE(parsed.parseResult.astNodePointer->sourceSpan.text == "DROP VIEW v");

    CodeGenerator codeGenerator;
    const auto result = codeGenerator.generateNode(*parsed.parseResult.astNodePointer);
    REQUIRE(result.code == "/* DROP VIEW: not supported as storage.drop_* in sqlite_orm */");
    REQUIRE(result.warnings.size() == 1);
    REQUIRE(result.warnings.at(0).message == "DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm "
                                             "sync_schema() applies to mapped tables/indexes/triggers, not views");
    REQUIRE(result.warnings.at(0).length == 11);
}

TEST_CASE("the CREATE VIEW header a warning underlines survives the SQL it was parsed from") {
    const auto parsed = parseThenScrubTheSql("CREATE TEMP VIEW v AS SELECT a FROM t;");
    const auto* createView = dynamic_cast<const CreateViewNode*>(parsed.parseResult.astNodePointer.get());
    REQUIRE(createView != nullptr);
    REQUIRE(createView->headerText == "CREATE TEMP VIEW");

    CodeGenerator codeGenerator;
    const auto result = codeGenerator.generateNode(*parsed.parseResult.astNodePointer);
    REQUIRE(result.warnings.size() == 2);
    REQUIRE(result.warnings.at(1).message ==
            "CREATE VIEW v: sqlite_orm views use C++26 reflection (make_view + [[= \"\xe2\x80\xa6\"_orm_name]]); "
            "this code requires C++26 and will not compile under the selected C++ standard");
    // `CREATE TEMP VIEW` as written, the two spaces between the keywords included.
    REQUIRE(result.warnings.at(1).length == 16);
}

TEST_CASE("codegen runs once the SQL buffer is freed") {
    ParseResult parseResult;
    {
        auto buffer = std::make_unique<std::string>("SELECT t.a, 'x' FROM t;");
        Tokenizer tokenizer;
        Parser parser;
        parseResult = parser.parse(tokenizer.tokenize(*buffer));
        REQUIRE(parseResult);
    }
    CodeGenerator codeGenerator;
    REQUIRE(codeGenerator.generateNode(*parseResult.astNodePointer).code ==
            "auto rows = storage.select(columns(&T::a, \"x\"));");
}
