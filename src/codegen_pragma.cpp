#include "codegen_pragma.h"
#include "codegen_context.h"
#include "codegen_utils.h"

#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    PragmaCodeGenerator::PragmaCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context) :
        coordinator(coordinator),
        context(context) {}

    CodeGenResult PragmaCodeGenerator::codegenPragmaStatement(const PragmaNode& node) {
        const std::string name = toLowerAscii(node.pragmaName);
        std::vector<DecisionPoint> decisionPoints;
        std::vector<std::string> warnings;

        auto mergeSub = [&](CodeGenResult sub) {
            decisionPoints.insert(decisionPoints.end(), std::make_move_iterator(sub.decisionPoints.begin()),
                                  std::make_move_iterator(sub.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(sub.warnings.begin()),
                            std::make_move_iterator(sub.warnings.end()));
            return std::move(sub.code);
        };

        if(name == "module_list") {
            return CodeGenResult{"storage.pragma.module_list();", {}, {}};
        }
        if(name == "quick_check") {
            return CodeGenResult{"storage.pragma.quick_check();", {}, {}};
        }
        if(name == "table_info") {
            if(!node.value) {
                this->context.accumulatedErrors.push_back("PRAGMA table_info requires a table name");
                return CodeGenResult{"/* PRAGMA table_info */"};
            }
            if(auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.table_info(" + *lit + ");", {}, {}};
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA table_info: use a string literal or identifier for the table name");
            return CodeGenResult{"/* PRAGMA table_info */"};
        }
        if(name == "table_xinfo") {
            if(!node.value) {
                this->context.accumulatedErrors.push_back("PRAGMA table_xinfo requires a table name");
                return CodeGenResult{"/* PRAGMA table_xinfo */"};
            }
            if(auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.table_xinfo(" + *lit + ");", {}, {}};
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA table_xinfo: use a string literal or identifier for the table name");
            return CodeGenResult{"/* PRAGMA table_xinfo */"};
        }
        if(name == "integrity_check") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.integrity_check();", {}, {}};
            }
            if(const auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(node.value.get())) {
                return CodeGenResult{
                    "storage.pragma.integrity_check(" + std::string(integerLiteral->value) + ");", {}, {}};
            }
            if(auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.integrity_check(" + *lit + ");", {}, {}};
            }
            warnings.push_back(
                "PRAGMA integrity_check argument is emitted via subexpression codegen; ensure it matches "
                "sqlite_orm::pragma_t::integrity_check overloads");
            std::string arg = mergeSub(this->coordinator.generateNode(*node.value));
            return CodeGenResult{
                "storage.pragma.integrity_check(" + std::move(arg) + ");", std::move(decisionPoints),
                std::move(warnings)};
        }
        if(name == "busy_timeout" || name == "application_id" || name == "synchronous" || name == "user_version" ||
           name == "auto_vacuum" || name == "max_page_count") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma." + name + "();", {}, {}};
            }
            std::string arg = mergeSub(this->coordinator.generateNode(*node.value));
            return CodeGenResult{
                "storage.pragma." + name + "(" + std::move(arg) + ");", std::move(decisionPoints),
                std::move(warnings)};
        }
        if(name == "recursive_triggers") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.recursive_triggers();", {}, {}};
            }
            if(auto boolValue = pragmaRecursiveTriggersBool(*node.value)) {
                return CodeGenResult{
                    std::string("storage.pragma.recursive_triggers(") + (*boolValue ? "true" : "false") + ");", {}, {}};
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA recursive_triggers = …: use 0/1, TRUE/FALSE, ON/OFF, or a string literal");
            return CodeGenResult{"/* PRAGMA recursive_triggers */"};
        }
        if(name == "journal_mode") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.journal_mode();", {}, {}};
            }
            if(auto token = pragmaJournalOrLockingValueToken(*node.value)) {
                if(auto cppEnum = journalModeSqlTokenToCppEnum(*token)) {
                    return CodeGenResult{"storage.pragma.journal_mode(" + *cppEnum + ");", {}, {}};
                }
            }
            this->context.accumulatedErrors.push_back(
                "PRAGMA journal_mode: unknown mode (expected delete, wal, memory, …)");
            return CodeGenResult{"/* PRAGMA journal_mode */"};
        }
        if(name == "locking_mode") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.locking_mode();", {}, {}};
            }
            if(auto token = pragmaJournalOrLockingValueToken(*node.value)) {
                if(auto cppEnum = lockingModeSqlTokenToCppEnum(*token)) {
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
