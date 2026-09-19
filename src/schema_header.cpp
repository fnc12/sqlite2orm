#include <sqlite2orm/schema_header.h>

#include <sqlite2orm/ast.h>

#include "codegen_context.h"
#include "codegen_utils.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sqlite2orm {

    namespace {

        std::string normTableName(std::string_view sqlTableName) {
            return stripIdentifierQuotes(sqlTableName);
        }

        void collectFkParents(const CreateTableNode& tableNode, std::unordered_set<std::string>& out,
                              const std::unordered_set<std::string>& known) {
            for(const ColumnDef& column : tableNode.columns) {
                if(column.foreignKey) {
                    const std::string ref = normTableName(column.foreignKey->table);
                    if(known.count(ref)) {
                        out.insert(ref);
                    }
                }
            }
            for(const TableForeignKey& tableForeignKey : tableNode.foreignKeys) {
                const std::string ref = normTableName(tableForeignKey.references.table);
                if(known.count(ref)) {
                    out.insert(ref);
                }
            }
        }

        [[nodiscard]] std::vector<const CreateTableNode*> topoSortTables(
            const std::vector<const CreateTableNode*>& tables) {
            std::unordered_map<std::string, const CreateTableNode*> tableByNormalizedName;
            std::unordered_set<std::string> knownNormalizedNames;
            for(const CreateTableNode* tableNode : tables) {
                const std::string normalizedName = normTableName(tableNode->tableName);
                tableByNormalizedName[normalizedName] = tableNode;
                knownNormalizedNames.insert(normalizedName);
            }

            std::unordered_map<std::string, std::vector<std::string>> adjacency;
            std::unordered_map<std::string, size_t> inDegree;
            for(const CreateTableNode* tableNode : tables) {
                inDegree[normTableName(tableNode->tableName)] = 0;
            }
            for(const CreateTableNode* tableNode : tables) {
                const std::string selfNormalized = normTableName(tableNode->tableName);
                std::unordered_set<std::string> parentTables;
                collectFkParents(*tableNode, parentTables, knownNormalizedNames);
                for(const std::string& parentTable : parentTables) {
                    adjacency[parentTable].push_back(selfNormalized);
                    inDegree[selfNormalized]++;
                }
            }

            std::vector<std::string> sortedNames;
            sortedNames.reserve(tables.size());
            for(const CreateTableNode* tableNode : tables) {
                sortedNames.push_back(normTableName(tableNode->tableName));
            }
            std::sort(sortedNames.begin(), sortedNames.end(),
                      [](const std::string& left, const std::string& right) {
                          return std::lexicographical_compare(
                              left.begin(), left.end(), right.begin(), right.end(),
                              [](char leftChar, char rightChar) {
                                  return std::tolower(static_cast<unsigned char>(leftChar)) <
                                         std::tolower(static_cast<unsigned char>(rightChar));
                              });
                      });

            std::queue<std::string> pending;
            for(const std::string& sortedName : sortedNames) {
                if(inDegree[sortedName] == 0) {
                    pending.push(sortedName);
                }
            }

            std::vector<const CreateTableNode*> ordered;
            std::unordered_set<std::string> visited;
            while(!pending.empty()) {
                const std::string currentTable = pending.front();
                pending.pop();
                if(visited.count(currentTable)) {
                    continue;
                }
                visited.insert(currentTable);
                const auto iterator = tableByNormalizedName.find(currentTable);
                if(iterator != tableByNormalizedName.end()) {
                    ordered.push_back(iterator->second);
                }
                for(const std::string& dependentTable : adjacency[currentTable]) {
                    if(--inDegree[dependentTable] == 0) {
                        pending.push(dependentTable);
                    }
                }
            }

            for(const std::string& sortedName : sortedNames) {
                if(!visited.count(sortedName)) {
                    const auto iterator = tableByNormalizedName.find(sortedName);
                    if(iterator != tableByNormalizedName.end()) {
                        ordered.push_back(iterator->second);
                    }
                }
            }
            return ordered;
        }

        /** The DDL keyword behind a `sqlite_master` type, for a warning that reads like the SQL. */
        std::string_view createStatementLabel(std::string_view masterType) {
            if(masterType == "table") {
                return "CREATE TABLE";
            }
            if(masterType == "view") {
                return "CREATE VIEW";
            }
            if(masterType == "index") {
                return "CREATE INDEX";
            }
            if(masterType == "trigger") {
                return "CREATE TRIGGER";
            }
            return masterType;
        }

        void trimTrailingSemicolon(std::string& line) {
            while(!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
                line.pop_back();
            }
            if(!line.empty() && line.back() == ';') {
                line.pop_back();
            }
            while(!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
                line.pop_back();
            }
        }

        /**
         *  One whole pass over the schema, generating every statement with the names sqlite_orm
         *  has no type for already known, and recording in them whatever this pass leaves out.
         */
        CodeGenResult generateHeaderPass(const ProcessSqliteSchemaResult& schema, const CodeGenPolicy* policy,
                                         std::set<std::string>& ungeneratableTables,
                                         std::set<std::string>& ungeneratableViews) {
            CodeGenerator gen;
            gen.codeGenPolicy = policy;
            gen.context().ungeneratableTables = ungeneratableTables;
            gen.context().ungeneratableViews = ungeneratableViews;

            std::vector<const CreateTableNode*> tableNodes;
            for(const SchemaStatementResult& statementResult : schema.statements) {
                if(!statementResult.pipeline.ok()) {
                    continue;
                }
                if(const auto* createTable = dynamic_cast<const CreateTableNode*>(
                       statementResult.pipeline.parseResult.astNodePointer.get())) {
                    tableNodes.push_back(createTable);
                }
            }

            const std::vector<const CreateTableNode*> sortedTables = topoSortTables(tableNodes);

            std::ostringstream oss;
            oss << "#pragma once\n\n"
                   "#include <sqlite_orm/sqlite_orm.h>\n"
                   "#include <cstdint>\n"
                   "#include <optional>\n"
                   "#include <string>\n"
                   "#include <vector>\n\n";

            std::vector<std::string> storageArgs;
            std::vector<DecisionPoint> allDecisionPoints;
            std::vector<CodegenWarning> allWarnings;
            std::vector<std::string> allComments;

            // A statement the pipeline could not carry through — a parse error, a rule sqlite_orm has
            // no spelling for, a codegen error — is left out of the storage instead of taking the whole
            // schema with it. SQLite only compiles a view or trigger body when it is used, so it stores
            // bodies sqlite2orm refuses (`CREATE VIEW v AS SELECT -0x8000000000000000` is accepted and
            // fails only on `SELECT * FROM v`), and one of them says nothing about the rest of the
            // schema. The name such a statement created has no C++ type behind it either, exactly as an
            // ungeneratable table has none, so it is marked here — before anything is generated — and
            // the existing funnel drops whatever rests on it.
            for(const SchemaStatementResult& statementResult : schema.statements) {
                if(statementResult.pipeline.ok()) {
                    // A virtual table is never merged into make_storage() either — sqlite_orm spells
                    // it `make_virtual_table`, which no storage of a plain schema holds — so its name
                    // has no C++ type behind it whatever it generates, and it is marked right here,
                    // before the first table is generated.
                    if(dynamic_cast<const CreateVirtualTableNode*>(
                           statementResult.pipeline.parseResult.astNodePointer.get())) {
                        gen.context().markUngeneratableTable(statementResult.meta.name);
                    }
                    continue;
                }
                if(statementResult.meta.type == "view") {
                    gen.context().markUngeneratableView(statementResult.meta.name);
                } else if(statementResult.meta.type == "table") {
                    gen.context().markUngeneratableTable(statementResult.meta.name);
                }
                allWarnings.push_back(std::string(createStatementLabel(statementResult.meta.type)) + " `" +
                                      statementResult.meta.name +
                                      "` did not generate and is not merged into make_storage()");
            }

            std::vector<CreateTableParts> tableParts;
            tableParts.reserve(sortedTables.size());
            for(const CreateTableNode* createTableNode : sortedTables) {
                tableParts.push_back(gen.createTableParts(*createTableNode));
            }

            for(size_t tableIndex = 0; tableIndex < sortedTables.size(); ++tableIndex) {
                const CreateTableParts& parts = tableParts[tableIndex];
                allWarnings.insert(allWarnings.end(), parts.warnings.begin(), parts.warnings.end());
                appendUniqueStrings(allComments, parts.comments);
                if(parts.makeTableExpression.empty()) {
                    allWarnings.push_back("CREATE TABLE `" + stripIdentifierQuotes(sortedTables[tableIndex]->tableName) +
                                          "` is not merged into make_storage()");
                    continue;
                }
                oss << parts.structDeclaration << "\n";
                storageArgs.push_back(parts.makeTableExpression);
            }

            // A view that is left out is a name sqlite_orm has no type for, exactly as an
            // ungeneratable table is, so it joins them: a trigger `INSTEAD OF ... ON` it and a view
            // selecting from it go with it. SQLite creates nothing before the view it names, so a
            // dropped view is always marked before anything resting on it is generated.
            for(const SchemaStatementResult& statementResult : schema.statements) {
                if(!statementResult.pipeline.ok()) {
                    continue;
                }
                const AstNode* root = statementResult.pipeline.parseResult.astNodePointer.get();
                if(dynamic_cast<const CreateTableNode*>(root)) {
                    continue;
                }
                if(dynamic_cast<const CreateVirtualTableNode*>(root)) {
                    allWarnings.push_back("CREATE VIRTUAL TABLE `" + statementResult.meta.name +
                                          "` is not merged into make_storage(); run sqlite2orm on its SQL separately");
                    continue;
                }

                // Which tables a statement names is only known once it is generated: a subquery
                // naming one can sit in any expression, a trigger WHEN clause or a CTE. Generation
                // records them (`CodeGeneratorContext::structNameForTable`), and a statement that
                // named an ungenerated table is rolled back whole — the fragment goes, and so does
                // every decision point id, bind parameter index and alias its generation took. The
                // context is only copied when there is something to roll back for, which is read
                // afresh at every statement because a dropped view adds to the set as we go; a name
                // can only be recorded when the set was already non-empty, so the copy is always
                // there when the rollback needs it.
                std::optional<CodeGeneratorContext> contextBeforeStatement;
                if(!gen.context().ungeneratableTables.empty()) {
                    contextBeforeStatement = gen.context();
                }
                gen.context().referencedUngeneratableTables.clear();
                const auto restsOnUngeneratableTable = [&]() {
                    if(gen.context().referencedUngeneratableTables.empty()) {
                        return false;
                    }
                    const std::string kind(gen.context().referencedUngeneratableKind());
                    gen.context() = *contextBeforeStatement;
                    allWarnings.push_back("`" + statementResult.meta.name + "` rests on a " + kind +
                                          " that is not generated and is not merged into "
                                          "make_storage()");
                    return true;
                };

                if(auto* createView = dynamic_cast<const CreateViewNode*>(root)) {
                    CreateViewParts viewParts = gen.createViewParts(*createView);
                    if(restsOnUngeneratableTable()) {
                        // The rollback undid every mark the statement made, so a view that also
                        // failed to generate is marked again here, for the one reason that survives.
                        gen.context().markUngeneratableView(createView->viewName);
                        continue;
                    }
                    allWarnings.insert(allWarnings.end(), viewParts.warnings.begin(), viewParts.warnings.end());
                    appendUniqueStrings(allComments, viewParts.comments);
                    allDecisionPoints.insert(allDecisionPoints.end(), viewParts.decisionPoints.begin(),
                                             viewParts.decisionPoints.end());
                    if(viewParts.makeViewExpression.empty()) {
                        // `createViewParts` has already marked the view as ungeneratable.
                        allWarnings.push_back("CREATE VIEW `" + statementResult.meta.name +
                                              "` is not merged into make_storage()");
                        continue;
                    }
                    oss << viewParts.structDeclaration << "\n";
                    storageArgs.push_back(viewParts.makeViewExpression);
                    continue;
                }

                if(dynamic_cast<const CreateIndexNode*>(root) || dynamic_cast<const CreateTriggerNode*>(root)) {
                    CodeGenResult fragment = gen.generate(*root);
                    if(restsOnUngeneratableTable()) {
                        continue;
                    }
                    allWarnings.insert(allWarnings.end(), fragment.warnings.begin(), fragment.warnings.end());
                    appendUniqueStrings(allComments, fragment.comments);
                    allDecisionPoints.insert(allDecisionPoints.end(), fragment.decisionPoints.begin(),
                                             fragment.decisionPoints.end());
                    std::string storageArgLine = fragment.code;
                    trimTrailingSemicolon(storageArgLine);
                    if(storageArgLine.empty()) {
                        allWarnings.push_back(
                            std::string(dynamic_cast<const CreateIndexNode*>(root) ? "CREATE INDEX" : "CREATE TRIGGER") +
                            " `" + statementResult.meta.name + "` is not merged into make_storage()");
                        continue;
                    }
                    storageArgs.push_back(storageArgLine);
                }
            }

            oss << "\ninline auto make_sqlite_schema_storage(const std::string& db_path) {\n";
            oss << "    using namespace sqlite_orm;\n";
            oss << "    return make_storage(db_path";
            for(const std::string& storageArgument : storageArgs) {
                oss << ",\n        " << storageArgument;
            }
            oss << ");\n}\n";

            std::vector<std::string> dmlStatements;
            for(const SchemaStatementResult& statementResult : schema.statements) {
                if(!statementResult.pipeline.ok()) {
                    continue;
                }
                const AstNode* root = statementResult.pipeline.parseResult.astNodePointer.get();
                if(dynamic_cast<const CreateTableNode*>(root) || dynamic_cast<const CreateIndexNode*>(root) ||
                   dynamic_cast<const CreateTriggerNode*>(root) || dynamic_cast<const CreateVirtualTableNode*>(root) ||
                   dynamic_cast<const CreateViewNode*>(root)) {
                    continue;
                }
                std::optional<CodeGeneratorContext> contextBeforeStatement;
                if(!gen.context().ungeneratableTables.empty()) {
                    contextBeforeStatement = gen.context();
                }
                gen.context().referencedUngeneratableTables.clear();
                CodeGenResult fragment = gen.generate(*root);
                if(!gen.context().referencedUngeneratableTables.empty()) {
                    const std::string kind(gen.context().referencedUngeneratableKind());
                    gen.context() = *contextBeforeStatement;
                    allWarnings.push_back("`" + statementResult.meta.name + "` rests on a " + kind +
                                          " that is not generated and is left out");
                    continue;
                }
                allWarnings.insert(allWarnings.end(), fragment.warnings.begin(), fragment.warnings.end());
                appendUniqueStrings(allComments, fragment.comments);
                allDecisionPoints.insert(allDecisionPoints.end(), fragment.decisionPoints.begin(),
                                         fragment.decisionPoints.end());
                if(!fragment.code.empty()) {
                    dmlStatements.push_back(fragment.code);
                }
            }
            if(!dmlStatements.empty()) {
                oss << "\n";
                for(const std::string& dml : dmlStatements) {
                    oss << dml << "\n";
                }
            }

            ungeneratableTables = gen.context().ungeneratableTables;
            ungeneratableViews = gen.context().ungeneratableViews;
            return CodeGenResult{oss.str(), std::move(allDecisionPoints), std::move(allWarnings), {},
                                 std::move(allComments)};
        }

    }  // namespace

    CodeGenResult generateSqliteSchemaHeader(const ProcessSqliteSchemaResult& schema, const CodeGenPolicy* policy) {
        // A table this schema cannot map, and a view left out of the storage, are alike names
        // sqlite_orm has no type for, so a foreign key into one, and every view, index and trigger
        // resting on one, has to go too — otherwise the header names a struct it never declares
        // and the tool still exits 0. Which names those are is only known once the schema has been
        // generated, and a table is generated before the views are, so the whole pass is repeated
        // whenever that set has grown. It only ever grows and is bounded by the number of
        // statements, so this settles; a schema with nothing left out is generated exactly once.
        std::set<std::string> ungeneratableTables;
        std::set<std::string> ungeneratableViews;
        for(;;) {
            const size_t knownBefore = ungeneratableTables.size();
            CodeGenResult result = generateHeaderPass(schema, policy, ungeneratableTables, ungeneratableViews);
            if(ungeneratableTables.size() == knownBefore) {
                return result;
            }
        }
    }

}  // namespace sqlite2orm
