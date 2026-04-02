#include "parser_tests_common.hpp"

using namespace sqlite2orm;
using namespace sqlite2orm::parser_test_helpers;

TEST_CASE("parser: CREATE TRIGGER after insert, body DELETE") {
    auto del = std::make_unique<DeleteNode>(SourceLocation{});
    del->tableName = "tbl";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerName = "tr";
    expected.timing = TriggerTiming::after;
    expected.eventKind = TriggerEventKind::insert_;
    expected.tableName = "tbl";
    expected.bodyStatements.push_back(std::move(del));

    auto parseResult = parse("CREATE TRIGGER tr AFTER INSERT ON tbl BEGIN DELETE FROM tbl; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER before delete") {
    auto del = std::make_unique<DeleteNode>(SourceLocation{});
    del->tableName = "u";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerName = "t1";
    expected.timing = TriggerTiming::before;
    expected.eventKind = TriggerEventKind::delete_;
    expected.tableName = "u";
    expected.bodyStatements.push_back(std::move(del));

    auto parseResult = parse("CREATE TRIGGER t1 BEFORE DELETE ON u BEGIN DELETE FROM u; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER instead of update of columns") {
    auto del = std::make_unique<DeleteNode>(SourceLocation{});
    del->tableName = "v";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerName = "iv";
    expected.timing = TriggerTiming::insteadOf;
    expected.eventKind = TriggerEventKind::updateOf;
    expected.updateOfColumns = {"a", "b"};
    expected.tableName = "v";
    expected.bodyStatements.push_back(std::move(del));

    auto parseResult = parse("CREATE TRIGGER iv INSTEAD OF UPDATE OF a, b ON v BEGIN DELETE FROM v; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER when and for each row") {
    auto del = std::make_unique<DeleteNode>(SourceLocation{});
    del->tableName = "t";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerName = "w";
    expected.timing = TriggerTiming::after;
    expected.eventKind = TriggerEventKind::update_;
    expected.tableName = "t";
    expected.forEachRow = true;
    expected.whenClause = makeNode<IntegerLiteralNode>("1");
    expected.bodyStatements.push_back(std::move(del));

    auto parseResult = parse("CREATE TRIGGER w AFTER UPDATE ON t FOR EACH ROW WHEN 1 BEGIN DELETE FROM t; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER if not exists and temp") {
    auto del = std::make_unique<DeleteNode>(SourceLocation{});
    del->tableName = "x";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerName = "tx";
    expected.ifNotExists = true;
    expected.temporary = true;
    expected.timing = TriggerTiming::before;
    expected.eventKind = TriggerEventKind::insert_;
    expected.tableName = "x";
    expected.bodyStatements.push_back(std::move(del));

    auto parseResult = parse("CREATE TEMP TRIGGER IF NOT EXISTS tx BEFORE INSERT ON x BEGIN DELETE FROM x; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER qualified names") {
    auto del = std::make_unique<DeleteNode>(SourceLocation{});
    del->tableName = "tbl";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerSchemaName = "main";
    expected.triggerName = "tr";
    expected.tableSchemaName = "main";
    expected.tableName = "tbl";
    expected.timing = TriggerTiming::after;
    expected.eventKind = TriggerEventKind::insert_;
    expected.bodyStatements.push_back(std::move(del));

    auto parseResult = parse("CREATE TRIGGER main.tr AFTER INSERT ON main.tbl BEGIN DELETE FROM tbl; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER two statements") {
    auto d1 = std::make_unique<DeleteNode>(SourceLocation{});
    d1->tableName = "t";
    auto d2 = std::make_unique<DeleteNode>(SourceLocation{});
    d2->tableName = "t";

    CreateTriggerNode expected(SourceLocation{});
    expected.triggerName = "multi";
    expected.timing = TriggerTiming::after;
    expected.eventKind = TriggerEventKind::insert_;
    expected.tableName = "t";
    expected.bodyStatements.push_back(std::move(d1));
    expected.bodyStatements.push_back(std::move(d2));

    auto parseResult = parse("CREATE TRIGGER multi AFTER INSERT ON t BEGIN DELETE FROM t; DELETE FROM t; END");
    REQUIRE(parseResult);
    REQUIRE(requireNode<CreateTriggerNode>(parseResult) == expected);
}

TEST_CASE("parser: CREATE TRIGGER empty body fails") {
    auto parseResult = parse("CREATE TRIGGER bad AFTER INSERT ON t BEGIN END");
    REQUIRE_FALSE(parseResult);
}

TEST_CASE("parser: RAISE(IGNORE) expression in SELECT result column") {
    auto parseResult = parse("SELECT RAISE(IGNORE);");
    REQUIRE(parseResult);
    SelectNode expected({});
    expected.columns = {SelectColumn{makeSharedNode<RaiseNode>(RaiseKind::ignore, nullptr), ""}};
    REQUIRE(requireNode<SelectNode>(parseResult) == expected);
}
