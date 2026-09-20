#include "codegen_pragma.h"
#include "codegen_context.h"
#include "codegen_utils.h"

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <cstdint>
#include <limits>

namespace sqlite2orm {

    namespace {

        /** The hex literal of `PRAGMA name = <value>`, as SQLite names it, when an int64 cannot hold it. */
        std::optional<std::string> pragmaValueHexLiteralTooBig(const AstNode& valueNode) {
            const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&valueNode);
            if (integerLiteral && hexLiteralExceedsInt64(integerLiteral->value)) {
                return withoutDigitSeparators(integerLiteral->value);
            }
            return std::nullopt;
        }

        /**
         *  Whether generating `valueNode` as it is written passes on the very int32
         *  `sqlite3GetInt32()` reads from it, so that the generated call runs the statement SQLite
         *  itself runs. An integer literal `sqlite3GetInt32()` accepts denotes exactly the value it
         *  answers — it reads the same digits C++ does — and so does a decimal one under a minus
         *  sign. A hexadecimal literal under a minus sign does not: the sign sends
         *  `sqlite3GetInt32()` down its decimal branch, where `-0x10` is 0 and not -16.
         */
        bool pragmaValueSpellsItsInt32(const AstNode& valueNode) {
            if (dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
                return true;
            }
            if (const auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&valueNode)) {
                if (unaryOperator->unaryOperator == UnaryOperator::minus && unaryOperator->operand) {
                    const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(unaryOperator->operand.get());
                    return integerLiteral != nullptr && !isHexadecimalIntegerLiteral(integerLiteral->value);
                }
            }
            return false;
        }

        /**
         *  Whether `valueNode` is an integer literal, the minus and plus signs folded into it
         *  included. That is the one value the PRAGMAs with a reader of their own can pass on as
         *  written: the reader takes the digits the generated C++ literal spells, while a real
         *  literal already parts ways with it — `max_page_count = 1.5` sets nothing at all, because
         *  `sqlite3DecOrHexToI64()` refuses a text with a dot in it.
         */
        bool pragmaValueIsIntegerLiteral(const AstNode& valueNode) {
            std::size_t foldedSigns = 0;
            const AstNode* literal = withoutFoldedSigns(valueNode, foldedSigns);
            return literal != nullptr && dynamic_cast<const IntegerLiteralNode*>(literal) != nullptr;
        }

        /**
         *  Whether the number a PRAGMA's reader answers reaches sqlite_orm's setter unchanged.
         *  Every one of those setters is declared as taking an `int` — `void max_page_count(int)` —
         *  so a value past that range never gets there: the narrowing at the call is silent, and
         *  the limit the program ends up asking for is not the one SQLite reads. `max_page_count`
         *  is the only one of these readers that reaches past an int32 at all, clamping at
         *  0xfffffffe, so the upper half of the limits SQLite accepts has no call to go out in.
         */
        bool pragmaSetterTakes(std::int64_t readValue) {
            return readValue >= std::numeric_limits<std::int32_t>::min() &&
                   readValue <= std::numeric_limits<std::int32_t>::max();
        }

    }  // namespace

    PragmaCodeGenerator::PragmaCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context) :
        coordinator(coordinator), context(context) {}

    CodeGenResult PragmaCodeGenerator::codegenPragmaStatement(const PragmaNode& node) {
        const std::string name = toLowerAscii(node.pragmaName);
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;

        auto mergeSub = [&](CodeGenResult sub) {
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(sub.decisionPoints.begin()),
                                  std::make_move_iterator(sub.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(sub.warnings.begin()),
                            std::make_move_iterator(sub.warnings.end()));
            return std::move(sub.code);
        };

        if (name == "module_list") {
            return CodeGenResult{"storage.pragma.module_list();", {}, {}};
        }
        if (name == "quick_check") {
            return CodeGenResult{"storage.pragma.quick_check();", {}, {}};
        }
        if (name == "table_info") {
            if (!node.value) {
                this->context.accumulatedErrors.push_back("PRAGMA table_info requires a table name");
                return CodeGenResult{"/* PRAGMA table_info */"};
            }
            if (auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.table_info(" + *lit + ");", {}, {}};
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA table_info: use a string literal or identifier for the table name");
            return CodeGenResult{"/* PRAGMA table_info */"};
        }
        if (name == "table_xinfo") {
            if (!node.value) {
                this->context.accumulatedErrors.push_back("PRAGMA table_xinfo requires a table name");
                return CodeGenResult{"/* PRAGMA table_xinfo */"};
            }
            if (auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.table_xinfo(" + *lit + ");", {}, {}};
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA table_xinfo: use a string literal or identifier for the table name");
            return CodeGenResult{"/* PRAGMA table_xinfo */"};
        }
        if (name == "integrity_check") {
            if (!node.value) {
                return CodeGenResult{"storage.pragma.integrity_check();", {}, {}};
            }
            if (const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(node.value.get())) {
                if (!sqlitePragmaInt32(withoutDigitSeparators(integerLiteral->value))) {
                    // SQLite reads the value with `sqlite3GetInt32()` and takes it for a table name
                    // when it does not fit an int32, so `= 2147483648` and `= 0x80000000` are both
                    // `no such table`, exactly as `= 0x10000000000000000` is.
                    this->context.accumulatedErrors.push_back(
                        "PRAGMA integrity_check = " + withoutDigitSeparators(integerLiteral->value) +
                        ": SQLite cannot read this literal as a 32-bit integer and refuses it as a table name");
                    return CodeGenResult{"/* PRAGMA integrity_check */"};
                }
                return CodeGenResult{"storage.pragma.integrity_check(" + integerLiteralToCpp(integerLiteral->value) +
                                         ");",
                                     {},
                                     {}};
            }
            if (auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.integrity_check(" + *lit + ");", {}, {}};
            }
            warnings.push_back(pragmaValueWarning(
                "PRAGMA integrity_check argument is emitted via subexpression codegen; ensure it matches "
                "sqlite_orm::pragma_t::integrity_check overloads",
                *node.value));
            std::string arg = mergeSub(this->coordinator.generateNode(*node.value));
            return CodeGenResult{"storage.pragma.integrity_check(" + std::move(arg) + ");",
                                 std::move(decisionPoints),
                                 std::move(warnings)};
        }
        if (name == "busy_timeout" || name == "application_id" || name == "user_version") {
            if (!node.value) {
                return CodeGenResult{"storage.pragma." + name + "();", {}, {}};
            }
            // A PRAGMA value is not an expression: SQLite never compiles it, so it accepts a hex
            // literal it refuses in a query, and it reads the value's text with `sqlite3Atoi()` —
            // which answers 0 for everything that does not fit an int32, a name and a string
            // included. Whatever the SQL spells, the generated call has to pass that int32 on, or
            // the header sets something SQLite never would.
            if (auto value = pragmaValue(*node.value)) {
                // SQLite refuses a `_` digit separator in a PRAGMA value outright, a standing
                // difference of its own; the separators a numeric literal carries here go away
                // before it is read, the way the generated C++ literal drops its own. A string is
                // read with them, though — `sqlite3Atoi()` stops at the `_` of `'1_2'` and answers
                // 1 — so only a numeric literal is stripped.
                const std::string numericText = numericLiteralSqlText(*node.value);
                const std::optional<std::int32_t> readValue =
                    sqlitePragmaInt32(numericText.empty() ? value->text : numericText);
                if (!readValue || !pragmaValueSpellsItsInt32(*node.value)) {
                    const std::string prefix =
                        "PRAGMA " + name + " = " + value->sqlText + ": SQLite reads a PRAGMA value as a 32-bit integer";
                    warnings.push_back(pragmaValueWarning(readValue
                                                              ? prefix + ", so it sets " + std::to_string(*readValue)
                                                              : prefix + " and cannot read this one, so it sets 0",
                                                          *value));
                    return CodeGenResult{"storage.pragma." + name + "(" + std::to_string(readValue.value_or(0)) + ");",
                                         {},
                                         std::move(warnings)};
                }
                std::string arg = mergeSub(this->coordinator.generateNode(*node.value));
                return CodeGenResult{"storage.pragma." + name + "(" + std::move(arg) + ");",
                                     std::move(decisionPoints),
                                     std::move(warnings)};
            }
            this->context.accumulatedErrors.push_back("PRAGMA " + name + " = …: expected a number, a string or a name");
            return CodeGenResult{"/* PRAGMA " + name + " */"};
        }
        if (name == "synchronous" || name == "auto_vacuum" || name == "max_page_count") {
            if (!node.value) {
                return CodeGenResult{"storage.pragma." + name + "();", {}, {}};
            }
            // These three read their value with a function of their own rather than with
            // `sqlite3Atoi()`, so the int32 above does not describe them: `getSafetyLevel()` falls
            // back to 1 for anything that does not start with a digit, `getAutoVacuum()` knows the
            // names `none`/`full`/`incremental`, and `max_page_count` reads the whole text as an
            // int64 and clamps it to 0xfffffffe — `= 0x80000000` really does set 2147483648 there.
            // All three still read a hex literal past the int64 range as 0, the way #32 left them.
            if (auto tooBig = pragmaValueHexLiteralTooBig(*node.value)) {
                warnings.push_back(pragmaValueWarning(
                    "PRAGMA " + name + " = " + *tooBig +
                        ": SQLite reads a PRAGMA value as a 32-bit integer and this hex literal does not fit "
                        "one, so it sets 0",
                    *node.value));
                return CodeGenResult{"storage.pragma." + name + "(0);", {}, std::move(warnings)};
            }
            // A PRAGMA value is a name as readily as a number — SQLite's `nmnum` rule takes either —
            // and each of these three readers knows names of its own: `getSafetyLevel()` reads
            // `full` as 2 and `extra` as 3, `getAutoVacuum()` reads `none`/`full`/`incremental` as
            // 0/1/2, and `max_page_count` refuses a name and reports the limit rather than setting
            // one. A name is no C++ expression — it used to leave here as `&User::full` — so every
            // value but the integer literal that spells itself goes out as the number its reader
            // answers for the text.
            if (!pragmaValueIsIntegerLiteral(*node.value)) {
                const std::optional<PragmaValue> value = pragmaValue(*node.value);
                if (!value) {
                    this->context.accumulatedErrors.push_back("PRAGMA " + name +
                                                              " = …: expected a number, a string or a name");
                    return CodeGenResult{"/* PRAGMA " + name + " */"};
                }
                const std::int64_t readValue = name == "synchronous"   ? sqlitePragmaSafetyLevel(value->text)
                                               : name == "auto_vacuum" ? sqlitePragmaAutoVacuum(value->text)
                                                                       : sqlitePragmaMaxPageCount(value->text);
                const std::string prefix =
                    "PRAGMA " + name + " = " + value->sqlText + ": SQLite reads a PRAGMA value as text, so ";
                if (name == "max_page_count" && readValue == 0) {
                    warnings.push_back(
                        pragmaValueWarning(prefix + "this one leaves the limit alone and only reports it", *value));
                    return CodeGenResult{"storage.pragma." + name + "(0);", {}, std::move(warnings)};
                }
                // Naming the number in the warning and then handing it to a setter that cannot
                // hold it would be a diagnostic the generated code contradicts, so the statement
                // generates nothing at all rather than a call that silently sets something else.
                if (!pragmaSetterTakes(readValue)) {
                    warnings.push_back(pragmaValueWarning(prefix + "it sets " + std::to_string(readValue) +
                                                              ", which sqlite_orm's " + name + "(int) cannot pass on",
                                                          *value));
                    return CodeGenResult{"/* PRAGMA " + name + " */", {}, std::move(warnings)};
                }
                warnings.push_back(pragmaValueWarning(prefix + "it sets " + std::to_string(readValue), *value));
                return CodeGenResult{"storage.pragma." + name + "(" + std::to_string(readValue) + ");",
                                     {},
                                     std::move(warnings)};
            }
            std::string arg = mergeSub(this->coordinator.generateNode(*node.value));
            return CodeGenResult{"storage.pragma." + name + "(" + std::move(arg) + ");",
                                 std::move(decisionPoints),
                                 std::move(warnings)};
        }
        if (name == "recursive_triggers") {
            if (!node.value) {
                return CodeGenResult{"storage.pragma.recursive_triggers();", {}, {}};
            }
            if (auto value = pragmaValue(*node.value)) {
                const bool boolValue = sqlitePragmaBoolean(value->text);
                if (!isCanonicalPragmaBooleanText(value->text)) {
                    warnings.push_back(pragmaValueWarning(
                        "PRAGMA recursive_triggers = " + value->sqlText + ": SQLite reads this as " +
                            (boolValue ? "true" : "false") + "; spell it 0/1, TRUE/FALSE or ON/OFF instead",
                        *value));
                }
                return CodeGenResult{std::string("storage.pragma.recursive_triggers(") +
                                         (boolValue ? "true" : "false") + ");",
                                     {},
                                     std::move(warnings)};
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA recursive_triggers = …: expected a number or a name, as in 0/1, TRUE/FALSE or ON/OFF");
            return CodeGenResult{"/* PRAGMA recursive_triggers */"};
        }
        if (name == "journal_mode") {
            if (!node.value) {
                return CodeGenResult{"storage.pragma.journal_mode();", {}, {}};
            }
            if (auto token = pragmaJournalOrLockingValueToken(*node.value)) {
                if (auto cppEnum = journalModeSqlTokenToCppEnum(*token)) {
                    return CodeGenResult{"storage.pragma.journal_mode(" + *cppEnum + ");", {}, {}};
                }
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA journal_mode: unknown mode (expected delete, wal, memory, …)");
            return CodeGenResult{"/* PRAGMA journal_mode */"};
        }
        if (name == "locking_mode") {
            if (!node.value) {
                return CodeGenResult{"storage.pragma.locking_mode();", {}, {}};
            }
            if (auto token = pragmaJournalOrLockingValueToken(*node.value)) {
                if (auto cppEnum = lockingModeSqlTokenToCppEnum(*token)) {
                    return CodeGenResult{"storage.pragma.locking_mode(" + *cppEnum + ");", {}, {}};
                }
            }
            this->context.accumulatedErrors.push_back("PRAGMA locking_mode: expected NORMAL or EXCLUSIVE");
            return CodeGenResult{"/* PRAGMA locking_mode */"};
        }

        this->context.accumulatedErrors.push_back("internal: unsupported PRAGMA reached codegen");
        return CodeGenResult{"/* PRAGMA */"};
    }

}  // namespace sqlite2orm
