#include "parser_tests_common.hpp"
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: CREATE TABLE - basic") {
    auto parseResult = parse("CREATE TABLE users (id INTEGER, name TEXT)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "users",
        {ColumnDef{"id", "INTEGER"}, ColumnDef{"name", "TEXT"}},
        false, {}));
}

TEST_CASE("parser: CREATE TABLE IF NOT EXISTS") {
    auto parseResult = parse("CREATE TABLE IF NOT EXISTS users (id INTEGER)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "users",
        {ColumnDef{"id", "INTEGER"}},
        true, {}));
}

TEST_CASE("parser: CREATE TABLE - multi-word type") {
    auto parseResult = parse("CREATE TABLE t (x UNSIGNED BIG INT)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"x", "UNSIGNED BIG INT"}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - type with size") {
    auto parseResult = parse("CREATE TABLE t (name VARCHAR(255))");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"name", "VARCHAR(255)"}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - type with precision") {
    auto parseResult = parse("CREATE TABLE t (price DECIMAL(10, 2))");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"price", "DECIMAL(10, 2)"}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - no type") {
    auto parseResult = parse("CREATE TABLE t (x)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"x", ""}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - many columns") {
    auto parseResult = parse("CREATE TABLE users (id INTEGER, name TEXT, email TEXT, age INTEGER)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "users",
        {ColumnDef{"id", "INTEGER"}, ColumnDef{"name", "TEXT"}, ColumnDef{"email", "TEXT"}, ColumnDef{"age", "INTEGER"}},
        false, {}));
}

TEST_CASE("parser: CREATE TABLE - schema prefix ignored") {
    auto parseResult = parse("CREATE TABLE main.users (id INTEGER)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "users", {ColumnDef{"id", "INTEGER"}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - semicolon allowed") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER);");
    REQUIRE(parseResult);
}

TEST_CASE("parser: CREATE TABLE - PRIMARY KEY") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER PRIMARY KEY)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER", true, false, false}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - PRIMARY KEY AUTOINCREMENT") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER PRIMARY KEY AUTOINCREMENT)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER", true, true, false}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - NOT NULL") {
    auto parseResult = parse("CREATE TABLE t (name TEXT NOT NULL)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"name", "TEXT", false, false, true}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - mixed constraints") {
    auto parseResult = parse(
        "CREATE TABLE users ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT, "
        "name TEXT NOT NULL, "
        "email TEXT)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "users",
        {
            ColumnDef{"id", "INTEGER", true, true, false},
            ColumnDef{"name", "TEXT", false, false, true},
            ColumnDef{"email", "TEXT", false, false, false},
        },
        false, {}));
}

TEST_CASE("parser: CREATE TABLE - PRIMARY KEY with conflict clause") {
    auto action = GENERATE(
        std::pair{"ROLLBACK", ConflictClause::rollback},
        std::pair{"ABORT", ConflictClause::abort},
        std::pair{"FAIL", ConflictClause::fail},
        std::pair{"IGNORE", ConflictClause::ignore},
        std::pair{"REPLACE", ConflictClause::replace});
    auto sql = "CREATE TABLE t (id INTEGER PRIMARY KEY ON CONFLICT " + std::string(action.first) + ")";
    auto parseResult = parse(sql);
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER", true, false, false, action.second}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - PRIMARY KEY conflict clause + AUTOINCREMENT") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER PRIMARY KEY ON CONFLICT REPLACE AUTOINCREMENT)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER", true, true, false, ConflictClause::replace}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - NOT NULL with conflict clause") {
    auto parseResult = parse("CREATE TABLE t (name TEXT NOT NULL ON CONFLICT ABORT)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"name", "TEXT", false, false, true}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - CONSTRAINT name prefix") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER CONSTRAINT pk PRIMARY KEY)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER", true, false, false}}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT integer") {
    auto parseResult = parse("CREATE TABLE t (x INTEGER DEFAULT 42)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "INTEGER", make_shared_node<IntegerLiteralNode>("42"))}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT string") {
    auto parseResult = parse("CREATE TABLE t (x TEXT DEFAULT 'hello')");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "TEXT", make_shared_node<StringLiteralNode>("'hello'"))}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT negative number") {
    auto parseResult = parse("CREATE TABLE t (x INTEGER DEFAULT -1)");
    auto expected = std::make_shared<UnaryOperatorNode>(
        UnaryOperator::minus, make_node<IntegerLiteralNode>("1"), SourceLocation{});
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "INTEGER", std::move(expected))}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT real") {
    auto parseResult = parse("CREATE TABLE t (x REAL DEFAULT 3.14)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "REAL", make_shared_node<RealLiteralNode>("3.14"))}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT TRUE") {
    auto parseResult = parse("CREATE TABLE t (x INTEGER DEFAULT TRUE)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "INTEGER", make_shared_node<BoolLiteralNode>(true))}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT NULL") {
    auto parseResult = parse("CREATE TABLE t (x TEXT DEFAULT NULL)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "TEXT", make_shared_node<NullLiteralNode>())}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT expression") {
    auto parseResult = parse("CREATE TABLE t (x TEXT DEFAULT (date('now')))");
    auto dateCall = std::make_shared<FunctionCallNode>(
        "date",
        []{
            std::vector<AstNodePointer> args;
            args.push_back(std::make_unique<StringLiteralNode>("'now'", SourceLocation{}));
            return args;
        }(),
        false, false, SourceLocation{});
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {column_with_default("x", "TEXT", std::move(dateCall))}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - DEFAULT with other constraints") {
    auto parseResult = parse("CREATE TABLE t (x INTEGER NOT NULL DEFAULT 0)");
    ColumnDef expected;
    expected.name = "x";
    expected.typeName = "INTEGER";
    expected.notNull = true;
    expected.defaultValue = make_shared_node<IntegerLiteralNode>("0");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - UNIQUE") {
    auto parseResult = parse("CREATE TABLE t (email TEXT UNIQUE)");
    ColumnDef expected;
    expected.name = "email";
    expected.typeName = "TEXT";
    expected.unique = true;
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - UNIQUE with conflict clause") {
    auto parseResult = parse("CREATE TABLE t (email TEXT UNIQUE ON CONFLICT IGNORE)");
    ColumnDef expected;
    expected.name = "email";
    expected.typeName = "TEXT";
    expected.unique = true;
    expected.uniqueConflict = ConflictClause::ignore;
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - CHECK constraint") {
    auto parseResult = parse("CREATE TABLE t (age INTEGER CHECK(age > 0))");
    ColumnDef expected;
    expected.name = "age";
    expected.typeName = "INTEGER";
    expected.checkExpression = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::greaterThan, make_node<ColumnRefNode>("age"), make_node<IntegerLiteralNode>("0"));
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - CHECK with complex expression") {
    auto parseResult = parse("CREATE TABLE t (x INTEGER CHECK(x >= 0 AND x <= 100))");
    ColumnDef expected;
    expected.name = "x";
    expected.typeName = "INTEGER";
    auto lhs = std::make_unique<BinaryOperatorNode>(
        BinaryOperator::greaterOrEqual, make_node<ColumnRefNode>("x"), make_node<IntegerLiteralNode>("0"), SourceLocation{});
    auto rhs = std::make_unique<BinaryOperatorNode>(
        BinaryOperator::lessOrEqual, make_node<ColumnRefNode>("x"), make_node<IntegerLiteralNode>("100"), SourceLocation{});
    expected.checkExpression = std::make_shared<BinaryOperatorNode>(
        BinaryOperator::logicalAnd, std::move(lhs), std::move(rhs), SourceLocation{});
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - CHECK with function") {
    auto parseResult = parse("CREATE TABLE t (name TEXT CHECK(length(name) > 0))");
    ColumnDef expected;
    expected.name = "name";
    expected.typeName = "TEXT";
    auto lengthCall = std::make_unique<FunctionCallNode>(
        "length",
        []{
            std::vector<AstNodePointer> args;
            args.push_back(std::make_unique<ColumnRefNode>("name", SourceLocation{}));
            return args;
        }(),
        false, false, SourceLocation{});
    expected.checkExpression = std::make_shared<BinaryOperatorNode>(
        BinaryOperator::greaterThan, std::move(lengthCall), make_node<IntegerLiteralNode>("0"), SourceLocation{});
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - COLLATE NOCASE") {
    auto parseResult = parse("CREATE TABLE t (name TEXT COLLATE NOCASE)");
    ColumnDef expected;
    expected.name = "name";
    expected.typeName = "TEXT";
    expected.collation = "NOCASE";
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - COLLATE BINARY") {
    auto parseResult = parse("CREATE TABLE t (name TEXT COLLATE BINARY)");
    ColumnDef expected;
    expected.name = "name";
    expected.typeName = "TEXT";
    expected.collation = "BINARY";
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - COLLATE RTRIM") {
    auto parseResult = parse("CREATE TABLE t (name TEXT COLLATE RTRIM)");
    ColumnDef expected;
    expected.name = "name";
    expected.typeName = "TEXT";
    expected.collation = "RTRIM";
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - CHECK + COLLATE + NOT NULL combined") {
    auto parseResult = parse("CREATE TABLE t (name TEXT NOT NULL CHECK(length(name) > 0) COLLATE NOCASE)");
    ColumnDef expected;
    expected.name = "name";
    expected.typeName = "TEXT";
    expected.notNull = true;
    auto lengthCall = std::make_unique<FunctionCallNode>(
        "length",
        []{
            std::vector<AstNodePointer> args;
            args.push_back(std::make_unique<ColumnRefNode>("name", SourceLocation{}));
            return args;
        }(),
        false, false, SourceLocation{});
    expected.checkExpression = std::make_shared<BinaryOperatorNode>(
        BinaryOperator::greaterThan, std::move(lengthCall), make_node<IntegerLiteralNode>("0"), SourceLocation{});
    expected.collation = "NOCASE";
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES simple") {
    auto parseResult = parse("CREATE TABLE posts (user_id INTEGER REFERENCES users(id))");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", "id"};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES without column") {
    auto parseResult = parse("CREATE TABLE posts (user_id INTEGER REFERENCES users)");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", ""};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES ON DELETE CASCADE") {
    auto parseResult = parse("CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE CASCADE)");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", "id", ForeignKeyAction::cascade, ForeignKeyAction::none};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES ON UPDATE SET NULL") {
    auto parseResult = parse("CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON UPDATE SET NULL)");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", "id", ForeignKeyAction::none, ForeignKeyAction::setNull};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES both actions") {
    auto parseResult = parse(
        "CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE CASCADE ON UPDATE SET DEFAULT)");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", "id", ForeignKeyAction::cascade, ForeignKeyAction::setDefault};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES NO ACTION") {
    auto parseResult = parse("CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE NO ACTION)");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", "id", ForeignKeyAction::noAction, ForeignKeyAction::none};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - REFERENCES RESTRICT") {
    auto parseResult = parse("CREATE TABLE posts (user_id INTEGER REFERENCES users(id) ON DELETE RESTRICT)");
    ColumnDef expected;
    expected.name = "user_id";
    expected.typeName = "INTEGER";
    expected.foreignKey = ForeignKeyClause{"users", "id", ForeignKeyAction::restrict_, ForeignKeyAction::none};
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts", {std::move(expected)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - table-level FOREIGN KEY") {
    auto parseResult = parse(
        "CREATE TABLE posts ("
        "id INTEGER PRIMARY KEY, "
        "user_id INTEGER, "
        "FOREIGN KEY(user_id) REFERENCES users(id))");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts",
        {ColumnDef{"id", "INTEGER", true}, ColumnDef{"user_id", "INTEGER"}},
        {TableForeignKey{"user_id", ForeignKeyClause{"users", "id"}}},
        false, {}));
}

TEST_CASE("parser: CREATE TABLE - table-level FOREIGN KEY with actions") {
    auto parseResult = parse(
        "CREATE TABLE posts ("
        "id INTEGER PRIMARY KEY, "
        "user_id INTEGER, "
        "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE ON UPDATE SET NULL)");
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "posts",
        {ColumnDef{"id", "INTEGER", true}, ColumnDef{"user_id", "INTEGER"}},
        {TableForeignKey{"user_id", ForeignKeyClause{"users", "id", ForeignKeyAction::cascade, ForeignKeyAction::setNull}}},
        false, {}));
}

// --- GENERATED ---

TEST_CASE("parser: CREATE TABLE - GENERATED ALWAYS AS STORED") {
    auto parseResult = parse("CREATE TABLE products (id INTEGER, full_name TEXT GENERATED ALWAYS AS (first_name || ' ' || last_name) STORED)");
    REQUIRE(parseResult);
    ColumnDef generated;
    generated.name = "full_name";
    generated.typeName = "TEXT";
    generated.generatedAlways = true;
    generated.generatedStorage = ColumnDef::GeneratedStorage::stored;
    generated.generatedExpression = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::concatenate,
        make_node<BinaryOperatorNode>(
            BinaryOperator::concatenate,
            make_node<ColumnRefNode>("first_name"),
            make_node<StringLiteralNode>("' '")),
        make_node<ColumnRefNode>("last_name"));
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "products", {ColumnDef{"id", "INTEGER"}, std::move(generated)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - AS VIRTUAL shorthand") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER, v TEXT AS (id + 1) VIRTUAL)");
    REQUIRE(parseResult);
    ColumnDef generated;
    generated.name = "v";
    generated.typeName = "TEXT";
    generated.generatedAlways = false;
    generated.generatedStorage = ColumnDef::GeneratedStorage::virtual_;
    generated.generatedExpression = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::add,
        make_node<ColumnRefNode>("id"),
        make_node<IntegerLiteralNode>("1"));
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER"}, std::move(generated)}, false, {}));
}

TEST_CASE("parser: CREATE TABLE - GENERATED ALWAYS AS no storage") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER, v TEXT GENERATED ALWAYS AS (id * 2))");
    REQUIRE(parseResult);
    ColumnDef generated;
    generated.name = "v";
    generated.typeName = "TEXT";
    generated.generatedAlways = true;
    generated.generatedStorage = ColumnDef::GeneratedStorage::none;
    generated.generatedExpression = make_shared_node<BinaryOperatorNode>(
        BinaryOperator::multiply,
        make_node<ColumnRefNode>("id"),
        make_node<IntegerLiteralNode>("2"));
    REQUIRE(require_node<CreateTableNode>(parseResult) == CreateTableNode(
        "t", {ColumnDef{"id", "INTEGER"}, std::move(generated)}, false, {}));
}

// --- Table-level PRIMARY KEY ---

TEST_CASE("parser: CREATE TABLE - table-level PRIMARY KEY") {
    auto parseResult = parse("CREATE TABLE t (a INTEGER, b TEXT, PRIMARY KEY (a, b))");
    REQUIRE(parseResult);
    CreateTableNode expected("t", {ColumnDef{"a", "INTEGER"}, ColumnDef{"b", "TEXT"}}, false, {});
    expected.primaryKeys = {TablePrimaryKey{{"a", "b"}}};
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TABLE - table-level PRIMARY KEY single column") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER, PRIMARY KEY (id))");
    REQUIRE(parseResult);
    CreateTableNode expected("t", {ColumnDef{"id", "INTEGER"}}, false, {});
    expected.primaryKeys = {TablePrimaryKey{{"id"}}};
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

// --- Table-level UNIQUE ---

TEST_CASE("parser: CREATE TABLE - table-level UNIQUE") {
    auto parseResult = parse("CREATE TABLE t (a INTEGER, b TEXT, UNIQUE (a, b))");
    REQUIRE(parseResult);
    CreateTableNode expected("t", {ColumnDef{"a", "INTEGER"}, ColumnDef{"b", "TEXT"}}, false, {});
    expected.uniques = {TableUnique{{"a", "b"}}};
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

// --- Table-level CHECK ---

TEST_CASE("parser: CREATE TABLE - table-level CHECK") {
    auto parseResult = parse("CREATE TABLE t (a INTEGER, b INTEGER, CHECK (a > 0 AND b > 0))");
    REQUIRE(parseResult);
    CreateTableNode expected("t", {ColumnDef{"a", "INTEGER"}, ColumnDef{"b", "INTEGER"}}, false, {});
    expected.checks = {TableCheck{make_shared_node<BinaryOperatorNode>(
        BinaryOperator::logicalAnd,
        make_node<BinaryOperatorNode>(BinaryOperator::greaterThan, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("0")),
        make_node<BinaryOperatorNode>(BinaryOperator::greaterThan, make_node<ColumnRefNode>("b"), make_node<IntegerLiteralNode>("0")))}};
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

// --- Mixed table-level constraints ---

TEST_CASE("parser: CREATE TABLE - mixed table-level constraints") {
    auto parseResult = parse("CREATE TABLE t (a INTEGER, b TEXT, c INTEGER, "
        "PRIMARY KEY (a, b), UNIQUE (b, c), CHECK (a > 0), "
        "FOREIGN KEY (c) REFERENCES other(id))");
    REQUIRE(parseResult);
    CreateTableNode expected("t",
        {ColumnDef{"a", "INTEGER"}, ColumnDef{"b", "TEXT"}, ColumnDef{"c", "INTEGER"}},
        {TableForeignKey{"c", ForeignKeyClause{"other", "id"}}},
        false, {});
    expected.primaryKeys = {TablePrimaryKey{{"a", "b"}}};
    expected.uniques = {TableUnique{{"b", "c"}}};
    expected.checks = {TableCheck{make_shared_node<BinaryOperatorNode>(
        BinaryOperator::greaterThan, make_node<ColumnRefNode>("a"), make_node<IntegerLiteralNode>("0"))}};
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

// --- CONSTRAINT name prefix for table constraints ---

TEST_CASE("parser: CREATE TABLE - CONSTRAINT name prefix for table PK") {
    auto parseResult = parse("CREATE TABLE t (a INTEGER, b INTEGER, CONSTRAINT pk_t PRIMARY KEY (a, b))");
    REQUIRE(parseResult);
    CreateTableNode expected("t", {ColumnDef{"a", "INTEGER"}, ColumnDef{"b", "INTEGER"}}, false, {});
    expected.primaryKeys = {TablePrimaryKey{{"a", "b"}}};
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

// --- WITHOUT ROWID ---

TEST_CASE("parser: CREATE TABLE - WITHOUT ROWID") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT NOT NULL) WITHOUT ROWID");
    REQUIRE(parseResult);
    ColumnDef idColumn;
    idColumn.name = "id";
    idColumn.typeName = "INTEGER";
    idColumn.primaryKey = true;
    ColumnDef nameColumn;
    nameColumn.name = "name";
    nameColumn.typeName = "TEXT";
    nameColumn.notNull = true;
    CreateTableNode expected("t", {std::move(idColumn), std::move(nameColumn)}, false, {});
    expected.withoutRowid = true;
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TABLE - without WITHOUT ROWID") {
    auto parseResult = parse("CREATE TABLE t (id INTEGER)");
    REQUIRE(parseResult);
    REQUIRE(require_node<CreateTableNode>(parseResult).withoutRowid == false);
}

// --- STRICT ---

TEST_CASE("parser: CREATE TABLE - STRICT") {
    auto parseResult = parse("CREATE TABLE t (a TEXT) STRICT");
    REQUIRE(parseResult);
    CreateTableNode expected("t", {ColumnDef{"a", "TEXT"}}, false, {});
    expected.strict = true;
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TABLE - WITHOUT ROWID, STRICT") {
    auto parseResult = parse("CREATE TABLE t (a TEXT PRIMARY KEY) WITHOUT ROWID, STRICT");
    REQUIRE(parseResult);
    ColumnDef aCol;
    aCol.name = "a";
    aCol.typeName = "TEXT";
    aCol.primaryKey = true;
    CreateTableNode expected("t", {std::move(aCol)}, false, {});
    expected.withoutRowid = true;
    expected.strict = true;
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}

// --- FK DEFERRABLE ---

TEST_CASE("parser: CREATE TABLE - FK DEFERRABLE INITIALLY DEFERRED") {
    auto parseResult = parse(
        "CREATE TABLE t (id INT, FOREIGN KEY (id) REFERENCES p(id) DEFERRABLE INITIALLY DEFERRED)");
    REQUIRE(parseResult);
    ForeignKeyClause fk;
    fk.table = "p";
    fk.column = "id";
    fk.deferrability = Deferrability::deferrable;
    fk.initially = InitialConstraintMode::deferred;
    CreateTableNode expected("t",
        {ColumnDef{"id", "INT"}},
        {TableForeignKey{"id", fk}},
        false, {});
    REQUIRE(require_node<CreateTableNode>(parseResult) == expected);
}
