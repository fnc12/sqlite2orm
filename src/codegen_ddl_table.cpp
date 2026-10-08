#include "codegen_context.h"
#include "codegen_ddl.h"
#include "codegen_ddl_internal.h"
#include "codegen_utils.h"
#include "ddl_serialization_scope.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <algorithm>
#include <map>

namespace sqlite2orm {

    CreateTableParts DdlCodeGenerator::createTableParts(const CreateTableNode& createTable) {
        // A table is created from the text sqlite_orm serializes it into, so a value written in one
        // of its clauses reaches SQLite as that text rather than bound the way a query's value is.
        const DdlSerializationScope ddlScope{this->context};
        // Where the comments of this statement start, for the take at the end of it: a public
        // generator may have generated something else before this table, and that node's comment
        // belongs to it, not here.
        const size_t commentMark = this->context.commentMark();
        const size_t placeholderMark = this->context.placeholderMark();
        const auto structName = toStructName(createTable.tableName);
        this->context.structName = structName;
        const auto rawTableName = stripIdentifierQuotes(createTable.tableName);
        std::vector<CodegenWarning> warnings;
        // Held back until the table is known to generate, the way the trigger's WHEN-clause
        // warnings above are: a table that is left out has no CREATE TABLE for sync_schema() to
        // run, so naming what its sync_schema() would throw on beside `/* CREATE TABLE t — not
        // supported for sqlite_orm */` says the opposite of what the statement says. It is the
        // same artefact the comments and placeholders of a clause are discarded for below.
        std::vector<CodegenWarning> ddlInfinityWarnings;
        // Columns whose DEFAULT SQLite stores but the generated column leaves out. sync_schema()
        // compares whether a column has a default at all, so a database holding this table is one
        // it drops the table in; that is said once for the table, after the reasons, and only
        // when the table is generated — a table left out has no sync_schema() to drop it.
        std::vector<std::string> columnsWithDroppedDefault;
        bool tableIsGeneratable = true;
        // Whether the table ends up with a primary key at all, and whether one it declares was left
        // out for naming a column the table does not declare: a WITHOUT ROWID table needs its key.
        bool primaryKeyGenerated = false;
        bool tablePrimaryKeyWasLeftOut = false;
        // A DEFAULT / CHECK / generated-column expression is the one place an expression clause
        // reaches codegen without going through the validator, so its warnings are the only word a
        // user gets about it — `-0x8000000000000000` is refused by SQLite wherever it is used, and a
        // negated predicate has no sqlite_orm form at all.
        //
        // SQLite stores the text of such a clause and compiles it only when the clause is used, so
        // it keeps a hex literal no int64 can hold where a query is refused: `stored` asks for the
        // literal to be collected in `storedHexLiteralsTooBig`, which the caller reads right after,
        // rather than to fail the statement. A VIRTUAL generated column is the one clause SQLite
        // compiles at CREATE TABLE time, so it is generated as an ordinary expression.
        //
        // A column reference in such a clause is resolved against the columns this table declares
        // (see the map filled below) under the `ClauseColumnRule` the caller names, because what a
        // name written in one stands for is not the same in every clause. A name the rule leaves no
        // member to be written from is handed back instead of the code, so that a caller cannot
        // reach the generated text without saying what its clause becomes without it.
        struct ClauseExpression {
            /** Generated code, or nothing when `refusedColumns` is not empty. */
            std::optional<std::string> code;
            /** Names the clause referenced that no member may be written from under its rule. */
            std::vector<std::string> refusedColumns;
        };
        auto clauseExpressionCode =
            [this, &warnings](const AstNode& expression, bool stored, ClauseColumnRule rule) -> ClauseExpression {
            this->context.clauseColumnRule = rule;
            // A VIRTUAL generated column asks for an ordinary expression, so the blob journal is
            // cleared here rather than in `generateStoredExpression()`: every clause of the table
            // is serialized, whether or not SQLite only stores it.
            this->context.ddlBlobLiterals.clear();
            // Each clause of the table warns about the infinities written in it by name, so the
            // list starts empty here rather than once for the whole table.
            this->context.ddlInfinityLiterals.clear();
            auto result = stored ? this->coordinator.generateStoredExpression(expression)
                                 : this->coordinator.generateNode(expression);
            warnings.insert(warnings.end(),
                            std::make_move_iterator(result.warnings.begin()),
                            std::make_move_iterator(result.warnings.end()));
            auto refusedColumns = this->context.takeRefusedClauseColumns();
            if (!refusedColumns.empty()) {
                return {std::nullopt, std::move(refusedColumns)};
            }
            return {std::move(result.code), {}};
        };
        // A CHECK is the one clause of this declaration that resolves a name the table declares no
        // column of — `rowid`, `oid`, `_rowid_` are the implicit row id there — and only while the
        // table has a row id at all: on a WITHOUT ROWID one sqlite3 3.51.0 answers `CHECK(rowid>0)`
        // with "no such column: rowid" and reads `CHECK("rowid">0)` as a string like any other name.
        const auto checkClauseRule = createTable.withoutRowid ? ClauseColumnRule::columnOrDoubleQuotedString
                                                              : ClauseColumnRule::columnOrRowIdOrDoubleQuotedString;

        // The reflected form of the table (C++26 `make_table<T>()` over an annotated struct,
        // sqlite_orm #1492) is built beside the classical one: the members are the same, a column
        // constraint becomes a member annotation instead of a `make_column()` argument, and the
        // table name comes from a class-scope `[[= "…"_orm_name]]` annotation. `reflectionBlocker`
        // names the first construct of this table that has no annotation form; while it is empty
        // the reflected form is offered, and targeting C++26 it is the one chosen.
        // Every reason handed to `blockReflection` names one column of this table, so the span of
        // that column's name travels with it: the hint the blocker ends up in is about that column
        // and has to underline it rather than the statement the column stands in.
        std::string reflectionBlocker;
        SourceSpan reflectionBlockerSpan;
        const auto blockReflection = [&reflectionBlocker, &reflectionBlockerSpan](std::string reason,
                                                                                  const SourceSpan& blockerSpan) {
            if (reflectionBlocker.empty()) {
                reflectionBlocker = std::move(reason);
                reflectionBlockerSpan = blockerSpan;
            }
        };
        std::vector<std::string> memberDeclarations;
        memberDeclarations.reserve(createTable.columns.size());

        std::string structDeclaration = "struct " + structName + " {\n";
        std::map<std::string, std::string> membersByName;
        for (const auto& column: createTable.columns) {
            const auto cppName = toCppIdentifier(column.name);
            if (auto warning = recordMemberName("table " + rawTableName,
                                                stripIdentifierQuotes(column.name),
                                                cppName,
                                                column.nameSpan,
                                                membersByName)) {
                warnings.push_back(std::move(*warning));
            }
            const auto cppType = sqliteColumnTypeToCpp(createTable, column);
            const bool nullable = columnMemberIsNullable(createTable, column);
            // No mapped type is the ANY column SQLite has, and what the one chosen for it costs
            // depends on the table around it; the warning is the only word a user gets about it.
            if (auto anyWarning = anyColumnTypeWarning(createTable, column)) {
                warnings.push_back(std::move(*anyWarning));
            }
            std::string memberDeclaration = nullable ? "std::optional<" + cppType + "> " + cppName + ";\n"
                                                     : cppType + " " + cppName + defaultInitializer(cppType) + ";\n";
            structDeclaration += "    " + memberDeclaration;
            memberDeclarations.push_back(std::move(memberDeclaration));
            // A reflected column carries the member's identifier as its name — there is no
            // renaming in sqlite_orm's reflected `make_table<T>()` — so a name C++ cannot spell,
            // or one this generator had to rewrite, has no reflected form at all.
            if (stripIdentifierQuotes(column.name) != cppName) {
                blockReflection("column `" + stripIdentifierQuotes(column.name) +
                                    "` is not a C++ identifier, and a reflected column is named after the member "
                                    "it reflects",
                                column.nameSpan);
            }
        }
        structDeclaration += "};\n";
        this->context.registerSourceTable(rawTableName, sourceTableColumnsFromCreateTable(createTable));

        // From here on every column name a constraint of this table writes is resolved against the
        // declarations above, so that a spelling SQLite reads as the same column — `ID`, `"Id"`,
        // `[id]` for a column declared `"Id"` — is written as the member the declaration produced
        // and not as itself. The map belongs to this table alone; it is emptied again whichever way
        // this function returns, so that a statement generated after it resolves nothing.
        struct ClearConstraintColumnMembers {
            CodeGeneratorContext* context;

            ~ClearConstraintColumnMembers() {
                context->constraintColumnMemberByNormalizedName.clear();
                context->constraintColumnTableNameNormalized.clear();
                context->refusedClauseColumns.clear();
                context->clauseColumnRule = ClauseColumnRule::columnOrDoubleQuotedString;
            }
        } clearConstraintColumnMembers{&this->context};

        this->context.constraintColumnTableNameNormalized = normalizeSqlIdentifier(createTable.tableName);
        for (const auto& column: createTable.columns) {
            this->context.constraintColumnMemberByNormalizedName[normalizeSqlIdentifier(column.name)] =
                toCppIdentifier(column.name);
        }

        // A constraint over a column the table declares none of has no generated form: a member
        // pointer written from a name no declaration answers names nothing, so the header would not
        // compile, and SQLite refuses such a CREATE TABLE outright ("no such column").
        //
        // Except over the implicit row id: `CHECK(rowid > 0)` is a statement sqlite3 3.51.0 takes on
        // a rowid table, and refuses on a WITHOUT ROWID one, and a generated column over `rowid` it
        // refuses either way — so the warning says nothing about the DDL there and names the reason
        // that holds for all of them, that sqlite_orm maps no member onto a row id it never declared.
        const auto warnUnresolvedConstraintColumns = [&warnings,
                                                      &rawTableName](const std::vector<std::string>& columnNames,
                                                                     std::string_view subject,
                                                                     std::string_view consequence) {
            for (const std::string& columnName: columnNames) {
                warnings.push_back(std::string(subject) + " of table " + rawTableName + " names column '" + columnName +
                                   "', which the table does not declare: " +
                                   (isImplicitRowIdName(columnName)
                                        ? "sqlite_orm maps no member onto the implicit row id, so "
                                        : "SQLite refuses such a CREATE TABLE, so ") +
                                   std::string(consequence));
            }
        };

        std::vector<std::string> memberAnnotations(createTable.columns.size());
        // The column CHECKs the classical form spells inside `make_column()`, in column order.
        std::vector<std::string> reflectedColumnChecks;

        std::string makeExpression = "make_table(" + cppStringLiteral(rawTableName);
        for (size_t columnIndex = 0; columnIndex < createTable.columns.size(); ++columnIndex) {
            const auto& column = createTable.columns.at(columnIndex);
            std::string& annotations = memberAnnotations.at(columnIndex);
            const auto annotate = [&annotations](const std::string& constraintCode) {
                annotations += "[[= " + constraintCode + "]] ";
            };
            const auto cppName = toCppIdentifier(column.name);
            const auto rawColumnName = stripIdentifierQuotes(column.name);
            makeExpression +=
                ",\n        make_column(" + cppStringLiteral(rawColumnName) + ", &" + structName + "::" + cppName;
            if (column.primaryKey) {
                std::string primaryKey = "primary_key()";
                // The direction is part of what the key means here: an `INTEGER PRIMARY KEY DESC`
                // is an ordinary column with an index over it rather than the rowid alias, and
                // sqlite_orm writes it back out of `primary_key().desc()`. It goes before the
                // conflict clause, the order SQLite spells the column constraint in.
                if (column.primaryKeySortDirection == SortDirection::asc) {
                    primaryKey += ".asc()";
                } else if (column.primaryKeySortDirection == SortDirection::desc) {
                    primaryKey += ".desc()";
                }
                switch (column.primaryKeyConflict) {
                    case ConflictClause::rollback:
                        primaryKey += ".on_conflict_rollback()";
                        break;
                    case ConflictClause::abort:
                        primaryKey += ".on_conflict_abort()";
                        break;
                    case ConflictClause::fail:
                        primaryKey += ".on_conflict_fail()";
                        break;
                    case ConflictClause::ignore:
                        primaryKey += ".on_conflict_ignore()";
                        break;
                    case ConflictClause::replace:
                        primaryKey += ".on_conflict_replace()";
                        break;
                    case ConflictClause::none:
                        break;
                }
                if (column.autoincrement) {
                    primaryKey += ".autoincrement()";
                }
                makeExpression += ", " + primaryKey;
                annotate(primaryKey);
                primaryKeyGenerated = true;
            }
            if (column.defaultValue) {
                // Only a parenthesized DEFAULT reaches here as an expression — an identifier written
                // without the parentheses is a string to SQLite and the parser reads it as one — and
                // an expression DEFAULT has to be constant, so SQLite refuses every column reference
                // in it, in every spelling and whether or not the table declares such a column.
                const auto defaultClause = clauseExpressionCode(*column.defaultValue, true, ClauseColumnRule::noColumn);
                if (!defaultClause.code) {
                    for (const std::string& columnName: defaultClause.refusedColumns) {
                        warnings.push_back("the DEFAULT of column '" + rawColumnName + "' of table " + rawTableName +
                                           " names column '" + columnName +
                                           "': SQLite refuses a DEFAULT that is not constant (\"default value of "
                                           "column [" +
                                           rawColumnName +
                                           "] is not constant\"), so the generated column has no default_value()");
                    }
                } else if (!this->context.ddlBlobLiterals.empty()) {
                    for (const std::string& literal: this->context.ddlBlobLiterals) {
                        warnings.push_back("the DEFAULT of column '" + rawColumnName + "' of table " + rawTableName +
                                           " uses " + ddlBlobLiteralReason(literal) +
                                           ", so the generated column has no default_value()");
                    }
                    columnsWithDroppedDefault.push_back(rawColumnName);
                } else if (this->context.storedHexLiteralsTooBig.empty()) {
                    const std::string& defaultCode = *defaultClause.code;
                    makeExpression += ", default_value(" + defaultCode + ")";
                    // SQLite keeps `DEFAULT 9e999` in the schema it reads this table from, and C++
                    // spells the value, so the default is generated; it is the DDL sqlite_orm
                    // writes it back as that SQLite refuses. Checked against sqlite3 3.51.0 and the
                    // libsqlite3 3.45.1 the tests link.
                    for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
                        ddlInfinityWarnings.push_back(
                            ddlInfinityWarning(literal,
                                               "the DEFAULT of column '" + rawColumnName + "'",
                                               "sync_schema() throws instead of creating table " + rawTableName +
                                                   " (sqlite_orm writes a DEFAULT in parentheses, and SQLite "
                                                   "answers DEFAULT (inf) with \"default value of column [" +
                                                   rawColumnName + "] is not constant\")"));
                    }
                    // An annotation is a constant expression, so only a default the compiler folds
                    // can stand as one: a text default is a pointer to a string literal and an
                    // expression default is no literal at all.
                    if (isAnnotationConstantValueCode(defaultCode)) {
                        annotate("default_value(" + defaultCode + ")");
                    } else if (dynamic_cast<const CurrentDatetimeLiteralNode*>(column.defaultValue.get())) {
                        // `current_timestamp()` and its two siblings return an empty struct, but
                        // sqlite_orm declares them `inline` without `constexpr`, so calling one is
                        // not a constant expression.
                        blockReflection("the DEFAULT of column `" + rawColumnName + "` is generated as `" +
                                            defaultCode +
                                            "`, which sqlite_orm does not declare constexpr, so an annotation "
                                            "cannot carry it",
                                        column.nameSpan);
                    } else {
                        blockReflection("the DEFAULT of column `" + rawColumnName + "` is generated as `" +
                                            defaultCode +
                                            "`, which is not a constant expression an annotation can "
                                            "carry",
                                        column.nameSpan);
                    }
                } else {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("DEFAULT " + literal + " on column '" + rawColumnName +
                                           "' is too big for a signed 64-bit integer: SQLite stores it but "
                                           "refuses every use of the default, and C++ has no literal for it, so "
                                           "the generated column has no default_value()");
                    }
                    columnsWithDroppedDefault.push_back(rawColumnName);
                }
            }
            if (column.unique) {
                makeExpression += ", unique()";
                // `unique_t` is one of sqlite_orm's column constraints, and a member annotation is
                // handed straight to `make_column()`, whose only gate is that very list — so it
                // travels the same path as the primary key and the collation above.
                annotate("unique()");
                if (column.uniqueConflict != ConflictClause::none) {
                    warnings.push_back("UNIQUE ON CONFLICT clause on column '" + rawColumnName +
                                       "' is not supported by sqlite_orm::unique()");
                }
            }
            if (column.checkExpression) {
                const auto checkClause = clauseExpressionCode(*column.checkExpression, true, checkClauseRule);
                if (!checkClause.code) {
                    warnUnresolvedConstraintColumns(checkClause.refusedColumns,
                                                    "the CHECK on column '" + rawColumnName + "'",
                                                    "the generated column has no check()");
                } else if (!this->context.ddlBlobLiterals.empty()) {
                    for (const std::string& literal: this->context.ddlBlobLiterals) {
                        warnings.push_back("CHECK on column '" + rawColumnName + "' uses " +
                                           ddlBlobLiteralReason(literal) + ", so the generated column has no check()");
                    }
                } else if (this->context.storedHexLiteralsTooBig.empty()) {
                    makeExpression += ", check(" + *checkClause.code + ")";
                    for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
                        ddlInfinityWarnings.push_back(
                            ddlInfinityWarning(literal,
                                               "the CHECK on column '" + rawColumnName + "'",
                                               "sync_schema() throws instead of creating table " + rawTableName +
                                                   " (\"no such column: inf\")"));
                    }
                    // A column CHECK names the table's own members, and a member annotation is
                    // parsed inside the class, where the members it names are not all declared yet
                    // — upstream sqlite_orm states such annotations are not expressible in C++26.
                    // SQLite checks a column CHECK exactly as it checks a table one, though, and
                    // `check_t` is one of sqlite_orm's table constraints, so the reflected form
                    // passes it to `make_table<T>(…)` instead of annotating the member.
                    reflectedColumnChecks.push_back("check(" + *checkClause.code + ")");
                } else {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("CHECK on column '" + rawColumnName + "' uses " + literal +
                                           ", too big for a signed 64-bit integer: SQLite stores it but refuses "
                                           "every use of the constraint, and C++ has no literal for it, so the "
                                           "generated column has no check()");
                    }
                }
            }
            if (!column.collation.empty()) {
                const auto lower = toLowerAscii(column.collation);
                if (lower == "nocase") {
                    makeExpression += ", collate_nocase()";
                    annotate("collate_nocase()");
                } else if (lower == "binary") {
                    makeExpression += ", collate_binary()";
                    annotate("collate_binary()");
                } else if (lower == "rtrim") {
                    makeExpression += ", collate_rtrim()";
                    annotate("collate_rtrim()");
                } else {
                    warnings.push_back("COLLATE " + column.collation + " on column '" + rawColumnName +
                                       "' is not a built-in collation in sqlite_orm");
                }
            }
            if (column.generatedExpression) {
                // Only a STORED generated column is stored text: SQLite compiles it when a row is
                // written, so it keeps a hex literal past the int64 range that it refuses in a
                // VIRTUAL one (the default) already at CREATE TABLE time. C++ has no literal for
                // it, and a column that lost its as(...) would be an ordinary column instead of a
                // generated one, so the whole table is left out rather than reshaped.
                const bool storedGenerated = column.generatedStorage == ColumnDef::GeneratedStorage::stored;
                const auto generatedClause = clauseExpressionCode(*column.generatedExpression,
                                                                  storedGenerated,
                                                                  ClauseColumnRule::columnOrDoubleQuotedString);
                if (!generatedClause.code) {
                    // A generated column that lost its `as(...)` would be an ordinary column, which
                    // is a table SQLite does not have — the same reason the hex-literal case below
                    // leaves the whole table out rather than reshaping it.
                    warnUnresolvedConstraintColumns(generatedClause.refusedColumns,
                                                    "generated column '" + rawColumnName + "'",
                                                    "the table is not generated");
                    tableIsGeneratable = false;
                } else if (!this->context.ddlBlobLiterals.empty()) {
                    // A column that lost its `as(...)` would be an ordinary column, so the whole
                    // table goes rather than a table SQLite does not have.
                    for (const std::string& literal: this->context.ddlBlobLiterals) {
                        warnings.push_back("generated column '" + rawColumnName + "' uses " +
                                           ddlBlobLiteralReason(literal) + ", so the table is not generated");
                    }
                    tableIsGeneratable = false;
                } else if (storedGenerated && !this->context.storedHexLiteralsTooBig.empty()) {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("STORED generated column '" + rawColumnName + "' uses " + literal +
                                           ", too big for a signed 64-bit integer: SQLite stores the table but "
                                           "refuses every row written to it, and C++ has no literal for it, so "
                                           "the table is not generated");
                    }
                    tableIsGeneratable = false;
                } else {
                    if (column.generatedAlways) {
                        makeExpression += ", generated_always_as(" + *generatedClause.code + ")";
                    } else {
                        makeExpression += ", as(" + *generatedClause.code + ")";
                    }
                    // A generated column is refused at CREATE TABLE time whether it is STORED or
                    // VIRTUAL: the name `inf` is resolved against the table's columns as the
                    // statement is read, not when a row is written.
                    for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
                        ddlInfinityWarnings.push_back(
                            ddlInfinityWarning(literal,
                                               "generated column '" + rawColumnName + "'",
                                               "sync_schema() throws instead of creating table " + rawTableName +
                                                   " (\"no such column: inf\")"));
                    }
                    // The expression names the struct's own members, which a member annotation
                    // cannot, and unlike a CHECK it belongs to the column: there is no table
                    // constraint of sqlite_orm to pass it to `make_table<T>(…)` as.
                    blockReflection("generated column `" + rawColumnName +
                                        "` names members of the struct being declared, which an annotation cannot",
                                    column.nameSpan);
                    if (column.generatedStorage == ColumnDef::GeneratedStorage::stored) {
                        makeExpression += ".stored()";
                    } else if (column.generatedStorage == ColumnDef::GeneratedStorage::virtual_) {
                        makeExpression += ".virtual_()";
                    }
                }
            }
            makeExpression += ")";
        }
        // Table-level constraints are collected rather than appended: the classical form spells
        // them after the columns of `make_table("name", …)`, the reflected form passes the same
        // text to `make_table<T>(…)`, whose columns come from the struct instead.
        std::vector<std::string> tableConstraints;

        auto findPrimaryKeyColumnCpp = [this, &createTable](std::string_view refTable) -> std::string {
            if (toLowerAscii(std::string(refTable)) == toLowerAscii(createTable.tableName)) {
                for (const auto& column: createTable.columns) {
                    if (column.primaryKey) {
                        return toCppIdentifier(column.name);
                    }
                }
                if (!createTable.primaryKeys.empty() && !createTable.primaryKeys[0].columns.empty()) {
                    // The key names its column as the constraint spelled it, and the member is named
                    // after the declaration: `REFERENCES t` back into a table whose `PRIMARY KEY(ID)`
                    // stands over a column declared `"Id"` is the member `Id`. A name the table
                    // declares no column of answers nothing, the same as a table with no key at all —
                    // its `primary_key()` is left out below too.
                    if (const auto member =
                            this->context.constraintColumnMember(createTable.primaryKeys[0].columns[0].name)) {
                        return *member;
                    }
                }
            }
            return {};
        };
        for (const auto& column: createTable.columns) {
            if (!column.foreignKey) {
                continue;
            }
            auto& foreignKey = *column.foreignKey;
            const auto cppName = toCppIdentifier(column.name);
            // sqlite_orm resolves a foreign key against the table its storage maps for the
            // referenced type, so a key into a table this batch cannot map, or into a virtual
            // table no storage holds, would not compile at all.
            if (this->context.isUngeneratableForeignKeyParent(foreignKey.table)) {
                warnings.push_back("foreign key on column '" + stripIdentifierQuotes(column.name) + "' references " +
                                   stripIdentifierQuotes(foreignKey.table) +
                                   ", which is not generated, so the generated table has no foreign_key()");
                continue;
            }
            // SQLite stores a foreign key into a table that does not exist — it resolves the
            // parent only when enforcement is on — so a schema can name a parent nothing creates.
            // That name gets no struct either, and `references(&O::x)` would not compile at all,
            // so the key is left out exactly as a key into an ungenerated table is.
            if (this->context.isNameOutsideSchema(foreignKey.table)) {
                warnings.push_back("foreign key on column '" + stripIdentifierQuotes(column.name) + "' references " +
                                   stripIdentifierQuotes(foreignKey.table) +
                                   ", which this schema does not create, so the generated table has no "
                                   "foreign_key()");
                continue;
            }
            const auto referencedStructName = toStructName(foreignKey.table);
            std::string referencedColumnName;
            if (!foreignKey.column.empty()) {
                referencedColumnName = this->context.sourceColumnMember(foreignKey.table, foreignKey.column);
            } else {
                referencedColumnName = findPrimaryKeyColumnCpp(foreignKey.table);
                if (referencedColumnName.empty()) {
                    referencedColumnName = cppName;
                }
            }
            std::string constraint = "foreign_key(&" + structName + "::" + cppName + ").references(&" +
                                     referencedStructName + "::" + referencedColumnName + ")";
            const auto actionString = [](ForeignKeyAction action) -> std::string {
                switch (action) {
                    case ForeignKeyAction::cascade:
                        return ".cascade()";
                    case ForeignKeyAction::restrict_:
                        return ".restrict_()";
                    case ForeignKeyAction::setNull:
                        return ".set_null()";
                    case ForeignKeyAction::setDefault:
                        return ".set_default()";
                    case ForeignKeyAction::noAction:
                        return ".no_action()";
                    case ForeignKeyAction::none:
                        return "";
                }
                return "";
            };
            if (foreignKey.onDelete != ForeignKeyAction::none) {
                constraint += ".on_delete" + actionString(foreignKey.onDelete);
            }
            if (foreignKey.onUpdate != ForeignKeyAction::none) {
                constraint += ".on_update" + actionString(foreignKey.onUpdate);
            }
            tableConstraints.push_back(std::move(constraint));
            if (foreignKey.deferrability != Deferrability::none) {
                std::string description =
                    foreignKey.deferrability == Deferrability::deferrable ? "DEFERRABLE" : "NOT DEFERRABLE";
                if (foreignKey.initially == InitialConstraintMode::deferred)
                    description += " INITIALLY DEFERRED";
                else if (foreignKey.initially == InitialConstraintMode::immediate)
                    description += " INITIALLY IMMEDIATE";
                warnings.push_back(description + " on foreign key for column '" + column.name +
                                   "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for (const auto& tableForeignKey: createTable.foreignKeys) {
            const auto keyColumnMember = this->context.constraintColumnMember(tableForeignKey.column);
            if (!keyColumnMember) {
                warnUnresolvedConstraintColumns({stripIdentifierQuotes(tableForeignKey.column)},
                                                "the FOREIGN KEY",
                                                "the generated table has no foreign_key()");
                continue;
            }
            const std::string& cppName = *keyColumnMember;
            if (this->context.isUngeneratableForeignKeyParent(tableForeignKey.references.table)) {
                warnings.push_back("table-level foreign key on column '" +
                                   stripIdentifierQuotes(tableForeignKey.column) + "' references " +
                                   stripIdentifierQuotes(tableForeignKey.references.table) +
                                   ", which is not generated, so the generated table has no foreign_key()");
                continue;
            }
            // Same as the column form above: a parent the schema never creates has no struct.
            if (this->context.isNameOutsideSchema(tableForeignKey.references.table)) {
                warnings.push_back("table-level foreign key on column '" +
                                   stripIdentifierQuotes(tableForeignKey.column) + "' references " +
                                   stripIdentifierQuotes(tableForeignKey.references.table) +
                                   ", which this schema does not create, so the generated table has no "
                                   "foreign_key()");
                continue;
            }
            const auto referencedStructName = toStructName(tableForeignKey.references.table);
            std::string referencedColumnName;
            if (!tableForeignKey.references.column.empty()) {
                referencedColumnName = this->context.sourceColumnMember(tableForeignKey.references.table,
                                                                        tableForeignKey.references.column);
            } else {
                referencedColumnName = findPrimaryKeyColumnCpp(tableForeignKey.references.table);
                if (referencedColumnName.empty()) {
                    referencedColumnName = cppName;
                }
            }
            std::string constraint = "foreign_key(&" + structName + "::" + cppName + ").references(&" +
                                     referencedStructName + "::" + referencedColumnName + ")";
            const auto actionString = [](ForeignKeyAction action) -> std::string {
                switch (action) {
                    case ForeignKeyAction::cascade:
                        return ".cascade()";
                    case ForeignKeyAction::restrict_:
                        return ".restrict_()";
                    case ForeignKeyAction::setNull:
                        return ".set_null()";
                    case ForeignKeyAction::setDefault:
                        return ".set_default()";
                    case ForeignKeyAction::noAction:
                        return ".no_action()";
                    case ForeignKeyAction::none:
                        return "";
                }
                return "";
            };
            if (tableForeignKey.references.onDelete != ForeignKeyAction::none) {
                constraint += ".on_delete" + actionString(tableForeignKey.references.onDelete);
            }
            if (tableForeignKey.references.onUpdate != ForeignKeyAction::none) {
                constraint += ".on_update" + actionString(tableForeignKey.references.onUpdate);
            }
            tableConstraints.push_back(std::move(constraint));
            if (tableForeignKey.references.deferrability != Deferrability::none) {
                std::string description = tableForeignKey.references.deferrability == Deferrability::deferrable
                                              ? "DEFERRABLE"
                                              : "NOT DEFERRABLE";
                if (tableForeignKey.references.initially == InitialConstraintMode::deferred)
                    description += " INITIALLY DEFERRED";
                else if (tableForeignKey.references.initially == InitialConstraintMode::immediate)
                    description += " INITIALLY IMMEDIATE";
                warnings.push_back(description + " on table-level foreign key for column '" + tableForeignKey.column +
                                   "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        // sqlite_orm takes plain member pointers in a table-level key: there is no place in
        // `primary_key(&T::a, &T::b)` for the collation or the direction one of those names was
        // spelled with. `primary_key(...).desc()` is no substitute — it is the column-level
        // `PRIMARY KEY DESC` spelling and puts the keyword before the list, which SQLite refuses
        // as `near "DESC": syntax error`. An ASC is the direction SQLite would take anyway, so
        // only a DESC is worth telling about.
        // The warnings are collected per key rather than told right away: a key naming a column the
        // table declares nothing of is left out of the generated table altogether, and saying that a
        // DESC of it was ignored would describe a key the user never gets.
        // A key may spell one column more than once, and the same spelling twice: the two
        // occurrences of `PRIMARY KEY(a DESC, a DESC)` have the very same thing to say, and saying
        // it twice tells the reader about two places when there is one thing to fix. A message
        // already collected for this key is left at the one it was collected at.
        const auto warnAboutKeySpelling =
            [](std::vector<std::string>& keyWarnings, const KeyColumn& keyColumn, std::string_view keyKind) {
                const std::string where = std::string(" on column '") + stripIdentifierQuotes(keyColumn.name) +
                                          "' of a table-level " + std::string(keyKind) +
                                          " is not supported in sqlite_orm — ignored in codegen";
                const auto tellOnce = [&keyWarnings](std::string message) {
                    if (std::find(keyWarnings.begin(), keyWarnings.end(), message) == keyWarnings.end()) {
                        keyWarnings.push_back(std::move(message));
                    }
                };
                if (!keyColumn.collation.empty()) {
                    tellOnce("COLLATE " + keyColumn.collation + where);
                }
                if (keyColumn.sortDirection == SortDirection::desc) {
                    tellOnce("DESC" + where);
                }
            };
        for (const auto& tablePrimaryKey: createTable.primaryKeys) {
            std::vector<std::string> unresolvedColumns;
            std::vector<std::string> keySpellingWarnings;
            // A name a table-level PRIMARY KEY spells more than once is written once: a column holds
            // a single place in the key SQLite reports, and there is no second place for the repeat
            // to take. `PRAGMA table_info` — all sqlite_orm has to compare a mapped table against —
            // answers `pk = 1` for the `a` of `PRIMARY KEY(a, a)`, and `pk = 1, 2` for the `a` and
            // the `b` of `PRIMARY KEY(a, b, a)`. sqlite_orm, on the other hand, ranks a key column
            // by the *last* place its name takes in `primary_key(...)`, so writing the name twice
            // makes `sync_schema()` see a key the stored table never had — and it answers a key
            // that changed by dropping the table and every row in it.
            // What the place of a column in the key is, exactly, differs between the two kinds of
            // table, and only one of them is a place the collapsed key can keep. A WITHOUT ROWID
            // table drops the repeat from the key itself (convertToWithoutRowidTable) and ranks
            // what is left one after another, which is what naming each column once writes. A rowid
            // table keeps the repeat — in the automatic index, where nothing sqlite_orm reads can
            // see it — and ranks a key column by the TERM its name is first spelled at, leaving the
            // rank the repeat sits at unused: `PRIMARY KEY(a, a, b)` is reported as `pk = 1` for
            // `a` and `pk = 3` for `b`, with no column at 2. (`PRAGMA table_info` stops looking
            // after as many ranks as the table has columns and answers one past that for anything
            // further, so the rank of `b` in `t(a, b)` reads 3 rather than 4 under
            // `PRIMARY KEY(a, a, a, b)` — still not the rank a collapsed key would give it.)
            // Checked against sqlite3 3.51.0.
            std::vector<std::string> spelledNormalizedNames;
            std::vector<std::string> keyMembers;
            for (const auto& keyColumn: tablePrimaryKey.columns) {
                std::string normalizedName = normalizeSqlIdentifier(keyColumn.name);
                const bool alreadySpelled =
                    std::find(spelledNormalizedNames.begin(), spelledNormalizedNames.end(), normalizedName) !=
                    spelledNormalizedNames.end();
                if (!alreadySpelled) {
                    spelledNormalizedNames.push_back(std::move(normalizedName));
                    if (const auto member = this->context.constraintColumnMember(keyColumn.name)) {
                        keyMembers.push_back(*member);
                    } else {
                        unresolvedColumns.push_back(stripIdentifierQuotes(keyColumn.name));
                    }
                }
                // The spelling of every occurrence is told about, the dropped repeat included: a
                // `PRIMARY KEY(a, a DESC)` does carry a DESC the generated key leaves behind.
                warnAboutKeySpelling(keySpellingWarnings, keyColumn, "PRIMARY KEY");
            }
            std::string constraint = "primary_key(";
            for (size_t memberIndex = 0; memberIndex < keyMembers.size(); ++memberIndex) {
                if (memberIndex > 0) {
                    constraint += ", ";
                }
                constraint += "&" + structName + "::" + keyMembers.at(memberIndex);
            }
            constraint += ")";
            if (!unresolvedColumns.empty()) {
                warnUnresolvedConstraintColumns(unresolvedColumns,
                                                "the PRIMARY KEY",
                                                "the generated table has no primary_key()");
                tablePrimaryKeyWasLeftOut = true;
                continue;
            }
            // Naming a repeated column once keeps the ranks SQLite reports only while every repeat
            // sits behind the last column the key names for the first time. Where one does not, the
            // rank it leaves unused is a rank `primary_key(...)` cannot leave: it ranks its columns
            // by their place in its list, one after another. Writing the repeat instead misses by
            // the same one column, the other way — sqlite_orm ranks a name written twice by its
            // last place — so the key cannot be carried over at all, and what is left is to say so
            // before `sync_schema()` drops the table over it.
            std::optional<std::string> columnRepeatedBeforeTheRestIsNamed;
            if (!createTable.withoutRowid) {
                for (size_t termIndex = 0; termIndex < spelledNormalizedNames.size(); ++termIndex) {
                    const auto& keyColumn = tablePrimaryKey.columns.at(termIndex);
                    if (normalizeSqlIdentifier(keyColumn.name) != spelledNormalizedNames.at(termIndex)) {
                        columnRepeatedBeforeTheRestIsNamed = stripIdentifierQuotes(keyColumn.name);
                        break;
                    }
                }
            }
            if (columnRepeatedBeforeTheRestIsNamed) {
                warnings.push_back(
                    "the table-level PRIMARY KEY names column '" + *columnRepeatedBeforeTheRestIsNamed +
                    "' again before the last column of the key is named for the first time, and no key "
                    "sqlite_orm can write is read back as this one. SQLite ranks a key column of a rowid "
                    "table by the term its name is first spelled at and leaves the rank the repeat sits at "
                    "unused — `PRIMARY KEY(a, a, b)` reports 'a' at rank 1 and 'b' at rank 3, with nothing "
                    "at rank 2 — while primary_key() ranks its columns by their place in the list and has "
                    "no rank to leave out. Naming the repeat there instead is no closer: sqlite_orm ranks a "
                    "name written twice by its last place. Either way the mapped key differs from the stored "
                    "one by the rank of a column, so `sync_schema()` drops the table and creates it again, "
                    "losing every row in it. A key whose repeats all come after the last column it names "
                    "first, `PRIMARY KEY(a, b, a)`, and a WITHOUT ROWID table, where SQLite drops the repeat "
                    "from the key itself, leave no unused rank and are mapped as stored");
            }
            // Writing a repeated name once has a price on exactly one shape, and it is told about
            // rather than left for the reader to meet in their own database. SQLite makes a column
            // the rowid alias when the key is over ONE term and the declared type is INTEGER, so
            // `PRIMARY KEY(a, a)` over an `a INTEGER` is no alias — two terms — while the key the
            // generated code carries, naming `a` once, is one. sqlite_orm has no way to write a
            // table-level key of two terms over one column, so the shape cannot be kept: what is
            // left is to say what changed.
            if (spelledNormalizedNames.size() == 1 && tablePrimaryKey.columns.size() > spelledNormalizedNames.size()) {
                const std::string& keptName = spelledNormalizedNames.front();
                for (const ColumnDef& column: createTable.columns) {
                    if (normalizeSqlIdentifier(column.name) != keptName) {
                        continue;
                    }
                    if (columnAloneInTableKeyIsRowidAlias(createTable, column)) {
                        const std::string columnName = stripIdentifierQuotes(column.name);
                        warnings.push_back(
                            "the table-level PRIMARY KEY names column '" + columnName +
                            "' more than once, and the generated primary_key() names it once, because that "
                            "is the one place the column holds in the key SQLite reports. That makes the "
                            "generated key a rowid alias, which the key written here is not: SQLite aliases "
                            "the rowid onto a column when the key is over a single term of declared type "
                            "INTEGER, and this key is written with more terms than one. A database the "
                            "header was generated from is unaffected — `sync_schema()` leaves it alone — "
                            "but in a database created from the generated code an INSERT that leaves '" +
                            columnName +
                            "' out stores the next rowid in it instead of NULL, and a NOT NULL that SQLite "
                            "enforces on the column here, whether declared or implied by STRICT, lets that "
                            "INSERT through rather than refusing it");
                    }
                    break;
                }
            }
            warnings.insert(warnings.end(), keySpellingWarnings.begin(), keySpellingWarnings.end());
            primaryKeyGenerated = true;
            tableConstraints.push_back(std::move(constraint));
        }
        for (const auto& tableUnique: createTable.uniques) {
            // Qualified on purpose: an unqualified unique() with two or more member pointers of the same
            // type is hijacked by std::unique via ADL, because mapped fields live in namespace std.
            std::vector<std::string> unresolvedColumns;
            std::vector<std::string> keySpellingWarnings;
            std::string constraint = "sqlite_orm::unique(";
            for (size_t columnIndex = 0; columnIndex < tableUnique.columns.size(); ++columnIndex) {
                if (columnIndex > 0) {
                    constraint += ", ";
                }
                const auto& keyColumn = tableUnique.columns.at(columnIndex);
                if (const auto member = this->context.constraintColumnMember(keyColumn.name)) {
                    constraint += "&" + structName + "::" + *member;
                } else {
                    unresolvedColumns.push_back(stripIdentifierQuotes(keyColumn.name));
                }
                warnAboutKeySpelling(keySpellingWarnings, keyColumn, "UNIQUE");
            }
            constraint += ")";
            if (!unresolvedColumns.empty()) {
                warnUnresolvedConstraintColumns(unresolvedColumns,
                                                "the UNIQUE constraint",
                                                "the generated table has no unique()");
                continue;
            }
            warnings.insert(warnings.end(), keySpellingWarnings.begin(), keySpellingWarnings.end());
            tableConstraints.push_back(std::move(constraint));
        }
        for (const auto& tableCheck: createTable.checks) {
            if (tableCheck.expression) {
                const auto checkClause = clauseExpressionCode(*tableCheck.expression, true, checkClauseRule);
                if (!checkClause.code) {
                    warnUnresolvedConstraintColumns(checkClause.refusedColumns,
                                                    "the CHECK constraint",
                                                    "the generated table has no check()");
                    continue;
                }
                if (!this->context.ddlBlobLiterals.empty()) {
                    for (const std::string& literal: this->context.ddlBlobLiterals) {
                        warnings.push_back("table CHECK uses " + ddlBlobLiteralReason(literal) +
                                           ", so the generated table has no check()");
                    }
                    continue;
                }
                if (!this->context.storedHexLiteralsTooBig.empty()) {
                    for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
                        warnings.push_back("table CHECK uses " + literal +
                                           ", too big for a signed 64-bit integer: SQLite stores it but refuses "
                                           "every use of the constraint, and C++ has no literal for it, so the "
                                           "generated table has no check()");
                    }
                    continue;
                }
                for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
                    ddlInfinityWarnings.push_back(ddlInfinityWarning(literal,
                                                                     "the CHECK constraint",
                                                                     "sync_schema() throws instead of creating table " +
                                                                         rawTableName + " (\"no such column: inf\")"));
                }
                tableConstraints.push_back("check(" + *checkClause.code + ")");
            }
        }
        for (const std::string& constraint: tableConstraints) {
            makeExpression += ",\n        " + constraint;
        }
        makeExpression += ")";
        if (createTable.withoutRowid) {
            makeExpression += ".without_rowid()";
            if (tablePrimaryKeyWasLeftOut && !primaryKeyGenerated) {
                // A WITHOUT ROWID table has no row id to fall back on, so one that lost the only
                // PRIMARY KEY it declares has nothing left to identify a row by: SQLite refuses such
                // a CREATE TABLE ("PRIMARY KEY missing on table t"), and that is the statement
                // sync_schema would run, so the table is left out whole rather than handed over as
                // a mapping that only compiles.
                warnings.push_back("table " + rawTableName +
                                   " is WITHOUT ROWID and the PRIMARY KEY it declares was left out, so the table "
                                   "is not generated");
                tableIsGeneratable = false;
            }
        }
        if (createTable.strict) {
            warnings.push_back("STRICT is not yet supported in sqlite_orm and was ignored for table " +
                               stripIdentifierQuotes(createTable.tableName) + " (converted as a regular table)");
        }

        if (this->context.placeheldInExpressionSince(placeholderMark)) {
            // A DEFAULT, CHECK or generated-column expression holds a construct with no sqlite_orm
            // form, and the placeholder standing for it is a comment: `check(/* IN (SELECT ...) */)`
            // is not C++, so the table cannot be generated with the clause and cannot be generated
            // without it either — sqlite_orm would map a table SQLite does not have.
            warnings.push_back("a column or table constraint of " + rawTableName +
                               " holds a construct that is not mapped to sqlite_orm, so the table is not "
                               "generated");
            tableIsGeneratable = false;
        }

        if (!tableIsGeneratable) {
            // The clauses around the one that gave the table up generate and record as usual, and
            // the statement still ends up a placeholder, so their comments go with the code that
            // is not there: a CHECK explained next to a table the consumer never gets reads as a
            // comment about the storage it does get. The placeholders they generated go with
            // them, for the same reason: the code that held them is not in the statement either.
            this->context.discardCommentsSince(commentMark);
            this->context.discardPlaceholdersSince(placeholderMark);
            // Nothing sqlite_orm can map this table to, so every statement naming it is left out
            // by whoever assembles the batch; the mark is what tells them which name that is.
            this->context.markUngeneratableTable(createTable.tableName);
            CreateTableParts parts;
            parts.warnings = std::move(warnings);
            return parts;
        }
        // The table is generated, so there is a CREATE TABLE for sync_schema() to run and the
        // infinities written in its clauses are worth naming.
        warnings.insert(warnings.end(),
                        std::make_move_iterator(ddlInfinityWarnings.begin()),
                        std::make_move_iterator(ddlInfinityWarnings.end()));
        if (!columnsWithDroppedDefault.empty()) {
            // A DEFAULT naming a column is not counted: SQLite refuses such a table at CREATE TABLE
            // time, so no database holds it. A CHECK left out is not counted either: sync_schema()
            // does not compare it and leaves the table alone, which is pinned beside the DEFAULT
            // it drops the table over in tests/schema_pipeline_tests.cpp.
            std::string columnList;
            for (size_t index = 0; index < columnsWithDroppedDefault.size(); ++index) {
                columnList += (index == 0 ? "'" : ", '") + columnsWithDroppedDefault.at(index) + "'";
            }
            const bool oneColumn = columnsWithDroppedDefault.size() == 1;
            warnings.push_back(sourceSpanWarning(
                "table " + rawTableName + " declares " + (oneColumn ? "a DEFAULT on column " : "DEFAULTs on columns ") +
                    columnList +
                    " that the generated code leaves out, and sync_schema() tells a column with a "
                    "default from one without: run against a database holding table " +
                    rawTableName +
                    " as declared, it drops the table and creates it again from the generated code, so "
                    "sync_schema() loses every row in it and sync_schema(true) copies the rows over but leaves "
                    "the table without " +
                    (oneColumn ? "that DEFAULT" : "those DEFAULTs"),
                SourceSpan{createTable.location, createTable.headerText}));
        }

        CreateTableParts parts;
        // A CREATE TABLE is a whole statement, so this is where the comments its clauses recorded —
        // every CHECK, DEFAULT and generated-column expression included — are taken out of the context.
        parts.comments = this->context.takeCommentsSince(commentMark);
        // Below C++26 the reflected form does not compile, so it is neither offered nor chosen and
        // the output is the classical one, unchanged.
        if (policyTargetCppStandard(this->context.codeGenPolicy) >= 26) {
            std::string reflectedStructDeclaration =
                "struct [[= " + cppStringLiteral(rawTableName) + "_orm_name]] " + structName + " {\n";
            for (size_t columnIndex = 0; columnIndex < memberDeclarations.size(); ++columnIndex) {
                reflectedStructDeclaration +=
                    "    " + memberAnnotations.at(columnIndex) + memberDeclarations.at(columnIndex);
            }
            reflectedStructDeclaration += "};\n";

            // Column CHECKs go first, the way SQLite reads them before the table's own constraints.
            std::vector<std::string> reflectedConstraints = std::move(reflectedColumnChecks);
            reflectedConstraints.insert(reflectedConstraints.end(), tableConstraints.begin(), tableConstraints.end());
            std::string reflectedMakeExpression = "make_table<" + structName + ">(";
            for (size_t constraintIndex = 0; constraintIndex < reflectedConstraints.size(); ++constraintIndex) {
                reflectedMakeExpression +=
                    (constraintIndex == 0 ? "\n        " : ",\n        ") + reflectedConstraints.at(constraintIndex);
            }
            reflectedMakeExpression += ")";
            if (createTable.withoutRowid) {
                reflectedMakeExpression += ".without_rowid()";
            }

            const std::string classicalCode = structDeclaration + "\n" + makeExpression;
            const std::string reflectedCode = reflectedStructDeclaration + "\n" + reflectedMakeExpression;
            // The reflected form is the default of the two once the target allows it, but a
            // consumer targeting C++26 on a compiler that has no reflection yet has to be able to
            // ask for the classical one back — no released compiler implements P2996 today.
            const bool reflectionOffered = reflectionBlocker.empty();
            const bool reflected =
                reflectionOffered && !policyEquals(this->context.codeGenPolicy, "table_mapping_style", "make_table");

            Option classicalOption{"make_table",
                                   classicalCode,
                                   "make_table(\"name\", make_column(…)) over a plain struct (wider compiler "
                                   "support)"};
            if (!reflectionOffered) {
                classicalOption.comments.push_back(sourceSpanComment(
                    "the C++26 reflection alternative is not offered for this table: " + reflectionBlocker,
                    reflectionBlockerSpan));
            }
            std::vector<Option> options{std::move(classicalOption)};
            if (reflectionOffered) {
                Option reflectedOption{"reflection",
                                       reflectedCode,
                                       "C++26 reflection: annotated struct + make_table<T>()"};
                reflectedOption.minCppStandard = 26;
                reflectedOption.comments.push_back(
                    sourceSpanComment(kCommentTableReflection,
                                      SourceSpan{createTable.location, createTable.headerText}));
                options.push_back(std::move(reflectedOption));
            }
            parts.decisionPoints.push_back(DecisionPoint{this->context.nextDecisionPointId++,
                                                         "table_mapping_style",
                                                         reflected ? "reflection" : "make_table",
                                                         reflected ? reflectedCode : classicalCode,
                                                         std::move(options)});
            if (reflected) {
                structDeclaration = std::move(reflectedStructDeclaration);
                makeExpression = std::move(reflectedMakeExpression);
                parts.comments.push_back(sourceSpanComment(kCommentTableReflection,
                                                           SourceSpan{createTable.location, createTable.headerText}));
                parts.structIsReflected = true;
            }
        }

        parts.structDeclaration = std::move(structDeclaration);
        parts.makeTableExpression = std::move(makeExpression);
        parts.warnings = std::move(warnings);
        return parts;
    }

}  // namespace sqlite2orm
