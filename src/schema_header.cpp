#include <sqlite2orm/schema_header.h>

#include <sqlite2orm/ast.h>
#include <sqlite2orm/token_stream.h>
#include <sqlite2orm/tokenizer.h>

#include "codegen_context.h"
#include "codegen_utils.h"

#include <algorithm>
#include <cctype>
#include <iterator>
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

        void collectFkParents(const CreateTableNode& tableNode,
                              std::unordered_set<std::string>& out,
                              const std::unordered_set<std::string>& known) {
            for (const ColumnDef& column: tableNode.columns) {
                if (column.foreignKey) {
                    const std::string ref = normTableName(column.foreignKey->table);
                    if (known.count(ref)) {
                        out.insert(ref);
                    }
                }
            }
            for (const TableForeignKey& tableForeignKey: tableNode.foreignKeys) {
                const std::string ref = normTableName(tableForeignKey.references.table);
                if (known.count(ref)) {
                    out.insert(ref);
                }
            }
        }

        [[nodiscard]] std::vector<const CreateTableNode*>
        topoSortTables(const std::vector<const CreateTableNode*>& tables) {
            std::unordered_map<std::string, const CreateTableNode*> tableByNormalizedName;
            std::unordered_set<std::string> knownNormalizedNames;
            for (const CreateTableNode* tableNode: tables) {
                const std::string normalizedName = normTableName(tableNode->tableName);
                tableByNormalizedName[normalizedName] = tableNode;
                knownNormalizedNames.insert(normalizedName);
            }

            std::unordered_map<std::string, std::vector<std::string>> adjacency;
            std::unordered_map<std::string, size_t> inDegree;
            for (const CreateTableNode* tableNode: tables) {
                inDegree[normTableName(tableNode->tableName)] = 0;
            }
            for (const CreateTableNode* tableNode: tables) {
                const std::string selfNormalized = normTableName(tableNode->tableName);
                std::unordered_set<std::string> parentTables;
                collectFkParents(*tableNode, parentTables, knownNormalizedNames);
                for (const std::string& parentTable: parentTables) {
                    adjacency[parentTable].push_back(selfNormalized);
                    inDegree[selfNormalized]++;
                }
            }

            std::vector<std::string> sortedNames;
            sortedNames.reserve(tables.size());
            for (const CreateTableNode* tableNode: tables) {
                sortedNames.push_back(normTableName(tableNode->tableName));
            }
            std::sort(sortedNames.begin(), sortedNames.end(), [](const std::string& left, const std::string& right) {
                return std::lexicographical_compare(left.begin(),
                                                    left.end(),
                                                    right.begin(),
                                                    right.end(),
                                                    [](char leftChar, char rightChar) {
                                                        return std::tolower(static_cast<unsigned char>(leftChar)) <
                                                               std::tolower(static_cast<unsigned char>(rightChar));
                                                    });
            });

            std::queue<std::string> pending;
            for (const std::string& sortedName: sortedNames) {
                if (inDegree[sortedName] == 0) {
                    pending.push(sortedName);
                }
            }

            std::vector<const CreateTableNode*> ordered;
            std::unordered_set<std::string> visited;
            while (!pending.empty()) {
                const std::string currentTable = pending.front();
                pending.pop();
                if (visited.count(currentTable)) {
                    continue;
                }
                visited.insert(currentTable);
                const auto iterator = tableByNormalizedName.find(currentTable);
                if (iterator != tableByNormalizedName.end()) {
                    ordered.push_back(iterator->second);
                }
                for (const std::string& dependentTable: adjacency[currentTable]) {
                    if (--inDegree[dependentTable] == 0) {
                        pending.push(dependentTable);
                    }
                }
            }

            for (const std::string& sortedName: sortedNames) {
                if (!visited.count(sortedName)) {
                    const auto iterator = tableByNormalizedName.find(sortedName);
                    if (iterator != tableByNormalizedName.end()) {
                        ordered.push_back(iterator->second);
                    }
                }
            }
            return ordered;
        }

        /**
         *  The suffixes FTS5 gives the tables it keeps its index in. SQLite decides that a table
         *  belongs to a module by its name alone: `sqlite3ShadowTableName()` cuts the name at its
         *  LAST underscore, looks the part in front up as a virtual table and asks that module's
         *  `xShadowName` about the part behind, which for FTS5 is this list, compared without
         *  regard to case.
         */
        constexpr std::string_view fts5ShadowTableSuffixes[] = {"config", "content", "data", "docsize", "idx"};

        /**
         *  Whether SQLite keeps this name for itself. Every name spelled `sqlite_...` belongs to
         *  the engine — `sqlite_sequence` behind AUTOINCREMENT and `sqlite_stat1` behind ANALYZE
         *  are the two an ordinary database hands out — and no schema of a user's own can hold
         *  one: sqlite3 3.51 answers `CREATE TABLE "sqlite_foo"(x)` with "object name reserved for
         *  internal use" whatever the case and the quoting, and refuses an index, a view and a
         *  trigger under such a name just as flatly. A storage holding one could therefore never
         *  create it — sync_schema() would stop right there — and writing to `sqlite_sequence`
         *  through it would take the bookkeeping of every AUTOINCREMENT key with it.
         *
         *  The name is the one `sqlite_master` holds, and SQLite reads that name as it stands, so
         *  nothing is unquoted here: `CREATE TABLE "'sqlite_foo'"(x)` is a table sqlite3 3.51
         *  creates without a word, under the name `'sqlite_foo'`, and a storage that left it out
         *  would lose a table of the user's own.
         */
        [[nodiscard]] bool isReservedSqliteName(std::string_view name) {
            return normalizeSchemaObjectName(name).rfind("sqlite_", 0) == 0;
        }

        /**
         *  The module behind a `sqlite_master` row when that row is a `CREATE VIRTUAL TABLE`, and
         *  nothing for any other statement. It is read off the tokens rather than off the AST
         *  because an AST is only there for a statement the whole pipeline carried through, and
         *  ordinary FTS5 DDL does not get that far: `CREATE VIRTUAL TABLE mail USING fts5(subject,
         *  sender UNINDEXED)` names a column option FTS5 documents, SQLite hands module arguments
         *  to the module verbatim instead of parsing them, and this parser reads them as
         *  expressions, so the statement fails to parse — as another one fails to validate or to
         *  generate. Losing the module name there would lose the shadow tables behind it with it,
         *  which is the whole of the symptom. Only the tokenizer ever cuts the text.
         */
        [[nodiscard]] std::optional<std::string> virtualTableModuleName(std::string_view sql) {
            std::vector<Token> tokens;
            try {
                tokens = Tokenizer{}.tokenize(sql);
            } catch (const TokenizeError&) {
                return std::nullopt;
            }
            TokenStream stream;
            stream.reset(std::move(tokens));
            if (!stream.match(TokenType::kwCreate)) {
                return std::nullopt;
            }
            if (stream.check(TokenType::kwTemp) || stream.check(TokenType::kwTemporary)) {
                stream.advanceToken();
            }
            if (!stream.match(TokenType::kwVirtual) || !stream.match(TokenType::kwTable)) {
                return std::nullopt;
            }
            if (stream.check(TokenType::kwIf)) {
                stream.advanceToken();
                if (!stream.match(TokenType::kwNot) || !stream.match(TokenType::kwExists)) {
                    return std::nullopt;
                }
            }
            if (!stream.isColumnNameToken()) {
                return std::nullopt;
            }
            stream.advanceToken();
            if (stream.match(TokenType::dot)) {
                if (!stream.isColumnNameToken()) {
                    return std::nullopt;
                }
                stream.advanceToken();
            }
            if (!stream.match(TokenType::kwUsing) || !stream.isColumnNameToken()) {
                return std::nullopt;
            }
            return normalizeSqlIdentifier(stream.current().value);
        }

        /**
         *  The tables FTS5 keeps its own index in, keyed by their normalized name and mapped to the
         *  virtual table they belong to. They are ordinary `CREATE TABLE` rows of `sqlite_master`
         *  and nothing in their SQL says whose they are, so the whole schema is needed to tell them
         *  apart from a table of the same shape a user wrote. A schema holding no FTS5 table at all
         *  is answered after one pass over the rows.
         *
         *  Both keys are names out of `sqlite_master` and are normalized as such, without unquoting:
         *  the storage of `CREATE VIRTUAL TABLE "[x]" USING fts5(a)` is filed under `[x]_data` and
         *  the four names beside it, which only cut back to the virtual table `[x]` while the
         *  brackets stay part of both names.
         */
        [[nodiscard]] std::unordered_map<std::string, std::string>
        fts5ShadowTables(const ProcessSqliteSchemaResult& schema) {
            std::unordered_map<std::string, std::string> fts5TableNames;
            std::vector<const SchemaStatementMeta*> plainTables;
            for (const SchemaStatementResult& statementResult: schema.statements) {
                if (statementResult.meta.type != "table") {
                    continue;
                }
                const std::optional<std::string> moduleName = virtualTableModuleName(statementResult.meta.sql);
                if (!moduleName) {
                    plainTables.push_back(&statementResult.meta);
                } else if (*moduleName == "fts5") {
                    fts5TableNames.emplace(normalizeSchemaObjectName(statementResult.meta.name),
                                           statementResult.meta.name);
                }
            }

            std::unordered_map<std::string, std::string> shadowTables;
            if (fts5TableNames.empty()) {
                return shadowTables;
            }
            for (const SchemaStatementMeta* meta: plainTables) {
                const std::string normalizedName = normalizeSchemaObjectName(meta->name);
                const size_t lastUnderscore = normalizedName.rfind('_');
                if (lastUnderscore == std::string::npos) {
                    continue;
                }
                const std::string_view suffix = std::string_view{normalizedName}.substr(lastUnderscore + 1);
                if (std::find(std::begin(fts5ShadowTableSuffixes), std::end(fts5ShadowTableSuffixes), suffix) ==
                    std::end(fts5ShadowTableSuffixes)) {
                    continue;
                }
                const auto virtualTable = fts5TableNames.find(normalizedName.substr(0, lastUnderscore));
                if (virtualTable != fts5TableNames.end()) {
                    shadowTables.emplace(normalizedName, virtualTable->second);
                }
            }
            return shadowTables;
        }

        /**
         *  The DDL keyword behind a `sqlite_master` row, for a warning that reads like the SQL that
         *  is in the database. `sqlite_master` files a virtual table under the type `table` like
         *  any other, so the SQL is what tells the two apart.
         */
        std::string_view createStatementLabel(const SchemaStatementMeta& meta) {
            const std::string_view masterType = meta.type;
            if (masterType == "table") {
                return virtualTableModuleName(meta.sql) ? "CREATE VIRTUAL TABLE" : "CREATE TABLE";
            }
            if (masterType == "view") {
                return "CREATE VIEW";
            }
            if (masterType == "index") {
                return "CREATE INDEX";
            }
            if (masterType == "trigger") {
                return "CREATE TRIGGER";
            }
            return masterType;
        }

        void trimTrailingSemicolon(std::string& line) {
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
                line.pop_back();
            }
            if (!line.empty() && line.back() == ';') {
                line.pop_back();
            }
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) {
                line.pop_back();
            }
        }

        /**
         *  One whole pass over the schema, generating every statement with the names sqlite_orm
         *  has no type for already known, and recording in them whatever this pass leaves out.
         */
        CodeGenResult generateHeaderPass(const ProcessSqliteSchemaResult& schema,
                                         const CodeGenPolicy* policy,
                                         const std::unordered_map<std::string, std::string>& shadowTables,
                                         std::set<std::string>& ungeneratableTables,
                                         std::set<std::string>& ungeneratableViews) {
            CodeGenerator gen;
            gen.codeGenPolicy = policy;
            gen.context().ungeneratableTables = ungeneratableTables;
            gen.context().ungeneratableViews = ungeneratableViews;

            // Every name the database holds a table or a view under. SQLite only resolves a
            // foreign key when enforcement is on, so a stored schema may name a parent that was
            // never created, and such a name has no struct behind it however the rest of the
            // schema generates. `sqlite_master` is the whole truth about what the database has,
            // so what is missing from it is missing from the schema.
            std::set<std::string> schemaObjectNames;
            for (const SchemaStatementResult& statementResult: schema.statements) {
                if (statementResult.meta.type == "table" || statementResult.meta.type == "view") {
                    schemaObjectNames.insert(normalizeSchemaObjectName(statementResult.meta.name));
                }
            }
            gen.context().schemaObjectNames = std::move(schemaObjectNames);

            // Whether SQLite, not sqlite_orm, owns what a row created: its own `sqlite_...` object
            // or a table a module keeps its index in. Either way the row is left out of the storage
            // before anything is generated, and the name gets no C++ type.
            const auto sqliteOwnsStatement = [&shadowTables](const SchemaStatementMeta& meta) {
                return isReservedSqliteName(meta.name) || shadowTables.count(normalizeSchemaObjectName(meta.name)) != 0;
            };

            std::vector<const CreateTableNode*> tableNodes;
            for (const SchemaStatementResult& statementResult: schema.statements) {
                if (!statementResult.pipeline.ok() || sqliteOwnsStatement(statementResult.meta)) {
                    continue;
                }
                if (const auto* createTable = dynamic_cast<const CreateTableNode*>(
                        statementResult.pipeline.parseResult.astNodePointer.get())) {
                    tableNodes.push_back(createTable);
                }
            }

            const std::vector<const CreateTableNode*> sortedTables = topoSortTables(tableNodes);

            // The includes stand in front of everything the header holds, but whether `<limits>`
            // is among them is only known once it is all generated: a literal past the range of a
            // double is spelled `std::numeric_limits<double>::infinity()`. So the body is built
            // first and the prologue is put in front of it at the end.
            std::ostringstream oss;

            // The struct declarations are collected apart from the rest of the header because what
            // has to stand in front of them is only known once they are all generated: a reflected
            // struct carries sqlite_orm annotations, and the names inside an annotation are looked
            // up where the struct is written — at namespace scope, which the using-directive inside
            // make_sqlite_schema_storage() below does not reach.
            std::ostringstream declarations;
            bool declarationsCarryAnnotations = false;

            std::vector<std::string> storageArgs;
            std::vector<std::string> dependentStorageArgs;
            std::vector<DecisionPoint> allDecisionPoints;
            std::vector<CodegenWarning> allWarnings;
            std::vector<CodegenComment> allComments;

            const auto markNameOfStatement = [&gen](const SchemaStatementMeta& meta) {
                if (meta.type == "view") {
                    gen.context().markUngeneratableSchemaView(meta.name);
                } else if (meta.type == "table") {
                    gen.context().markUngeneratableSchemaTable(meta.name);
                }
            };

            // A statement the pipeline could not carry through — a parse error, a rule sqlite_orm has
            // no spelling for, a codegen error — is left out of the storage instead of taking the whole
            // schema with it. SQLite only compiles a view or trigger body when it is used, so it stores
            // bodies sqlite2orm refuses (`CREATE VIEW v AS SELECT -0x8000000000000000` is accepted and
            // fails only on `SELECT * FROM v`), and one of them says nothing about the rest of the
            // schema. The name such a statement created has no C++ type behind it either, exactly as an
            // ungeneratable table has none, so it is marked here — before anything is generated — and
            // the existing funnel drops whatever rests on it.
            for (const SchemaStatementResult& statementResult: schema.statements) {
                // What SQLite owns is decided before what this pipeline could do with it: a
                // reserved `sqlite_...` object and a table a module keeps its index in are left out
                // whether or not they parsed, and saying they "did not generate" would name the
                // wrong reason for a row that generates perfectly well.
                if (isReservedSqliteName(statementResult.meta.name)) {
                    markNameOfStatement(statementResult.meta);
                    allWarnings.push_back(std::string(createStatementLabel(statementResult.meta)) + " `" +
                                          statementResult.meta.name +
                                          "` is reserved for SQLite's own use and is not merged into "
                                          "make_storage()");
                    continue;
                }
                // A table FTS5 keeps its index in is the module's own storage: writing to it
                // through a storage corrupts the index, and sync_schema() would go around the
                // module. It is left out of make_storage() exactly as the virtual table it
                // belongs to is, and its name gets no C++ type either, so whatever rests on it
                // goes with it.
                const auto shadowTable = shadowTables.find(normalizeSchemaObjectName(statementResult.meta.name));
                if (shadowTable != shadowTables.end()) {
                    gen.context().markUngeneratableSchemaTable(statementResult.meta.name);
                    allWarnings.push_back("CREATE TABLE `" + statementResult.meta.name +
                                          "` is an internal FTS5 table of virtual table `" + shadowTable->second +
                                          "` and is not merged into make_storage()");
                    continue;
                }
                if (statementResult.pipeline.ok()) {
                    // A virtual table is never merged into make_storage() either — sqlite_orm spells
                    // it `make_virtual_table`, which no storage of a plain schema holds — so its name
                    // has no C++ type behind it whatever it generates, and it is marked right here,
                    // before the first table is generated.
                    if (dynamic_cast<const CreateVirtualTableNode*>(
                            statementResult.pipeline.parseResult.astNodePointer.get())) {
                        gen.context().markUngeneratableSchemaTable(statementResult.meta.name);
                    }
                    continue;
                }
                markNameOfStatement(statementResult.meta);
                allWarnings.push_back(std::string(createStatementLabel(statementResult.meta)) + " `" +
                                      statementResult.meta.name +
                                      "` did not generate and is not merged into make_storage()");
            }

            std::vector<CreateTableParts> tableParts;
            tableParts.reserve(sortedTables.size());
            for (const CreateTableNode* createTableNode: sortedTables) {
                tableParts.push_back(gen.createTableParts(*createTableNode));
            }

            for (size_t tableIndex = 0; tableIndex < sortedTables.size(); ++tableIndex) {
                const CreateTableParts& parts = tableParts[tableIndex];
                allWarnings.insert(allWarnings.end(), parts.warnings.begin(), parts.warnings.end());
                appendUniqueComments(allComments, parts.comments);
                allDecisionPoints.insert(allDecisionPoints.end(),
                                         parts.decisionPoints.begin(),
                                         parts.decisionPoints.end());
                if (parts.makeTableExpression.empty()) {
                    allWarnings.push_back("CREATE TABLE `" +
                                          stripIdentifierQuotes(sortedTables[tableIndex]->tableName) +
                                          "` is not merged into make_storage()");
                    continue;
                }
                declarations << parts.structDeclaration << "\n";
                declarationsCarryAnnotations = declarationsCarryAnnotations || parts.structIsReflected;
                storageArgs.push_back(parts.makeTableExpression);
            }

            // A view that is left out is a name sqlite_orm has no type for, exactly as an
            // ungeneratable table is, so it joins them: a trigger `INSTEAD OF ... ON` it and a view
            // selecting from it go with it. SQLite creates nothing before the view it names, so a
            // dropped view is always marked before anything resting on it is generated.
            for (const SchemaStatementResult& statementResult: schema.statements) {
                if (!statementResult.pipeline.ok() || sqliteOwnsStatement(statementResult.meta)) {
                    continue;
                }
                const AstNode* root = statementResult.pipeline.parseResult.astNodePointer.get();
                if (dynamic_cast<const CreateTableNode*>(root)) {
                    continue;
                }
                if (dynamic_cast<const CreateVirtualTableNode*>(root)) {
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
                if (!gen.context().ungeneratableTables.empty()) {
                    contextBeforeStatement = gen.context();
                }
                gen.context().referencedUngeneratableTables.clear();
                const auto restsOnUngeneratableTable = [&]() {
                    if (gen.context().referencedUngeneratableTables.empty()) {
                        return false;
                    }
                    const std::string kind(gen.context().referencedUngeneratableKind());
                    gen.context() = *contextBeforeStatement;
                    allWarnings.push_back("`" + statementResult.meta.name + "` rests on a " + kind +
                                          " that is not generated and is not merged into "
                                          "make_storage()");
                    return true;
                };

                if (auto* createView = dynamic_cast<const CreateViewNode*>(root)) {
                    CreateViewParts viewParts = gen.createViewParts(*createView);
                    if (restsOnUngeneratableTable()) {
                        // The rollback undid every mark the statement made, so a view that also
                        // failed to generate is marked again here, for the one reason that survives.
                        gen.context().markUngeneratableView(createView->viewName);
                        continue;
                    }
                    allWarnings.insert(allWarnings.end(), viewParts.warnings.begin(), viewParts.warnings.end());
                    appendUniqueComments(allComments, viewParts.comments);
                    allDecisionPoints.insert(allDecisionPoints.end(),
                                             viewParts.decisionPoints.begin(),
                                             viewParts.decisionPoints.end());
                    if (viewParts.makeViewExpression.empty()) {
                        // `createViewParts` has already marked the view as ungeneratable.
                        allWarnings.push_back("CREATE VIEW `" + statementResult.meta.name +
                                              "` is not merged into make_storage()");
                        continue;
                    }
                    // A view has no classical form at all: sqlite_orm maps every one of them by
                    // reflection, so its struct always carries the `[[= "…"_orm_name]]` annotation.
                    declarations << viewParts.structDeclaration << "\n";
                    declarationsCarryAnnotations = true;
                    storageArgs.push_back(viewParts.makeViewExpression);
                    continue;
                }

                if (dynamic_cast<const CreateIndexNode*>(root) || dynamic_cast<const CreateTriggerNode*>(root)) {
                    CodeGenResult fragment = gen.generate(*root);
                    if (restsOnUngeneratableTable()) {
                        continue;
                    }
                    allWarnings.insert(allWarnings.end(), fragment.warnings.begin(), fragment.warnings.end());
                    appendUniqueComments(allComments, fragment.comments);
                    allDecisionPoints.insert(allDecisionPoints.end(),
                                             fragment.decisionPoints.begin(),
                                             fragment.decisionPoints.end());
                    std::string storageArgLine = fragment.code;
                    trimTrailingSemicolon(storageArgLine);
                    if (storageArgLine.empty()) {
                        allWarnings.push_back(std::string(dynamic_cast<const CreateIndexNode*>(root)
                                                              ? "CREATE INDEX"
                                                              : "CREATE TRIGGER") +
                                              " `" + statementResult.meta.name + "` is not merged into make_storage()");
                        continue;
                    }
                    dependentStorageArgs.push_back(storageArgLine);
                }
            }

            storageArgs = storageArgumentOrder(std::move(storageArgs), std::move(dependentStorageArgs));

            if (declarationsCarryAnnotations) {
                // Unqualified lookup is all an annotation gets, and a literal operator
                // (`"users"_orm_name`) gets nothing else at all — not even ADL. This is how
                // sqlite_orm's own reflection tests spell it, above the annotated struct. A header
                // holding no annotated struct is left byte for byte as it was.
                oss << "using namespace sqlite_orm;\n\n";
            }
            oss << declarations.str();

            oss << "\ninline auto make_sqlite_schema_storage(const std::string& db_path) {\n";
            oss << "    using namespace sqlite_orm;\n";
            oss << "    return make_storage(db_path";
            for (const std::string& storageArgument: storageArgs) {
                oss << ",\n        " << storageArgument;
            }
            oss << ");\n}\n";

            std::vector<std::string> dmlStatements;
            for (const SchemaStatementResult& statementResult: schema.statements) {
                if (!statementResult.pipeline.ok()) {
                    continue;
                }
                const AstNode* root = statementResult.pipeline.parseResult.astNodePointer.get();
                if (dynamic_cast<const CreateTableNode*>(root) || dynamic_cast<const CreateIndexNode*>(root) ||
                    dynamic_cast<const CreateTriggerNode*>(root) || dynamic_cast<const CreateVirtualTableNode*>(root) ||
                    dynamic_cast<const CreateViewNode*>(root)) {
                    continue;
                }
                std::optional<CodeGeneratorContext> contextBeforeStatement;
                if (!gen.context().ungeneratableTables.empty()) {
                    contextBeforeStatement = gen.context();
                }
                gen.context().referencedUngeneratableTables.clear();
                CodeGenResult fragment = gen.generate(*root);
                if (!gen.context().referencedUngeneratableTables.empty()) {
                    const std::string kind(gen.context().referencedUngeneratableKind());
                    gen.context() = *contextBeforeStatement;
                    allWarnings.push_back("`" + statementResult.meta.name + "` rests on a " + kind +
                                          " that is not generated and is left out");
                    continue;
                }
                allWarnings.insert(allWarnings.end(), fragment.warnings.begin(), fragment.warnings.end());
                appendUniqueComments(allComments, fragment.comments);
                allDecisionPoints.insert(allDecisionPoints.end(),
                                         fragment.decisionPoints.begin(),
                                         fragment.decisionPoints.end());
                if (!fragment.code.empty()) {
                    dmlStatements.push_back(fragment.code);
                }
            }
            if (!dmlStatements.empty()) {
                oss << "\n";
                for (const std::string& dml: dmlStatements) {
                    oss << dml << "\n";
                }
            }

            std::string prologue = "#pragma once\n\n"
                                   "#include <sqlite_orm/sqlite_orm.h>\n"
                                   "#include <cstdint>\n";
            if (gen.context().spelledInfinity) {
                prologue += "#include <limits>\n";
            }
            prologue += "#include <optional>\n"
                        "#include <string>\n"
                        "#include <vector>\n\n";

            ungeneratableTables = gen.context().ungeneratableTables;
            ungeneratableViews = gen.context().ungeneratableViews;
            return CodeGenResult{prologue + oss.str(),
                                 std::move(allDecisionPoints),
                                 std::move(allWarnings),
                                 {},
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
        // Which tables FTS5 owns is read from the schema, not from what a pass generated, so it is
        // the same answer every pass and is worked out once.
        const std::unordered_map<std::string, std::string> shadowTables = fts5ShadowTables(schema);
        for (;;) {
            const size_t knownBefore = ungeneratableTables.size();
            CodeGenResult result =
                generateHeaderPass(schema, policy, shadowTables, ungeneratableTables, ungeneratableViews);
            if (ungeneratableTables.size() == knownBefore) {
                return result;
            }
        }
    }

}  // namespace sqlite2orm
