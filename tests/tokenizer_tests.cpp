#include <sqlite2orm/tokenizer.h>
#include <catch2/catch_all.hpp>

#include <ostream>
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
    const std::string with_bom = std::string("\xEF\xBB\xBF") + "SELECT 1";
    REQUIRE(tokenize(with_bom) == std::vector<Token>{
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
        REQUIRE(tokenize("CREATE TABLE INSERT UPDATE DELETE FROM WHERE AND OR NOT NULL TRUE FALSE") == std::vector<Token>{
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
        REQUIRE(tokenize("CURRENT_TIME CURRENT_DATE CURRENT_TIMESTAMP") == std::vector<Token>{
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
    REQUIRE(tokenize("CREATE TABLE users (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL)") == std::vector<Token>{
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
    REQUIRE(tokenize("SELECT id, name FROM users WHERE id > 5 AND name LIKE 'A%'") == std::vector<Token>{
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
    REQUIRE(tokenize("CREATE TRIGGER t BEFORE INSERT ON tbl BEGIN SELECT NEW.col, OLD.col; END") == std::vector<Token>{
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
