#include <sqlite2orm/schema_header.h>

#include <sqlite2orm/ast.h>

#include <algorithm>
#include <cctype>
#include <queue>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sqlite2orm {

    namespace {

        std::string stripIdentifierQuotes(std::string_view identifier) {
            if(identifier.size() >= 2) {
                const char first = identifier.front();
                const char last = identifier.back();
                if((first == '"' && last == '"') || (first == '`' && last == '`') ||
                   (first == '[' && last == ']')) {
                    return std::string(identifier.substr(1, identifier.size() - 2));
                }
            }
            return std::string(identifier);
        }

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

        void appendUniqueStrings(std::vector<std::string>& dst, const std::vector<std::string>& src) {
            for(const std::string& s : src) {
                if(std::find(dst.begin(), dst.end(), s) == dst.end()) {
                    dst.push_back(s);
                }
            }
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

    }  // namespace

    CodeGenResult generateSqliteSchemaHeader(const ProcessSqliteSchemaResult& schema, const CodeGenPolicy* policy) {
        CodeGenerator gen;
        gen.codeGenPolicy = policy;

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
        std::vector<std::string> allWarnings;
        std::vector<std::string> allComments;

        for(const CreateTableNode* createTableNode : sortedTables) {
            const CreateTableParts parts = gen.createTableParts(*createTableNode);
            oss << parts.structDeclaration << "\n";
            allWarnings.insert(allWarnings.end(), parts.warnings.begin(), parts.warnings.end());
            storageArgs.push_back(parts.makeTableExpression);
        }

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

            if(dynamic_cast<const CreateIndexNode*>(root) || dynamic_cast<const CreateTriggerNode*>(root)) {
                CodeGenResult fragment = gen.generate(*root);
                allWarnings.insert(allWarnings.end(), fragment.warnings.begin(), fragment.warnings.end());
                appendUniqueStrings(allComments, fragment.comments);
                allDecisionPoints.insert(allDecisionPoints.end(), fragment.decisionPoints.begin(),
                                         fragment.decisionPoints.end());
                std::string storageArgLine = fragment.code;
                trimTrailingSemicolon(storageArgLine);
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
            CodeGenResult fragment = gen.generate(*root);
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

        return CodeGenResult{oss.str(), std::move(allDecisionPoints), std::move(allWarnings), {},
                             std::move(allComments)};
    }

}  // namespace sqlite2orm
