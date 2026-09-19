#include <sqlite2orm/tokenizer.h>
#include <catch2/catch_all.hpp>

#include <ostream>
#include <string>
#include <vector>

using namespace sqlite2orm;

namespace sqlite2orm {
    std::ostream& operator<<(std::ostream& os, const Token& token) {
        return os << tokenTypeName(token.type) << "('" << token.value << "')";
    }
}

namespace {
    std::vector<Token> tokenize(std::string_view sql) {
        Tokenizer tokenizer;
        return tokenizer.tokenize(sql);
    }

    std::string tokenizeError(std::string_view sql) {
        try {
            tokenize(sql);
        } catch (const TokenizeError& error) {
            return error.what();
        }
        return "no error";
    }
}

TEST_CASE("tokenizer: empty input") {
    REQUIRE(tokenize("") == std::vector<Token>{
                                {TokenType::eof},
                            });
}

TEST_CASE("tokenizer: whitespace only") {
    REQUIRE(tokenize("   \t\n  ") == std::vector<Token>{
                                         {TokenType::eof},
                                     });
}

TEST_CASE("tokenizer: vertical tab and form feed as whitespace") {
    REQUIRE(tokenize("42\v\f7") == std::vector<Token>{
                                       {TokenType::integerLiteral, "42"},
                                       {TokenType::integerLiteral, "7"},
                                       {TokenType::eof},
                                   });
}

TEST_CASE("tokenizer: carriage return and CRLF between tokens") {
    REQUIRE(tokenize("1\r2") == std::vector<Token>{
                                    {TokenType::integerLiteral, "1"},
                                    {TokenType::integerLiteral, "2"},
                                    {TokenType::eof},
                                });
    REQUIRE(tokenize("a\r\nb") == std::vector<Token>{
                                      {TokenType::identifier, "a"},
                                      {TokenType::identifier, "b"},
                                      {TokenType::eof},
                                  });
}

TEST_CASE("tokenizer: UTF-8 BOM skipped at start") {
    const std::string withBom = std::string("\xEF\xBB\xBF") + "SELECT 1";
    REQUIRE(tokenize(withBom) == std::vector<Token>{
                                     {TokenType::kwSelect, "SELECT"},
                                     {TokenType::integerLiteral, "1"},
                                     {TokenType::eof},
                                 });
}

TEST_CASE("tokenizer: keywords separated by newlines and spaces") {
    REQUIRE(tokenize("CREATE\n  TABLE\r\n  t (x)") == std::vector<Token>{
                                                          {TokenType::kwCreate, "CREATE"},
                                                          {TokenType::kwTable, "TABLE"},
                                                          {TokenType::identifier, "t"},
                                                          {TokenType::leftParen, "("},
                                                          {TokenType::identifier, "x"},
                                                          {TokenType::rightParen, ")"},
                                                          {TokenType::eof},
                                                      });
}

TEST_CASE("tokenizer: line comment") {
    REQUIRE(tokenize("-- this is a comment\n42") == std::vector<Token>{
                                                        {TokenType::integerLiteral, "42"},
                                                        {TokenType::eof},
                                                    });
}

TEST_CASE("tokenizer: block comment") {
    REQUIRE(tokenize("/* block */ 42") == std::vector<Token>{
                                              {TokenType::integerLiteral, "42"},
                                              {TokenType::eof},
                                          });
}

TEST_CASE("tokenizer: unterminated block comment") {
    REQUIRE_THROWS_AS(tokenize("/* unterminated"), TokenizeError);
}

TEST_CASE("tokenizer: integer literals") {
    SECTION("simple") {
        REQUIRE(tokenize("0 1 42 999999") == std::vector<Token>{
                                                 {TokenType::integerLiteral, "0"},
                                                 {TokenType::integerLiteral, "1"},
                                                 {TokenType::integerLiteral, "42"},
                                                 {TokenType::integerLiteral, "999999"},
                                                 {TokenType::eof},
                                             });
    }
    SECTION("hex") {
        REQUIRE(tokenize("0xFF 0X1A") == std::vector<Token>{
                                             {TokenType::integerLiteral, "0xFF"},
                                             {TokenType::integerLiteral, "0X1A"},
                                             {TokenType::eof},
                                         });
    }
}

TEST_CASE("tokenizer: real literals") {
    SECTION("with decimal point") {
        REQUIRE(tokenize("3.14 0.5 100.") == std::vector<Token>{
                                                 {TokenType::realLiteral, "3.14"},
                                                 {TokenType::realLiteral, "0.5"},
                                                 {TokenType::realLiteral, "100."},
                                                 {TokenType::eof},
                                             });
    }
    SECTION("starting with dot") {
        REQUIRE(tokenize(".5") == std::vector<Token>{
                                      {TokenType::realLiteral, ".5"},
                                      {TokenType::eof},
                                  });
    }
    SECTION("with exponent") {
        REQUIRE(tokenize("1e10 3.14e-2 .5E+3") == std::vector<Token>{
                                                      {TokenType::realLiteral, "1e10"},
                                                      {TokenType::realLiteral, "3.14e-2"},
                                                      {TokenType::realLiteral, ".5E+3"},
                                                      {TokenType::eof},
                                                  });
    }
    // SQLite turns the literal into a float on `.` alone, so the exponent still applies.
    SECTION("exponent right after the decimal point") {
        REQUIRE(tokenize("1.e5") == std::vector<Token>{
                                        {TokenType::realLiteral, "1.e5"},
                                        {TokenType::eof},
                                    });
    }
}

TEST_CASE("tokenizer: digit separators in numeric literals") {
    SECTION("decimal") {
        REQUIRE(tokenize("1_000_000") == std::vector<Token>{
                                             {TokenType::integerLiteral, "1_000_000"},
                                             {TokenType::eof},
                                         });
    }
    SECTION("hex") {
        REQUIRE(tokenize("0x1_ffff") == std::vector<Token>{
                                            {TokenType::integerLiteral, "0x1_ffff"},
                                            {TokenType::eof},
                                        });
    }
    SECTION("fraction") {
        REQUIRE(tokenize("3.141_592") == std::vector<Token>{
                                             {TokenType::realLiteral, "3.141_592"},
                                             {TokenType::eof},
                                         });
        REQUIRE(tokenize("1_000.000_1") == std::vector<Token>{
                                               {TokenType::realLiteral, "1_000.000_1"},
                                               {TokenType::eof},
                                           });
    }
    SECTION("exponent") {
        REQUIRE(tokenize("1e1_0") == std::vector<Token>{
                                         {TokenType::realLiteral, "1e1_0"},
                                         {TokenType::eof},
                                     });
    }
    SECTION("every position inside one literal") {
        REQUIRE(tokenize("1_000.000_1e1_0") == std::vector<Token>{
                                                   {TokenType::realLiteral, "1_000.000_1e1_0"},
                                                   {TokenType::eof},
                                               });
        REQUIRE(tokenize("9_223_372_036_854_775_807") == std::vector<Token>{
                                                             {TokenType::integerLiteral, "9_223_372_036_854_775_807"},
                                                             {TokenType::eof},
                                                         });
        REQUIRE(tokenize(".5_5") == std::vector<Token>{
                                        {TokenType::realLiteral, ".5_5"},
                                        {TokenType::eof},
                                    });
        REQUIRE(tokenize("1e+1_0") == std::vector<Token>{
                                          {TokenType::realLiteral, "1e+1_0"},
                                          {TokenType::eof},
                                      });
        REQUIRE(tokenize("0X1_F") == std::vector<Token>{
                                         {TokenType::integerLiteral, "0X1_F"},
                                         {TokenType::eof},
                                     });
    }
    SECTION("leading separator is an identifier, like in SQLite") {
        REQUIRE(tokenize("_100") == std::vector<Token>{
                                        {TokenType::identifier, "_100"},
                                        {TokenType::eof},
                                    });
    }
    // Real SQLite rejects each of these as `unrecognized token` instead of splitting them into a
    // number plus an identifier, verified with the sqlite3 3.51 CLI.
    SECTION("separator not surrounded by digits is rejected") {
        REQUIRE_THROWS_AS(tokenize("100_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1__0"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1__"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1_000_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1_.0"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1_e5"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1.2_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1._2"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1e_1"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1e1_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("1e1__0"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("0x_1f"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("0x1f_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("0x1__f"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("0x_"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("0_x1f"), TokenizeError);
        REQUIRE_THROWS_AS(tokenize("SELECT 100_;"), TokenizeError);
    }
    // The rejected run covers exactly the text real SQLite names in its own `unrecognized token`
    // message, so `1_.5` is one bad literal rather than `1_` followed by `.5`.
    SECTION("rejected literal spans the same text as in SQLite") {
        REQUIRE(tokenizeError("1__0") == "unrecognized token '1__0'");
        REQUIRE(tokenizeError("100_") == "unrecognized token '100_'");
        REQUIRE(tokenizeError("1_.") == "unrecognized token '1_.'");
        REQUIRE(tokenizeError("1_.5") == "unrecognized token '1_.5'");
        REQUIRE(tokenizeError("1_e5") == "unrecognized token '1_e5'");
        REQUIRE(tokenizeError("1.5_e3") == "unrecognized token '1.5_e3'");
        REQUIRE(tokenizeError("100_abc") == "unrecognized token '100_abc'");
        REQUIRE(tokenizeError("0x1f_") == "unrecognized token '0x1f_'");
        REQUIRE(tokenizeError("0x_1f") == "unrecognized token '0x_1f'");
    }
    SECTION("rejected literal reports its position") {
        try {
            tokenize("SELECT 1__0;");
            FAIL("expected a TokenizeError");
        } catch (const TokenizeError& error) {
            REQUIRE(std::string(error.what()) == "unrecognized token '1__0'");
            REQUIRE(error.location.line == 1);
            REQUIRE(error.location.column == 8);
        }
    }
}

// A hex literal a signed 64-bit integer cannot hold is still a valid token: SQLite raises
// `hex literal too big` in codeInteger(), when it compiles an expression, so the statements that
// never compile the value keep it — `CREATE TABLE t(x DEFAULT 0x10000000000000000)` and
// `PRAGMA user_version = 0x10000000000000000` are both accepted. Checked against sqlite3 3.51.
TEST_CASE("tokenizer: hex literal beyond the int64 range") {
    SECTION("sixteen significant digits") {
        REQUIRE(tokenize("0xFFFFFFFFFFFFFFFF") == std::vector<Token>{
                                                      {TokenType::integerLiteral, "0xFFFFFFFFFFFFFFFF"},
                                                      {TokenType::eof},
                                                  });
        REQUIRE(tokenize("0x0000FFFFFFFFFFFFFFFF") == std::vector<Token>{
                                                          {TokenType::integerLiteral, "0x0000FFFFFFFFFFFFFFFF"},
                                                          {TokenType::eof},
                                                      });
        REQUIRE(tokenize("0xFF_FF_FF_FF_FF_FF_FF_FF") == std::vector<Token>{
                                                             {TokenType::integerLiteral, "0xFF_FF_FF_FF_FF_FF_FF_FF"},
                                                             {TokenType::eof},
                                                         });
    }
    SECTION("a seventeenth one is a token all the same") {
        REQUIRE(tokenize("0x10000000000000000") == std::vector<Token>{
                                                       {TokenType::integerLiteral, "0x10000000000000000"},
                                                       {TokenType::eof},
                                                   });
        REQUIRE(tokenize("0x00001FFFFFFFFFFFFFFFF") == std::vector<Token>{
                                                           {TokenType::integerLiteral, "0x00001FFFFFFFFFFFFFFFF"},
                                                           {TokenType::eof},
                                                       });
        REQUIRE(tokenize("0x1_FF_FF_FF_FF_FF_FF_FF_FF") ==
                std::vector<Token>{
                    {TokenType::integerLiteral, "0x1_FF_FF_FF_FF_FF_FF_FF_FF"},
                    {TokenType::eof},
                });
        REQUIRE(tokenizeError("SELECT 0x10000000000000000;") == "no error");
    }
    SECTION("the digit separator rules still apply to it") {
        REQUIRE(tokenizeError("0x_10000000000000000") == "unrecognized token '0x_10000000000000000'");
        REQUIRE(tokenizeError("0x10000000000000000_") == "unrecognized token '0x10000000000000000_'");
    }
}

// Trailing identifier characters have always made a numeric literal illegal in SQLite, not just
// since digit separators arrived; `_` is one more identifier character that hits this rule.
TEST_CASE("tokenizer: numeric literal followed by identifier characters") {
    REQUIRE_THROWS_AS(tokenize("1a"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("1.x"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("1abc"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("1.2a"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("1_000a"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("0x1g"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("0x"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("1e"), TokenizeError);
    REQUIRE_THROWS_AS(tokenize("1e+"), TokenizeError);
}

TEST_CASE("tokenizer: string literals") {
    SECTION("simple") {
        REQUIRE(tokenize("'hello'") == std::vector<Token>{
                                           {TokenType::stringLiteral, "'hello'"},
                                           {TokenType::eof},
                                       });
    }
    SECTION("escaped quote") {
        REQUIRE(tokenize("'it''s'") == std::vector<Token>{
                                           {TokenType::stringLiteral, "'it''s'"},
                                           {TokenType::eof},
                                       });
    }
    SECTION("empty") {
        REQUIRE(tokenize("''") == std::vector<Token>{
                                      {TokenType::stringLiteral, "''"},
                                      {TokenType::eof},
                                  });
    }
    SECTION("unterminated") {
        REQUIRE_THROWS_AS(tokenize("'unterminated"), TokenizeError);
    }
}

TEST_CASE("tokenizer: blob literals") {
    REQUIRE(tokenize("X'48656C6C6F' x'AB'") == std::vector<Token>{
                                                   {TokenType::blobLiteral, "X'48656C6C6F'"},
                                                   {TokenType::blobLiteral, "x'AB'"},
                                                   {TokenType::eof},
                                               });
}

TEST_CASE("tokenizer: identifiers") {
    SECTION("simple") {
        REQUIRE(tokenize("foo bar_baz _private") == std::vector<Token>{
                                                        {TokenType::identifier, "foo"},
                                                        {TokenType::identifier, "bar_baz"},
                                                        {TokenType::identifier, "_private"},
                                                        {TokenType::eof},
                                                    });
    }
    SECTION("double-quoted") {
        REQUIRE(tokenize(R"("my column")") == std::vector<Token>{
                                                  {TokenType::identifier, R"("my column")"},
                                                  {TokenType::eof},
                                              });
    }
    SECTION("backtick-quoted") {
        REQUIRE(tokenize("`my column`") == std::vector<Token>{
                                               {TokenType::identifier, "`my column`"},
                                               {TokenType::eof},
                                           });
    }
    SECTION("bracket-quoted") {
        REQUIRE(tokenize("[my column]") == std::vector<Token>{
                                               {TokenType::identifier, "[my column]"},
                                               {TokenType::eof},
                                           });
    }
    SECTION("unterminated quoted") {
        REQUIRE_THROWS_AS(tokenize(R"("unterminated)"), TokenizeError);
    }
}

TEST_CASE("tokenizer: keywords") {
    SECTION("case insensitive") {
        REQUIRE(tokenize("SELECT") == std::vector<Token>{{TokenType::kwSelect, "SELECT"}, {TokenType::eof}});
        REQUIRE(tokenize("select") == std::vector<Token>{{TokenType::kwSelect, "select"}, {TokenType::eof}});
        REQUIRE(tokenize("Select") == std::vector<Token>{{TokenType::kwSelect, "Select"}, {TokenType::eof}});
    }
    SECTION("common keywords") {
        REQUIRE(tokenize("CREATE TABLE INSERT UPDATE DELETE FROM WHERE AND OR NOT NULL TRUE FALSE") ==
                std::vector<Token>{
                    {TokenType::kwCreate, "CREATE"},
                    {TokenType::kwTable, "TABLE"},
                    {TokenType::kwInsert, "INSERT"},
                    {TokenType::kwUpdate, "UPDATE"},
                    {TokenType::kwDelete, "DELETE"},
                    {TokenType::kwFrom, "FROM"},
                    {TokenType::kwWhere, "WHERE"},
                    {TokenType::kwAnd, "AND"},
                    {TokenType::kwOr, "OR"},
                    {TokenType::kwNot, "NOT"},
                    {TokenType::kwNull, "NULL"},
                    {TokenType::kwTrue, "TRUE"},
                    {TokenType::kwFalse, "FALSE"},
                    {TokenType::eof},
                });
    }
    SECTION("CURRENT_TIME, CURRENT_DATE, CURRENT_TIMESTAMP") {
        REQUIRE(tokenize("CURRENT_TIME CURRENT_DATE CURRENT_TIMESTAMP") ==
                std::vector<Token>{
                    {TokenType::kwCurrentTime, "CURRENT_TIME"},
                    {TokenType::kwCurrentDate, "CURRENT_DATE"},
                    {TokenType::kwCurrentTimestamp, "CURRENT_TIMESTAMP"},
                    {TokenType::eof},
                });
    }
}

TEST_CASE("tokenizer: operators") {
    SECTION("single-char") {
        REQUIRE(tokenize("+ - * / % & ~ ( ) , . ;") == std::vector<Token>{
                                                           {TokenType::plus, "+"},
                                                           {TokenType::minus, "-"},
                                                           {TokenType::star, "*"},
                                                           {TokenType::slash, "/"},
                                                           {TokenType::percent, "%"},
                                                           {TokenType::ampersand, "&"},
                                                           {TokenType::tilde, "~"},
                                                           {TokenType::leftParen, "("},
                                                           {TokenType::rightParen, ")"},
                                                           {TokenType::comma, ","},
                                                           {TokenType::dot, "."},
                                                           {TokenType::semicolon, ";"},
                                                           {TokenType::eof},
                                                       });
    }
    SECTION("two-char") {
        REQUIRE(tokenize("|| == != <> <= >= << >>") == std::vector<Token>{
                                                           {TokenType::pipe2, "||"},
                                                           {TokenType::eq2, "=="},
                                                           {TokenType::ne, "!="},
                                                           {TokenType::ltGt, "<>"},
                                                           {TokenType::le, "<="},
                                                           {TokenType::ge, ">="},
                                                           {TokenType::shiftLeft, "<<"},
                                                           {TokenType::shiftRight, ">>"},
                                                           {TokenType::eof},
                                                       });
    }
    SECTION("single vs double") {
        REQUIRE(tokenize("= < > |") == std::vector<Token>{
                                           {TokenType::eq, "="},
                                           {TokenType::lt, "<"},
                                           {TokenType::gt, ">"},
                                           {TokenType::pipe, "|"},
                                           {TokenType::eof},
                                       });
    }
    SECTION("arrow operators") {
        REQUIRE(tokenize("-> ->>") == std::vector<Token>{
                                          {TokenType::arrow, "->"},
                                          {TokenType::arrow2, "->>"},
                                          {TokenType::eof},
                                      });
    }
    SECTION("minus vs arrow") {
        REQUIRE(tokenize("-5 -> ->>x") == std::vector<Token>{
                                              {TokenType::minus, "-"},
                                              {TokenType::integerLiteral, "5"},
                                              {TokenType::arrow, "->"},
                                              {TokenType::arrow2, "->>"},
                                              {TokenType::identifier, "x"},
                                              {TokenType::eof},
                                          });
    }
}

TEST_CASE("tokenizer: bind parameters") {
    REQUIRE(tokenize("? ?1 ?123 :name @param $var") == std::vector<Token>{
                                                           {TokenType::bindParameter, "?"},
                                                           {TokenType::bindParameter, "?1"},
                                                           {TokenType::bindParameter, "?123"},
                                                           {TokenType::bindParameter, ":name"},
                                                           {TokenType::bindParameter, "@param"},
                                                           {TokenType::bindParameter, "$var"},
                                                           {TokenType::eof},
                                                       });
}

TEST_CASE("tokenizer: source locations") {
    auto tokens = tokenize("SELECT\n  *\nFROM t");
    REQUIRE(tokens.at(0) == Token{TokenType::kwSelect, "SELECT"});
    REQUIRE(tokens.at(0).location.line == 1);
    REQUIRE(tokens.at(0).location.column == 1);
    REQUIRE(tokens.at(1) == Token{TokenType::star, "*"});
    REQUIRE(tokens.at(1).location.line == 2);
    REQUIRE(tokens.at(1).location.column == 3);
    REQUIRE(tokens.at(2) == Token{TokenType::kwFrom, "FROM"});
    REQUIRE(tokens.at(2).location.line == 3);
    REQUIRE(tokens.at(2).location.column == 1);
}

TEST_CASE("tokenizer: CREATE TABLE statement") {
    REQUIRE(tokenize("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)") ==
            std::vector<Token>{
                {TokenType::kwCreate, "CREATE"},
                {TokenType::kwTable, "TABLE"},
                {TokenType::identifier, "users"},
                {TokenType::leftParen, "("},
                {TokenType::identifier, "id"},
                {TokenType::identifier, "INTEGER"},
                {TokenType::kwPrimary, "PRIMARY"},
                {TokenType::kwKey, "KEY"},
                {TokenType::kwAutoincrement, "AUTOINCREMENT"},
                {TokenType::comma, ","},
                {TokenType::identifier, "name"},
                {TokenType::identifier, "TEXT"},
                {TokenType::kwNot, "NOT"},
                {TokenType::kwNull, "NULL"},
                {TokenType::rightParen, ")"},
                {TokenType::eof},
            });
}

TEST_CASE("tokenizer: SELECT with expressions") {
    REQUIRE(tokenize("SELECT id, name FROM users WHERE id > 5 AND name LIKE 'A%'") ==
            std::vector<Token>{
                {TokenType::kwSelect, "SELECT"},
                {TokenType::identifier, "id"},
                {TokenType::comma, ","},
                {TokenType::identifier, "name"},
                {TokenType::kwFrom, "FROM"},
                {TokenType::identifier, "users"},
                {TokenType::kwWhere, "WHERE"},
                {TokenType::identifier, "id"},
                {TokenType::gt, ">"},
                {TokenType::integerLiteral, "5"},
                {TokenType::kwAnd, "AND"},
                {TokenType::identifier, "name"},
                {TokenType::kwLike, "LIKE"},
                {TokenType::stringLiteral, "'A%'"},
                {TokenType::eof},
            });
}

TEST_CASE("tokenizer: trigger with NEW/OLD") {
    REQUIRE(tokenize("CREATE TRIGGER t BEFORE INSERT ON tbl BEGIN SELECT NEW.col, OLD.col; END") ==
            std::vector<Token>{
                {TokenType::kwCreate, "CREATE"},
                {TokenType::kwTrigger, "TRIGGER"},
                {TokenType::identifier, "t"},
                {TokenType::kwBefore, "BEFORE"},
                {TokenType::kwInsert, "INSERT"},
                {TokenType::kwOn, "ON"},
                {TokenType::identifier, "tbl"},
                {TokenType::kwBegin, "BEGIN"},
                {TokenType::kwSelect, "SELECT"},
                {TokenType::identifier, "NEW"},
                {TokenType::dot, "."},
                {TokenType::identifier, "col"},
                {TokenType::comma, ","},
                {TokenType::identifier, "OLD"},
                {TokenType::dot, "."},
                {TokenType::identifier, "col"},
                {TokenType::semicolon, ";"},
                {TokenType::kwEnd, "END"},
                {TokenType::eof},
            });
}

TEST_CASE("tokenizer: ISNULL and NOTNULL keywords") {
    REQUIRE(tokenize("x ISNULL y NOTNULL") == std::vector<Token>{
                                                  {TokenType::identifier, "x"},
                                                  {TokenType::kwIsnull, "ISNULL"},
                                                  {TokenType::identifier, "y"},
                                                  {TokenType::kwNotnull, "NOTNULL"},
                                                  {TokenType::eof},
                                              });
}

TEST_CASE("tokenizer: unexpected character") {
    REQUIRE_THROWS_AS(tokenize("SELECT # FROM"), TokenizeError);
}

TEST_CASE("tokenizer: non-ASCII identifiers are accepted like SQLite") {
    // SQLite treats bytes >= 0x80 as identifier characters, so accented / non-Latin
    // identifiers tokenize as a single identifier rather than erroring.
    REQUIRE(tokenize("café") == std::vector<Token>{
                                    {TokenType::identifier, "café"},
                                    {TokenType::eof},
                                });
    REQUIRE(tokenize("привет") == std::vector<Token>{
                                      {TokenType::identifier, "привет"},
                                      {TokenType::eof},
                                  });
    REQUIRE(tokenize("CREATE TABLE café (x)") == std::vector<Token>{
                                                     {TokenType::kwCreate, "CREATE"},
                                                     {TokenType::kwTable, "TABLE"},
                                                     {TokenType::identifier, "café"},
                                                     {TokenType::leftParen, "("},
                                                     {TokenType::identifier, "x"},
                                                     {TokenType::rightParen, ")"},
                                                     {TokenType::eof},
                                                 });
}
