#include "codegen_dml.h"
#include "codegen_context.h"
#include "codegen_utils.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <cctype>
#include <string_view>

namespace sqlite2orm {

    DmlCodeGenerator::DmlCodeGenerator(CodeGenerator& coordinator, CodeGeneratorContext& context)
        : coordinator(coordinator), context(context) {}

    CodeGenResult DmlCodeGenerator::generateInsert(const InsertNode& insertNode) {
        std::vector<std::string> warnings;
        if(insertNode.schemaName) {
            warnings.push_back("schema-qualified table in INSERT is not represented in sqlite_orm mapping");
        }
        std::string tableStruct = toStructName(insertNode.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;

        std::string verb = insertNode.replaceInto ? "replace" : "insert";
        std::string orPrefix = insertNode.replaceInto ? std::string() : dmlInsertOrPrefix(insertNode.orConflict);

        std::vector<DecisionPoint> dps;
        std::string middle;
        if(insertNode.dataKind == InsertDataKind::defaultValues) {
            middle = "default_values()";
        } else if(insertNode.dataKind == InsertDataKind::values && insertNode.columnNames.empty()) {
            std::string code;
            for(size_t valueRowIndex = 0; valueRowIndex < insertNode.valueRows.size(); ++valueRowIndex) {
                if(valueRowIndex > 0) {
                    code += "\n";
                }
                code += "storage." + verb + "(" + tableStruct + "{";
                const auto& row = insertNode.valueRows[valueRowIndex];
                for(size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                    if(columnIndex > 0) {
                        code += ", ";
                    }
                    if(dynamic_cast<const NullLiteralNode*>(row[columnIndex].get())) {
                        code += "std::nullopt";
                    } else {
                        auto cell = this->coordinator.generateNode(*row[columnIndex]);
                        dps.insert(dps.end(),
                                   std::make_move_iterator(cell.decisionPoints.begin()),
                                   std::make_move_iterator(cell.decisionPoints.end()));
                        warnings.insert(warnings.end(),
                                        std::make_move_iterator(cell.warnings.begin()),
                                        std::make_move_iterator(cell.warnings.end()));
                        code += cell.code;
                    }
                }
                code += "});";
            }
            this->context.structName = savedStruct;
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if(insertNode.dataKind == InsertDataKind::values) {
            std::string cols = "columns(";
            for(size_t columnIndex = 0; columnIndex < insertNode.columnNames.size(); ++columnIndex) {
                if(columnIndex > 0) {
                    cols += ", ";
                }
                cols += "&" + tableStruct + "::" + toCppIdentifier(insertNode.columnNames[columnIndex]);
            }
            cols += ")";
            std::string vals = "values(";
            for(size_t rowIndex = 0; rowIndex < insertNode.valueRows.size(); ++rowIndex) {
                if(rowIndex > 0) {
                    vals += ", ";
                }
                vals += "std::make_tuple(";
                const auto& row = insertNode.valueRows[rowIndex];
                for(size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                    if(columnIndex > 0) {
                        vals += ", ";
                    }
                    auto cell = this->coordinator.generateNode(*row[columnIndex]);
                    dps.insert(dps.end(),
                               std::make_move_iterator(cell.decisionPoints.begin()),
                               std::make_move_iterator(cell.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(cell.warnings.begin()),
                                    std::make_move_iterator(cell.warnings.end()));
                    vals += cell.code;
                }
                vals += ")";
            }
            vals += ")";
            middle = cols + ", " + vals;
        } else {
            auto sub = this->coordinator.tryCodegenSelectLikeSubquery(*insertNode.selectStatement);
            warnings.insert(warnings.end(),
                            std::make_move_iterator(sub.warnings.begin()),
                            std::make_move_iterator(sub.warnings.end()));
            dps.insert(dps.end(),
                       std::make_move_iterator(sub.decisionPoints.begin()),
                       std::make_move_iterator(sub.decisionPoints.end()));
            if(sub.code.empty()) {
                this->context.structName = savedStruct;
                return CodeGenResult{"/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */",
                                     std::move(dps), std::move(warnings)};
            }
            if(!insertNode.columnNames.empty()) {
                std::string cols = "columns(";
                for(size_t columnIndex = 0; columnIndex < insertNode.columnNames.size(); ++columnIndex) {
                    if(columnIndex > 0) {
                        cols += ", ";
                    }
                    cols += "&" + tableStruct + "::" + toCppIdentifier(insertNode.columnNames[columnIndex]);
                }
                cols += ")";
                middle = cols + ", " + sub.code;
            } else {
                middle = sub.code;
            }
        }

        std::string upsertSuffix;
        if(insertNode.hasUpsertClause) {
            if(insertNode.upsertConflictWhere) {
                warnings.push_back(
                    "ON CONFLICT target WHERE is not represented in sqlite_orm on_conflict(); generated code "
                    "omits that predicate");
            }
            std::string onTarget;
            if(insertNode.upsertConflictColumns.empty()) {
                onTarget = "on_conflict()";
            } else if(insertNode.upsertConflictColumns.size() == 1u) {
                onTarget = "on_conflict(&" + tableStruct + "::" +
                           toCppIdentifier(insertNode.upsertConflictColumns[0]) + ")";
            } else {
                onTarget = "on_conflict(columns(";
                for(size_t upsertIndex = 0; upsertIndex < insertNode.upsertConflictColumns.size(); ++upsertIndex) {
                    if(upsertIndex > 0) {
                        onTarget += ", ";
                    }
                    onTarget +=
                        "&" + tableStruct + "::" + toCppIdentifier(insertNode.upsertConflictColumns[upsertIndex]);
                }
                onTarget += "))";
            }
            if(insertNode.upsertAction == InsertUpsertAction::doNothing) {
                upsertSuffix = ", " + onTarget + ".do_nothing()";
            } else if(insertNode.upsertAction == InsertUpsertAction::doUpdate) {
                std::string setArgs;
                for(size_t assignmentIndex = 0; assignmentIndex < insertNode.upsertUpdateAssignments.size();
                    ++assignmentIndex) {
                    if(assignmentIndex > 0) {
                        setArgs += ", ";
                    }
                    auto valueResult =
                        this->coordinator.generateNode(*insertNode.upsertUpdateAssignments[assignmentIndex].value);
                    dps.insert(dps.end(),
                               std::make_move_iterator(valueResult.decisionPoints.begin()),
                               std::make_move_iterator(valueResult.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(valueResult.warnings.begin()),
                                    std::make_move_iterator(valueResult.warnings.end()));
                    std::string cppCol =
                        toCppIdentifier(insertNode.upsertUpdateAssignments[assignmentIndex].columnName);
                    setArgs += "c(&" + tableStruct + "::" + cppCol + ") = " + valueResult.code;
                }
                upsertSuffix = ", " + onTarget + ".do_update(set(" + setArgs + ")";
                if(insertNode.upsertUpdateWhere) {
                    auto whereResult = this->coordinator.generateNode(*insertNode.upsertUpdateWhere);
                    dps.insert(dps.end(),
                               std::make_move_iterator(whereResult.decisionPoints.begin()),
                               std::make_move_iterator(whereResult.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(whereResult.warnings.begin()),
                                    std::make_move_iterator(whereResult.warnings.end()));
                    upsertSuffix += ", where(" + whereResult.code + ")";
                }
                upsertSuffix += ")";
            }
        }

        this->context.structName = savedStruct;
        std::string code =
            "storage." + verb + "(" + orPrefix + "into<" + tableStruct + ">(), " + middle + upsertSuffix + ");";
        if(insertNode.replaceInto) {
            std::string insertOrReplace =
                "storage.insert(or_replace(), into<" + tableStruct + ">(), " + middle + upsertSuffix + ");";
            dps.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                        "replace_style",
                                        "replace_call",
                                        code,
                                        {Alternative{"insert_or_replace",
                                                     insertOrReplace,
                                                     "same semantics via raw insert(or_replace(), into<T>(), ...)"}}});
        }
        return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
    }

    CodeGenResult DmlCodeGenerator::generateUpdate(const UpdateNode& updateNode) {
        std::vector<std::string> warnings;
        if(updateNode.schemaName) {
            warnings.push_back("schema-qualified table in UPDATE is not represented in sqlite_orm mapping");
        }
        if(updateNode.orConflict != ConflictClause::none) {
            warnings.push_back(
                "UPDATE OR modifier is not represented in sqlite_orm; generated code uses update_all(...) without OR");
        }
        if(!updateNode.fromClause.empty()) {
            warnings.push_back("UPDATE ... FROM ... is not supported in sqlite_orm — "
                               "FROM clause is ignored in codegen");
        }
        std::string tableStruct = toStructName(updateNode.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;
        std::vector<DecisionPoint> dps;
        std::string setArgs;
        for(size_t assignmentIndex = 0; assignmentIndex < updateNode.assignments.size(); ++assignmentIndex) {
            if(assignmentIndex > 0) {
                setArgs += ", ";
            }
            auto valueResult = this->coordinator.generateNode(*updateNode.assignments[assignmentIndex].value);
            dps.insert(dps.end(),
                       std::make_move_iterator(valueResult.decisionPoints.begin()),
                       std::make_move_iterator(valueResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(valueResult.warnings.begin()),
                            std::make_move_iterator(valueResult.warnings.end()));
            std::string cppCol = toCppIdentifier(updateNode.assignments[assignmentIndex].columnName);
            setArgs += "c(&" + tableStruct + "::" + cppCol + ") = " + valueResult.code;
        }
        std::string code = "storage.update_all(set(" + setArgs + ")";
        if(updateNode.whereClause) {
            auto whereResult = this->coordinator.generateNode(*updateNode.whereClause);
            dps.insert(dps.end(),
                       std::make_move_iterator(whereResult.decisionPoints.begin()),
                       std::make_move_iterator(whereResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(whereResult.warnings.begin()),
                            std::make_move_iterator(whereResult.warnings.end()));
            code += ", where(" + whereResult.code + ")";
        }
        code += ");";
        this->context.structName = savedStruct;
        return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
    }

    CodeGenResult DmlCodeGenerator::generateDelete(const DeleteNode& deleteNode) {
        std::vector<std::string> warnings;
        if(deleteNode.schemaName) {
            warnings.push_back("schema-qualified table in DELETE is not represented in sqlite_orm mapping");
        }
        std::string tableStruct = toStructName(deleteNode.tableName);
        std::string savedStruct = this->context.structName;
        this->context.structName = tableStruct;
        std::vector<DecisionPoint> dps;
        std::string code = "storage.remove_all<" + tableStruct + ">()";
        if(deleteNode.whereClause) {
            auto whereResult = this->coordinator.generateNode(*deleteNode.whereClause);
            dps.insert(dps.end(),
                       std::make_move_iterator(whereResult.decisionPoints.begin()),
                       std::make_move_iterator(whereResult.decisionPoints.end()));
            warnings.insert(warnings.end(),
                            std::make_move_iterator(whereResult.warnings.begin()),
                            std::make_move_iterator(whereResult.warnings.end()));
            code = "storage.remove_all<" + tableStruct + ">(where(" + whereResult.code + "))";
        }
        code += ";";
        this->context.structName = savedStruct;
        return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
    }

    CodeGenResult DmlCodeGenerator::generateTriggerStep(const AstNode& statement,
                                                        const std::string& subjectTableStruct) {
        struct TriggerStepScope {
            CodeGeneratorContext* ctx;
            std::string savedStruct;
            std::map<std::string, std::string> savedAliases;
            std::map<std::string, std::string> savedColumnAliases;
            std::map<std::string, std::string> savedColumnAliasCpp20Vars;

            TriggerStepScope(CodeGeneratorContext* ctx, const std::string& subject)
                : ctx(ctx), savedStruct(ctx->structName), savedAliases(ctx->fromTableAliasToStructName),
                  savedColumnAliases(ctx->activeSelectColumnAliases),
                  savedColumnAliasCpp20Vars(ctx->activeSelectColumnAliasCpp20Vars) {
                ctx->structName = subject;
                ctx->fromTableAliasToStructName.clear();
                ctx->activeSelectColumnAliases.clear();
                ctx->activeSelectColumnAliasCpp20Vars.clear();
            }
            ~TriggerStepScope() {
                ctx->structName = std::move(savedStruct);
                ctx->fromTableAliasToStructName = std::move(savedAliases);
                ctx->activeSelectColumnAliases = std::move(savedColumnAliases);
                ctx->activeSelectColumnAliasCpp20Vars = std::move(savedColumnAliasCpp20Vars);
            }
        } scope{&this->context, subjectTableStruct};

        if(auto* selectNode = dynamic_cast<const SelectNode*>(&statement)) {
            return this->coordinator.tryCodegenSqliteSelectSubexpression(*selectNode);
        }
        if(auto* compoundSelectNode = dynamic_cast<const CompoundSelectNode*>(&statement)) {
            return this->coordinator.tryCodegenCompoundSelectSubexpression(*compoundSelectNode);
        }
        auto outer = this->coordinator.generateNode(statement);
        std::string code = outer.code;
        static constexpr std::string_view kStoragePrefix = "storage.";
        if(code.size() >= kStoragePrefix.size() && code.compare(0, kStoragePrefix.size(), kStoragePrefix) == 0) {
            code.erase(0, kStoragePrefix.size());
        }
        while(!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        if(!code.empty() && code.back() == ';') {
            code.pop_back();
        }
        while(!code.empty() && std::isspace(static_cast<unsigned char>(code.back()))) {
            code.pop_back();
        }
        return CodeGenResult{std::move(code), std::move(outer.decisionPoints), std::move(outer.warnings)};
    }

}  // namespace sqlite2orm
