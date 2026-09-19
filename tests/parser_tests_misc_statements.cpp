#include "parser_tests_common.hpp"

using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: SAVEPOINT name") {
    auto result = parse("SAVEPOINT sp1;");
    requireNode<SavepointNode>(result);
    REQUIRE(requireNode<SavepointNode>(result) == SavepointNode({}, "sp1"));
}

TEST_CASE("parser: RELEASE SAVEPOINT name") {
    auto result = parse("RELEASE SAVEPOINT sp1;");
    REQUIRE(requireNode<ReleaseNode>(result) == ReleaseNode({}, "sp1"));
}

TEST_CASE("parser: RELEASE name (without SAVEPOINT keyword)") {
    auto result = parse("RELEASE sp1;");
    REQUIRE(requireNode<ReleaseNode>(result) == ReleaseNode({}, "sp1"));
}

TEST_CASE("parser: ROLLBACK TO SAVEPOINT name") {
    auto result = parse("ROLLBACK TO SAVEPOINT sp1;");
    const auto& node = requireNode<TransactionControlNode>(result);
    REQUIRE(node.kind == TransactionControlNode::Kind::rollback);
    REQUIRE(node.rollbackToSavepoint == "sp1");
}

TEST_CASE("parser: ROLLBACK TRANSACTION TO SAVEPOINT name") {
    auto result = parse("ROLLBACK TRANSACTION TO SAVEPOINT sp1;");
    const auto& node = requireNode<TransactionControlNode>(result);
    REQUIRE(node.kind == TransactionControlNode::Kind::rollback);
    REQUIRE(node.rollbackToSavepoint == "sp1");
}

TEST_CASE("parser: ROLLBACK TO name (without SAVEPOINT keyword)") {
    auto result = parse("ROLLBACK TO sp1;");
    const auto& node = requireNode<TransactionControlNode>(result);
    REQUIRE(node.rollbackToSavepoint == "sp1");
}

TEST_CASE("parser: ATTACH DATABASE") {
    auto result = parse("ATTACH DATABASE 'test.db' AS test_schema;");
    const auto& node = requireNode<AttachDatabaseNode>(result);
    REQUIRE(node.schemaName == "test_schema");
    REQUIRE(dynamic_cast<const StringLiteralNode*>(node.fileExpression.get()) != nullptr);
}

TEST_CASE("parser: ATTACH without DATABASE keyword") {
    auto result = parse("ATTACH 'data.db' AS aux;");
    const auto& node = requireNode<AttachDatabaseNode>(result);
    REQUIRE(node.schemaName == "aux");
}

TEST_CASE("parser: DETACH DATABASE") {
    auto result = parse("DETACH DATABASE aux;");
    REQUIRE(requireNode<DetachDatabaseNode>(result) == DetachDatabaseNode({}, "aux"));
}

TEST_CASE("parser: DETACH without DATABASE keyword") {
    auto result = parse("DETACH aux;");
    REQUIRE(requireNode<DetachDatabaseNode>(result) == DetachDatabaseNode({}, "aux"));
}

TEST_CASE("parser: ANALYZE bare") {
    auto result = parse("ANALYZE;");
    auto expected = AnalyzeNode({});
    REQUIRE(requireNode<AnalyzeNode>(result) == expected);
}

TEST_CASE("parser: ANALYZE table") {
    auto result = parse("ANALYZE users;");
    auto expected = AnalyzeNode({});
    expected.schemaOrTableName = "users";
    REQUIRE(requireNode<AnalyzeNode>(result) == expected);
}

TEST_CASE("parser: ANALYZE schema.table") {
    auto result = parse("ANALYZE main.users;");
    auto expected = AnalyzeNode({});
    expected.schemaOrTableName = "main";
    expected.tableName = "users";
    REQUIRE(requireNode<AnalyzeNode>(result) == expected);
}

TEST_CASE("parser: REINDEX bare") {
    auto result = parse("REINDEX;");
    auto expected = ReindexNode({});
    REQUIRE(requireNode<ReindexNode>(result) == expected);
}

TEST_CASE("parser: REINDEX collation") {
    auto result = parse("REINDEX NOCASE;");
    auto expected = ReindexNode({});
    expected.schemaOrObjectName = "NOCASE";
    REQUIRE(requireNode<ReindexNode>(result) == expected);
}

TEST_CASE("parser: REINDEX schema.table") {
    auto result = parse("REINDEX main.users;");
    auto expected = ReindexNode({});
    expected.schemaOrObjectName = "main";
    expected.objectName = "users";
    REQUIRE(requireNode<ReindexNode>(result) == expected);
}

TEST_CASE("parser: PRAGMA query") {
    auto result = parse("PRAGMA table_info;");
    auto expected = PragmaNode({});
    expected.pragmaName = "table_info";
    REQUIRE(requireNode<PragmaNode>(result) == expected);
}

TEST_CASE("parser: PRAGMA with value") {
    auto result = parse("PRAGMA journal_mode = WAL;");
    const auto& node = requireNode<PragmaNode>(result);
    REQUIRE(node.pragmaName == "journal_mode");
    REQUIRE(node.value != nullptr);
}

TEST_CASE("parser: PRAGMA with parenthesized value") {
    auto result = parse("PRAGMA table_info('users');");
    const auto& node = requireNode<PragmaNode>(result);
    REQUIRE(node.pragmaName == "table_info");
    REQUIRE(node.value != nullptr);
}

TEST_CASE("parser: PRAGMA schema.name") {
    auto result = parse("PRAGMA main.journal_mode;");
    const auto& node = requireNode<PragmaNode>(result);
    REQUIRE(node.schemaName == "main");
    REQUIRE(node.pragmaName == "journal_mode");
}

TEST_CASE("parser: EXPLAIN SELECT") {
    auto result = parse("EXPLAIN SELECT 1;");
    const auto& node = requireNode<ExplainNode>(result);
    REQUIRE_FALSE(node.queryPlan);
    REQUIRE(dynamic_cast<const SelectNode*>(node.statement.get()) != nullptr);
}

TEST_CASE("parser: EXPLAIN QUERY PLAN SELECT") {
    auto result = parse("EXPLAIN QUERY PLAN SELECT * FROM users;");
    const auto& node = requireNode<ExplainNode>(result);
    REQUIRE(node.queryPlan);
    REQUIRE(dynamic_cast<const SelectNode*>(node.statement.get()) != nullptr);
}

TEST_CASE("parser: EXPLAIN INSERT") {
    auto result = parse("EXPLAIN INSERT INTO t (a) VALUES (1);");
    const auto& node = requireNode<ExplainNode>(result);
    REQUIRE_FALSE(node.queryPlan);
    REQUIRE(dynamic_cast<const InsertNode*>(node.statement.get()) != nullptr);
}

TEST_CASE("parser: ORDER BY with NULLS FIRST") {
    auto result = parse("SELECT * FROM users ORDER BY name ASC NULLS FIRST;");
    const auto& node = requireNode<SelectNode>(result);
    REQUIRE(node.orderBy.size() == 1);
    REQUIRE(node.orderBy.at(0).nulls == NullsOrdering::first);
}

TEST_CASE("parser: ORDER BY with NULLS LAST") {
    auto result = parse("SELECT * FROM users ORDER BY name DESC NULLS LAST;");
    const auto& node = requireNode<SelectNode>(result);
    REQUIRE(node.orderBy.size() == 1);
    REQUIRE(node.orderBy.at(0).nulls == NullsOrdering::last);
}

TEST_CASE("parser: INSERT with RETURNING") {
    auto result = parse("INSERT INTO t (a) VALUES (1) RETURNING id, name AS n;");
    const auto& node = requireNode<InsertNode>(result);
    REQUIRE(node.returning.size() == 2);
    REQUIRE(node.returning.at(0).alias.empty());
    REQUIRE(node.returning.at(1).alias == "n");
}

TEST_CASE("parser: UPDATE with RETURNING *") {
    auto result = parse("UPDATE t SET a = 1 RETURNING *;");
    const auto& node = requireNode<UpdateNode>(result);
    REQUIRE(node.returning.size() == 1);
}

TEST_CASE("parser: DELETE with RETURNING") {
    auto result = parse("DELETE FROM t WHERE id = 1 RETURNING id;");
    auto expectedAst = std::make_unique<DeleteNode>(SourceLocation{});
    expectedAst->tableName = "t";
    expectedAst->whereClause = makeNode<BinaryOperatorNode>(BinaryOperator::equals,
                                                            makeNode<ColumnRefNode>("id"),
                                                            makeNode<IntegerLiteralNode>("1"));
    expectedAst->returning.push_back(ReturningColumn{makeNode<ColumnRefNode>("id"), ""});
    REQUIRE(result == ParseResult{std::move(expectedAst), {}});
}

TEST_CASE("parser: parseAll splits multiple statements on semicolons") {
    Tokenizer tokenizer;
    Parser parser;
    auto results = parser.parseAll(tokenizer.tokenize("SELECT 1; SELECT 2;"));
    std::vector<ParseResult> expected;
    expected.push_back(parse("SELECT 1;"));
    expected.push_back(parse("SELECT 2;"));
    REQUIRE(results == expected);
}

TEST_CASE("parser: parseAll handles CREATE TABLE + INSERT") {
    Tokenizer tokenizer;
    Parser parser;
    auto results =
        parser.parseAll(tokenizer.tokenize("CREATE TABLE t (id INTEGER PRIMARY KEY); INSERT INTO t (id) VALUES (1);"));
    std::vector<ParseResult> expected;
    expected.push_back(parse("CREATE TABLE t (id INTEGER PRIMARY KEY);"));
    expected.push_back(parse("INSERT INTO t (id) VALUES (1);"));
    REQUIRE(results == expected);
}

TEST_CASE("parser: parseAll empty input returns no results") {
    Tokenizer tokenizer;
    Parser parser;
    auto results = parser.parseAll(tokenizer.tokenize(";;;"));
    REQUIRE(results == std::vector<ParseResult>{});
}

TEST_CASE("parser: parseAll single statement without trailing semicolon") {
    Tokenizer tokenizer;
    Parser parser;
    auto results = parser.parseAll(tokenizer.tokenize("SELECT 42"));
    std::vector<ParseResult> expected;
    expected.push_back(parse("SELECT 42"));
    REQUIRE(results == expected);
}

// SQLite never compiles a PRAGMA value, it reads the value's text, and its `nmnum` rule takes a
// bare name: every keyword its parser falls back to an identifier for stands as a value, and `ON`,
// `DELETE` and `DEFAULT` are named in the rule on top of those. Only the reserved words below are
// a syntax error there. The two lists are what sqlite3 3.51 answers for
// `PRAGMA recursive_triggers = <keyword>;` over every keyword this tokenizer knows.
TEST_CASE("parser: the keywords SQLite refuses as a PRAGMA value") {
    const std::vector<std::string> allKeywords{"abort",
                                               "action",
                                               "add",
                                               "after",
                                               "all",
                                               "alter",
                                               "always",
                                               "analyze",
                                               "and",
                                               "as",
                                               "asc",
                                               "attach",
                                               "autoincrement",
                                               "before",
                                               "begin",
                                               "between",
                                               "by",
                                               "cascade",
                                               "case",
                                               "cast",
                                               "check",
                                               "collate",
                                               "column",
                                               "commit",
                                               "conflict",
                                               "constraint",
                                               "create",
                                               "cross",
                                               "current",
                                               "current_date",
                                               "current_time",
                                               "current_timestamp",
                                               "database",
                                               "default",
                                               "deferrable",
                                               "deferred",
                                               "delete",
                                               "desc",
                                               "detach",
                                               "distinct",
                                               "do",
                                               "drop",
                                               "each",
                                               "else",
                                               "end",
                                               "escape",
                                               "except",
                                               "exclude",
                                               "excluded",
                                               "exclusive",
                                               "exists",
                                               "explain",
                                               "fail",
                                               "false",
                                               "filter",
                                               "first",
                                               "following",
                                               "for",
                                               "foreign",
                                               "from",
                                               "full",
                                               "generated",
                                               "glob",
                                               "group",
                                               "groups",
                                               "having",
                                               "if",
                                               "ignore",
                                               "immediate",
                                               "in",
                                               "index",
                                               "indexed",
                                               "initially",
                                               "inner",
                                               "insert",
                                               "instead",
                                               "intersect",
                                               "into",
                                               "is",
                                               "isnull",
                                               "join",
                                               "key",
                                               "last",
                                               "left",
                                               "like",
                                               "limit",
                                               "match",
                                               "materialized",
                                               "natural",
                                               "no",
                                               "not",
                                               "nothing",
                                               "notnull",
                                               "null",
                                               "nulls",
                                               "of",
                                               "offset",
                                               "on",
                                               "or",
                                               "order",
                                               "others",
                                               "outer",
                                               "over",
                                               "partition",
                                               "plan",
                                               "pragma",
                                               "preceding",
                                               "primary",
                                               "query",
                                               "raise",
                                               "range",
                                               "recursive",
                                               "references",
                                               "regexp",
                                               "reindex",
                                               "release",
                                               "rename",
                                               "replace",
                                               "restrict",
                                               "returning",
                                               "right",
                                               "rollback",
                                               "row",
                                               "rows",
                                               "savepoint",
                                               "select",
                                               "set",
                                               "stored",
                                               "strict",
                                               "table",
                                               "temp",
                                               "temporary",
                                               "then",
                                               "ties",
                                               "to",
                                               "transaction",
                                               "trigger",
                                               "true",
                                               "unbounded",
                                               "union",
                                               "unique",
                                               "update",
                                               "using",
                                               "vacuum",
                                               "values",
                                               "view",
                                               "virtual",
                                               "when",
                                               "where",
                                               "window",
                                               "with",
                                               "without"};
    // `NULL` is the one reserved word missing from the list: SQLite calls it a syntax error too,
    // but the parser keeps it so that codegen can name the PRAGMA it cannot set — see
    // "processSql: PRAGMA recursive_triggers = NULL".
    std::vector<std::string> refused;
    for (const std::string& keyword: allKeywords) {
        if (!parse("PRAGMA recursive_triggers = " + keyword + ";")) {
            refused.push_back(keyword);
        }
    }
    REQUIRE(refused == std::vector<std::string>{
                           "add",      "all",   "alter",   "and",     "as",          "autoincrement", "between",
                           "case",     "check", "collate", "commit",  "constraint",  "create",        "deferrable",
                           "distinct", "drop",  "else",    "escape",  "except",      "exists",        "foreign",
                           "from",     "group", "having",  "in",      "index",       "insert",        "intersect",
                           "into",     "is",    "isnull",  "join",    "limit",       "not",           "nothing",
                           "notnull",  "or",    "order",   "primary", "references",  "returning",     "select",
                           "set",      "table", "then",    "to",      "transaction", "union",         "unique",
                           "update",   "using", "values",  "when",    "where"});
}

TEST_CASE("parser: PRAGMA with a keyword value") {
    auto result = parse("PRAGMA recursive_triggers = no;");
    auto expected = PragmaNode({});
    expected.pragmaName = "recursive_triggers";
    expected.value = makeNode<ColumnRefNode>(std::string_view{"no"});
    REQUIRE(requireNode<PragmaNode>(result) == expected);
}

TEST_CASE("parser: PRAGMA with a parenthesized keyword value") {
    auto result = parse("PRAGMA table_info(row);");
    auto expected = PragmaNode({});
    expected.pragmaName = "table_info";
    expected.value = makeNode<ColumnRefNode>(std::string_view{"row"});
    REQUIRE(requireNode<PragmaNode>(result) == expected);
}
