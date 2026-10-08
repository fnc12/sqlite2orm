#include "codegen_context.h"
#include "codegen_ddl.h"
#include "codegen_ddl_internal.h"
#include "codegen_utils.h"
#include "ddl_serialization_scope.h"
#include "view_field_type_inferrer.h"
#include <sqlite2orm/codegen.h>
#include <sqlite2orm/utils.h>

#include <map>

namespace sqlite2orm {

    std::string viewDisplayName(const CreateViewNode& node) {
        std::string displayName;
        if (node.viewSchemaName) {
            displayName = *node.viewSchemaName;
            displayName += '.';
        }
        displayName += node.viewName;
        return displayName;
    }

    CreateViewParts DdlCodeGenerator::createViewParts(const CreateViewNode& node) {
        // A CREATE VIEW is a whole statement, so this is where the comments its body recorded —
        // every expression of the SELECT included — are taken out of the context. The mark is what
        // keeps the take to this statement: the generator this one is reached through is public, so
        // it may have been handed an unrelated node before, and that node's comment is not this
        // view's.
        const size_t commentMark = this->context.commentMark();
        const size_t placeholderMark = this->context.placeholderMark();
        CreateViewParts parts = this->viewParts(node);
        if (!parts.makeViewExpression.empty() && this->context.placeheldInExpressionSince(placeholderMark)) {
            // The body holds a construct with no sqlite_orm form, and the placeholder standing for
            // it is a comment: `make_view<V>(select(/* (SELECT ...) */))` is not C++. The view is
            // given up on here, so that the branch below leaves it out the way every other
            // ungeneratable view is left out.
            parts.warnings.push_back("CREATE VIEW " + viewDisplayName(node) +
                                     ": the SELECT holds a construct that is not mapped to sqlite_orm, so the "
                                     "view is not generated");
            parts.makeViewExpression.clear();
        }
        if (parts.makeViewExpression.empty()) {
            // The body may well have generated and recorded before the view was given up on — a
            // stored hex literal past int64 stops it after that — but the statement ends up as a
            // placeholder, so the comment has no form left to explain and goes with the code.
            // The placeholders it generated go the same way: the code holding them is the code
            // that is not there, and the statement is left with the header placeholder, which is a
            // comment line of its own and compiles.
            this->context.discardCommentsSince(commentMark);
            this->context.discardPlaceholdersSince(placeholderMark);
            // A view sqlite_orm has no make_view() for is a name it has no type for either, so
            // whatever rests on the view — a trigger INSTEAD OF it, a view selecting from it —
            // has to go with it, exactly as it does for a table left out of the storage.
            this->context.markUngeneratableView(node.viewName);
            return parts;
        }
        parts.comments = this->context.takeCommentsSince(commentMark);
        return parts;
    }

    CreateViewParts DdlCodeGenerator::viewParts(const CreateViewNode& node) {
        // A view is created from the text sqlite_orm serializes its body into, so a value written
        // in the body reaches SQLite as that text rather than bound the way a query's value is.
        const DdlSerializationScope ddlScope{this->context};
        CreateViewParts parts;
        const std::string displayName = viewDisplayName(node);
        if (!node.selectQuery) {
            parts.warnings.push_back("CREATE VIEW " + displayName + " has no SELECT; cannot generate make_view()");
            return parts;
        }
        if (node.viewSchemaName) {
            parts.warnings.push_back(
                "schema-qualified view name is not represented in sqlite_orm; generated code uses unqualified "
                "view name only");
        }

        // A view body is stored, never compiled, so SQLite accepts a hex literal in it that it
        // refuses in a query; C++ has no literal for one, so the view cannot be generated.
        this->context.storedHexLiteralsTooBig.clear();
        this->context.ddlBlobLiterals.clear();
        const bool viewWasStoredExpression = this->context.storedExpression;
        this->context.storedExpression = true;
        CodeGenResult selectExpression = this->coordinator.tryCodegenSelectLikeSubquery(*node.selectQuery);
        this->context.storedExpression = viewWasStoredExpression;
        for (const std::string& literal: this->context.storedHexLiteralsTooBig) {
            parts.warnings.push_back("CREATE VIEW " + displayName + " uses " + literal +
                                     ", too big for a signed 64-bit integer: SQLite stores the view but refuses "
                                     "every query against it, and C++ has no literal for it, so the view is not "
                                     "generated");
        }
        if (!this->context.storedHexLiteralsTooBig.empty()) {
            return parts;
        }
        for (const std::string& literal: this->context.ddlBlobLiterals) {
            parts.warnings.push_back("CREATE VIEW " + displayName + " uses " + ddlBlobLiteralReason(literal) +
                                     ", so the view is not generated");
        }
        if (!this->context.ddlBlobLiterals.empty()) {
            return parts;
        }
        parts.decisionPoints.insert(parts.decisionPoints.end(),
                                    std::make_move_iterator(selectExpression.decisionPoints.begin()),
                                    std::make_move_iterator(selectExpression.decisionPoints.end()));
        appendUniqueWarnings(parts.warnings, selectExpression.warnings);
        if (selectExpression.code.empty()) {
            parts.warnings.push_back("CREATE VIEW " + displayName +
                                     ": SELECT is not supported for sqlite_orm code generation");
            return parts;
        }

        const SelectNode* firstSelect = dynamic_cast<const SelectNode*>(node.selectQuery.get());
        if (!firstSelect) {
            if (auto* compound = dynamic_cast<const CompoundSelectNode*>(node.selectQuery.get())) {
                if (!compound->selects.empty()) {
                    firstSelect = dynamic_cast<const SelectNode*>(compound->selects.front().get());
                }
            }
        }
        if (!firstSelect) {
            parts.warnings.push_back("CREATE VIEW " + displayName + ": cannot derive view columns from its SELECT");
            return parts;
        }

        const ViewFieldTypeInferrer inferrer(this->context, *firstSelect);
        const std::string rawViewName = stripIdentifierQuotes(node.viewName);
        const std::string structName = toStructName(node.viewName);

        struct ViewField {
            std::string cppName;
            std::string cppType;
            bool nullable = false;
        };
        std::vector<ViewField> fields;
        std::vector<SourceTableColumn> registeredColumns;

        std::map<std::string, std::string> membersByName;
        auto appendField = [&](std::string sqlName,
                               const std::optional<InferredFieldType>& inferred,
                               std::optional<SourceLocation> location,
                               size_t underlineLength,
                               const SourceSpan& nameSpan = {}) {
            ViewField field;
            field.cppName = toCppIdentifier(sqlName);
            if (auto warning = recordMemberName("view " + rawViewName,
                                                sqlName,
                                                field.cppName,
                                                nameSpan,
                                                membersByName,
                                                MemberDeclaration::reflectedViewMember)) {
                parts.warnings.push_back(std::move(*warning));
            }
            if (inferred) {
                field.cppType = inferred->cppType;
                field.nullable = inferred->nullable;
            } else {
                field.cppType = defaultCppTypeForSyntheticColumn(field.cppName);
                std::string message = "view " + rawViewName + ": type of column `" + sqlName +
                                      "` could not be inferred; defaulting to " + field.cppType;
                if (location) {
                    parts.warnings.push_back(CodegenWarning{std::move(message), *location, underlineLength});
                } else {
                    parts.warnings.push_back(CodegenWarning{std::move(message)});
                }
            }
            registeredColumns.push_back(SourceTableColumn{std::move(sqlName), field.cppType, field.nullable});
            fields.push_back(std::move(field));
        };

        auto expandAllColumnsOf = [&](std::string_view tableName) -> bool {
            const auto tableIterator =
                this->context.sourceTableColumnsByNormalizedName.find(normalizeSqlIdentifier(tableName));
            if (tableIterator == this->context.sourceTableColumnsByNormalizedName.end()) {
                return false;
            }
            for (const SourceTableColumn& column: tableIterator->second) {
                appendField(column.sqlName, InferredFieldType{column.cppType, column.nullable}, std::nullopt, 0);
            }
            return true;
        };

        {
            if (!node.columnNames.empty() && node.columnNames.size() != firstSelect->columns.size()) {
                parts.warnings.push_back("view " + rawViewName +
                                         ": explicit column list size differs from SELECT column count");
            }
            for (size_t columnIndex = 0; columnIndex < firstSelect->columns.size(); ++columnIndex) {
                const SelectColumn& selectColumn = firstSelect->columns.at(columnIndex);
                if (!selectColumn.expression) {
                    // Bare `SELECT *`: expand every FROM table's columns.
                    if (inferrer.scope.sourceNames().empty()) {
                        parts.warnings.push_back("CREATE VIEW " + displayName +
                                                 ": cannot derive view columns from SELECT *");
                        return parts;
                    }
                    for (const std::string& tableName: inferrer.scope.sourceNames()) {
                        if (!expandAllColumnsOf(tableName)) {
                            parts.warnings.push_back("CREATE VIEW " + displayName + ": columns of table `" + tableName +
                                                     "` are unknown; cannot derive view columns from SELECT *");
                            return parts;
                        }
                    }
                    continue;
                }
                if (auto* qualifiedAsterisk =
                        dynamic_cast<const QualifiedAsteriskNode*>(selectColumn.expression.get())) {
                    const std::string_view sourceTable = inferrer.scope.sourceForAlias(qualifiedAsterisk->tableName);
                    if (!expandAllColumnsOf(sourceTable)) {
                        parts.warnings.push_back("CREATE VIEW " + displayName + ": columns of table `" +
                                                 std::string(sourceTable) +
                                                 "` are unknown; cannot derive view columns from `.*`");
                        return parts;
                    }
                    continue;
                }
                // The field's name, and the stretch of the source that spells it: the view's own
                // column list names a field before an alias does, and an alias before the column
                // the expression reads. A name a qualified reference (`t.x`) gives has no span of
                // its own — only the whole reference is one token's worth of location — so a
                // warning about that name goes unanchored, as the type warning below does.
                std::string sqlName;
                SourceSpan nameSpan;
                if (columnIndex < node.columnNames.size()) {
                    sqlName = stripIdentifierQuotes(node.columnNames.at(columnIndex));
                    if (columnIndex < node.columnNameSpans.size()) {
                        nameSpan = node.columnNameSpans.at(columnIndex);
                    }
                } else if (!selectColumn.alias.empty()) {
                    sqlName = stripIdentifierQuotes(selectColumn.alias);
                    nameSpan = selectColumn.aliasSpan;
                } else if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(selectColumn.expression.get())) {
                    sqlName = stripIdentifierQuotes(columnRef->columnName);
                    nameSpan = SourceSpan{columnRef->location, columnRef->columnName};
                } else if (auto* qualifiedRef =
                               dynamic_cast<const QualifiedColumnRefNode*>(selectColumn.expression.get())) {
                    sqlName = stripIdentifierQuotes(qualifiedRef->columnName);
                } else {
                    sqlName = "column_" + std::to_string(columnIndex + 1);
                    parts.warnings.push_back("view " + rawViewName + ": SELECT column " +
                                             std::to_string(columnIndex + 1) +
                                             " has no name; using synthesized field name `" + sqlName + "`");
                }
                std::optional<InferredFieldType> inferred;
                std::optional<SourceLocation> columnLocation;
                size_t underlineLength = 0;
                if (selectColumn.expression) {
                    inferred = inferrer.infer(*selectColumn.expression);
                    // The underline covers the SELECT expression whose type could not be
                    // inferred, as the source spells it, and only when that expression is a bare
                    // column reference — whatever name the field ends up carrying, be it one from
                    // the view's column list or an alias. A qualified reference, or any other
                    // expression, has no single token to measure at that location, so such a
                    // warning is left unanchored rather than underlining whatever is there.
                    if (const auto* columnRef = dynamic_cast<const ColumnRefNode*>(selectColumn.expression.get())) {
                        columnLocation = columnRef->location;
                        underlineLength = underlineLengthOf(columnRef->columnName);
                    }
                }
                // A field read back through a call whose result type the generated code spells
                // out carries that type, exactly as a SELECT result column does — a view's struct
                // is a reading surface of its own — so the same report is made for it, named by
                // the field it is about and anchored at the call in the view's body.
                if (selectColumn.expression) {
                    if (auto warning = commonArgumentTypeWarning(*selectColumn.expression,
                                                                 "view " + rawViewName + ": column `" + sqlName + "`",
                                                                 inferrer.scope.resolver())) {
                        parts.warnings.push_back(std::move(*warning));
                    }
                }
                appendField(std::move(sqlName), inferred, columnLocation, underlineLength, nameSpan);
            }
        }

        std::string structDeclaration =
            "struct [[= " + cppStringLiteral(rawViewName) + "_orm_name]] " + structName + " {\n";
        for (const ViewField& field: fields) {
            if (field.nullable) {
                structDeclaration += "    std::optional<" + field.cppType + "> " + field.cppName + ";\n";
            } else {
                structDeclaration +=
                    "    " + field.cppType + " " + field.cppName + defaultInitializer(field.cppType) + ";\n";
            }
        }
        structDeclaration += "};\n";

        this->context.registerSourceTable(rawViewName, std::move(registeredColumns));

        parts.structDeclaration = std::move(structDeclaration);
        parts.makeViewExpression = "make_view<" + structName + ">(" + selectExpression.code + ")";
        // The hint is about the view as a whole, so it underlines the statement's opening keywords
        // as the source spells them — where the warning about the same mapping stands below.
        this->context.recordComment(
            sourceSpanComment(kCommentViewReflection, SourceSpan{node.location, node.headerText}));
        // SQLite stores a view body without compiling it, so the CREATE VIEW carrying an `inf`
        // goes through and every query against the view is the one refused — checked on sqlite3
        // 3.51.0 and on the libsqlite3 3.45.1 the tests link. The `inf` itself comes out of the one
        // serializer every object's DDL is written by, measured on a table and on a trigger; a
        // generated view cannot be built here to measure, as `make_view` needs a C++26 compiler.
        // Said only here, past every point the view is given up at: one that is not generated has
        // no CREATE VIEW for sync_schema() to run, and the warnings of such a view would describe
        // an object the consumer never gets.
        for (const DdlInfinityLiteral& literal: this->context.ddlInfinityLiterals) {
            parts.warnings.push_back(
                ddlInfinityWarning(literal,
                                   "view " + displayName,
                                   "sync_schema() creates a view that refuses every query against it "
                                   "(\"no such column: inf\")"));
        }
        // sqlite_orm maps views to C++26 reflection (make_view + [[= "…"_orm_name]]); there is no
        // pre-C++26 form. Surface a visible warning (anchored at the statement's opening keywords,
        // as the source spells them) when the target is lower.
        if (policyTargetCppStandard(this->context.codeGenPolicy) < 26) {
            std::string message =
                "CREATE VIEW " + displayName +
                ": sqlite_orm views use C++26 reflection (make_view + [[= \"…\"_orm_name]]); this code "
                "requires C++26 and will not compile under the selected C++ standard";
            if (node.headerText.empty()) {
                parts.warnings.push_back(CodegenWarning{std::move(message)});
            } else {
                parts.warnings.push_back(
                    CodegenWarning{std::move(message), node.location, underlineLengthOf(node.headerText)});
            }
        }
        return parts;
    }

}  // namespace sqlite2orm
