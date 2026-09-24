#include "codegen_context.h"

#include "codegen_utils.h"

#include <sqlite2orm/utils.h>

#include <utility>

namespace sqlite2orm {

    bool CodeGeneratorContext::useCpp20ColumnAliasStyle() const {
        if (this->columnAliasStyleOverride) {
            // Internal override used to render the C++20 alternative for the options list; ungated.
            return *this->columnAliasStyleOverride == "cpp20_literal";
        }
        return cpp20Allowed(this->codeGenPolicy) &&
               policyEquals(this->codeGenPolicy, "column_alias_style", "cpp20_literal");
    }

    bool CodeGeneratorContext::useCpp20TableAliasStyle() const {
        if (this->withCteCpp20Monikers()) {
            return true;
        }
        return cpp20Allowed(this->codeGenPolicy) && policyEquals(this->codeGenPolicy, "table_alias_style", "cpp20");
    }

    bool CodeGeneratorContext::withCteLegacyColalias() const {
        return this->activeWithCteStyle && *this->activeWithCteStyle == "legacy_colalias";
    }

    bool CodeGeneratorContext::withCteCpp20Monikers() const {
        return this->activeWithCteStyle && *this->activeWithCteStyle == "cpp20_monikers";
    }

    bool CodeGeneratorContext::columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const {
        if (!this->useCpp20ColumnAliasStyle()) {
            return false;
        }
        std::string normalized = toLowerAscii(stripIdentifierQuotes(ref.columnName));
        return this->activeSelectColumnAliasCpp20Vars.find(normalized) != this->activeSelectColumnAliasCpp20Vars.end();
    }

    bool CodeGeneratorContext::isExplicitCteColumn(std::string_view cteKeyNorm, std::string_view columnName) const {
        auto it = this->cteColumnNamesByTableKey.find(std::string(cteKeyNorm));
        if (it == this->cteColumnNamesByTableKey.end()) {
            return false;
        }
        std::string normalizedCol = normalizeSqlIdentifier(columnName);
        for (const auto& colName: it->second) {
            if (normalizeSqlIdentifier(colName) == normalizedCol) {
                return true;
            }
        }
        return false;
    }

    void CodeGeneratorContext::registerSourceTable(std::string_view tableName, std::vector<SourceTableColumn> columns) {
        this->sourceTableColumnsByNormalizedName[normalizeSqlIdentifier(tableName)] = std::move(columns);
    }

    const SourceTableColumn* CodeGeneratorContext::findSourceTableColumn(std::string_view tableName,
                                                                         std::string_view columnName) const {
        const auto tableIterator = this->sourceTableColumnsByNormalizedName.find(normalizeSqlIdentifier(tableName));
        if (tableIterator == this->sourceTableColumnsByNormalizedName.end()) {
            return nullptr;
        }
        const std::string normalizedColumn = normalizeSqlIdentifier(columnName);
        for (const SourceTableColumn& sourceTableColumn: tableIterator->second) {
            if (normalizeSqlIdentifier(sourceTableColumn.sqlName) == normalizedColumn) {
                return &sourceTableColumn;
            }
        }
        return nullptr;
    }

    std::optional<std::string> CodeGeneratorContext::constraintColumnMember(std::string_view columnName) const {
        if (!this->constraintColumnsTableIsBeingGenerated()) {
            return toCppIdentifier(columnName);
        }
        const auto memberIterator =
            this->constraintColumnMemberByNormalizedName.find(normalizeSqlIdentifier(columnName));
        if (memberIterator == this->constraintColumnMemberByNormalizedName.end()) {
            return std::nullopt;
        }
        return memberIterator->second;
    }

    std::string CodeGeneratorContext::clauseColumnMember(std::string_view columnName) {
        if (!this->constraintColumnsTableIsBeingGenerated()) {
            return toCppIdentifier(columnName);
        }
        const auto member = this->constraintColumnMember(columnName);
        if (member && this->clauseColumnRule != ClauseColumnRule::noColumn) {
            return *member;
        }
        // Either the table declares no column of that name, or the clause takes no column reference
        // at all: there is no member this clause may be written from, and the site that asked for
        // the expression answers with a diagnostic instead of with the clause.
        this->refusedClauseColumns.push_back(stripIdentifierQuotes(columnName));
        return member.value_or(toCppIdentifier(columnName));
    }

    std::optional<std::string> CodeGeneratorContext::clauseColumnAsStringLiteral(std::string_view columnName) const {
        if (!this->constraintColumnsTableIsBeingGenerated() || this->clauseColumnRule == ClauseColumnRule::noColumn) {
            return std::nullopt;
        }
        if (columnName.size() < 2 || columnName.front() != '"' || columnName.back() != '"') {
            return std::nullopt;
        }
        if (this->constraintColumnMember(columnName)) {
            return std::nullopt;
        }
        // SQLite reads a double-quoted name as a string only where it resolves to nothing at all,
        // and a CHECK of a rowid table resolves `"rowid"`, `"oid"` and `"_rowid_"` to the implicit
        // row id: the name is that row id there and not a string, so it is answered like any other
        // name no member may be written from.
        if (this->clauseColumnRule == ClauseColumnRule::columnOrRowIdOrDoubleQuotedString &&
            isImplicitRowIdName(columnName)) {
            return std::nullopt;
        }
        return sqlStringLiteralText(columnName);
    }

    std::vector<std::string> CodeGeneratorContext::takeRefusedClauseColumns() {
        return std::exchange(this->refusedClauseColumns, {});
    }

    bool CodeGeneratorContext::constraintColumnsTableIsBeingGenerated() const {
        return !this->constraintColumnTableNameNormalized.empty();
    }

    bool CodeGeneratorContext::constraintColumnsAreOfTable(std::string_view tableName) const {
        return this->constraintColumnsTableIsBeingGenerated() &&
               normalizeSqlIdentifier(tableName) == this->constraintColumnTableNameNormalized;
    }

    std::string CodeGeneratorContext::sourceColumnMember(std::string_view tableName,
                                                         std::string_view columnName) const {
        if (const SourceTableColumn* declared = this->findSourceTableColumn(tableName, columnName)) {
            return toCppIdentifier(declared->sqlName);
        }
        return toCppIdentifier(columnName);
    }

    const SourceTableColumn* CodeGeneratorContext::findReferencedColumn(const AstNode& node) const {
        // A COLLATE or a unary plus generates its operand and nothing else, so the column named
        // under one is the column the expression is built over.
        const AstNode& valueNode = generatedOperandNode(node);
        // What a reference is read back as is the type of the field the form the emitter wrote
        // names it a member of, so what is asked here is which struct that form names — and not
        // which table of the batch happens to declare a column of that name. The struct a source
        // table's rows are read into is `toStructName` of its name, and two SQL names differing
        // only in case name one table, so the match is case-insensitive; two tables of one batch
        // read into struct names that differ only in case answer nothing at all.
        auto columnOfStruct = [this](std::string_view structForReference,
                                     std::string_view columnName) -> const SourceTableColumn* {
            const std::string structKey = toLowerAscii(structForReference);
            if (structKey.empty()) {
                return nullptr;
            }
            const std::vector<SourceTableColumn>* tableColumns = nullptr;
            for (const auto& [tableKey, columns]: this->sourceTableColumnsByNormalizedName) {
                if (toLowerAscii(toStructName(tableKey)) != structKey) {
                    continue;
                }
                if (tableColumns) {
                    return nullptr;
                }
                tableColumns = &columns;
            }
            if (!tableColumns) {
                return nullptr;
            }
            const std::string normalizedColumn = normalizeSqlIdentifier(columnName);
            for (const SourceTableColumn& column: *tableColumns) {
                if (normalizeSqlIdentifier(column.sqlName) == normalizedColumn) {
                    return &column;
                }
            }
            return nullptr;
        };
        if (auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&valueNode)) {
            const std::string tableKeyNorm = normalizeSqlIdentifier(qualifiedRef->tableName);
            if (this->activeCteTypedefByTableKey.find(tableKeyNorm) != this->activeCteTypedefByTableKey.end()) {
                // The form names the CTE — `column<cte_0>(…)` — and sqlite_orm types it out of
                // the SELECT the CTE was built from, which the schema of the batch does not say.
                return nullptr;
            }
            const std::string tableKey(qualifiedRef->tableName);
            if (const auto aliasIterator = this->activeTableAliases.find(tableKey);
                aliasIterator != this->activeTableAliases.end()) {
                return columnOfStruct(aliasIterator->second.baseStructName, qualifiedRef->columnName);
            }
            if (const auto fromIterator = this->fromTableAliasToStructName.find(tableKey);
                fromIterator != this->fromTableAliasToStructName.end()) {
                return columnOfStruct(fromIterator->second, qualifiedRef->columnName);
            }
            // Whatever the qualifier is, the reference comes out a member of the struct that name
            // maps to: a view of the batch answers nothing here, since the schema this reads
            // holds the CREATE TABLEs alone and not the fields a view's struct is built with.
            return columnOfStruct(toStructName(qualifiedRef->tableName), qualifiedRef->columnName);
        }
        if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            const std::string normalized = toLowerAscii(stripIdentifierQuotes(columnRef->columnName));
            if (this->activeSelectColumnAliases.find(normalized) != this->activeSelectColumnAliases.end()) {
                // The form is `get<Alias>()`, typed by the expression the alias was declared over.
                return nullptr;
            }
            if (this->implicitSingleSourceCteTypedef) {
                // Every form a reference that names no table takes under a single CTE source
                // names that CTE, so no field of a source table is read.
                return nullptr;
            }
            // A reference that names no table belongs to the source the select reads, which the
            // emitter has settled into `structName` — or, where that source carries a SQL alias,
            // into the alias's base struct, the one the `alias_column<…>(&T::x)` form names.
            return columnOfStruct(this->implicitSourceAlias ? this->implicitSourceAlias->baseStructName
                                                            : this->structName,
                                  columnRef->columnName);
        }
        return nullptr;
    }

    std::string CodeGeneratorContext::customFunctionArgType(const AstNode& argument) const {
        // The argument a COLLATE or a unary plus stands over is the one the call is handed.
        const AstNode& valueNode = generatedOperandNode(argument);
        if (auto* columnRef = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
            // Schema type wins when the column belongs to a known CREATE TABLE in the batch.
            const std::string normalizedColumn = normalizeSqlIdentifier(columnRef->columnName);
            for (const auto& [tableKey, columns]: this->sourceTableColumnsByNormalizedName) {
                (void)tableKey;
                for (const SourceTableColumn& column: columns) {
                    if (normalizeSqlIdentifier(column.sqlName) == normalizedColumn) {
                        return column.cppType;
                    }
                }
            }
            const std::string cppName = toCppIdentifier(columnRef->columnName);
            const auto known = this->columnTypes.find(cppName);
            if (known != this->columnTypes.end()) {
                return known->second;
            }
            return this->syntheticColumnCppType(cppName);  // name heuristic (name → std::string, else int)
        }
        return this->inferTypeFromNode(valueNode);
    }

    void CodeGeneratorContext::registerColumn(const std::string& cppName, const std::string& cppType) {
        auto [it, inserted] = this->columnTypes.try_emplace(cppName, cppType);
        if (!inserted && it->second == "int" && cppType != "int") {
            it->second = cppType;
        }
    }

    void CodeGeneratorContext::registerPrefixColumn(const std::string& cppName, const std::string& cppType) {
        if (this->implicitSingleSourceCteTypedef) {
            return;
        }
        this->registerColumn(cppName, cppType);
    }

    std::string CodeGeneratorContext::syntheticColumnCppType(std::string_view cppIdentifier) const {
        return defaultCppTypeForSyntheticColumn(cppIdentifier);
    }

    std::string CodeGeneratorContext::inferTypeFromNode(const AstNode& node) const {
        // A COLLATE picks a collating sequence for a comparison and leaves the value it is written
        // over alone, so the value — and the field that holds it — is the one under it.
        const AstNode& valueNode = generatedOperandNode(node);
        if (dynamic_cast<const StringLiteralNode*>(&valueNode))
            return "std::string";
        if (auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
            // An integer literal an int64 cannot hold is a REAL for SQLite, so is the column.
            if (integerLiteralExceedsInt64(integerLiteral->value)) {
                return "double";
            }
            // `int` stays the field for the small literal the comparison usually carries, but one
            // that only an int64 holds would be truncated by it, so it widens the field instead.
            return integerLiteralExceedsInt32(integerLiteral->value) ? "int64_t" : "int";
        }
        if (dynamic_cast<const RealLiteralNode*>(&valueNode))
            return "double";
        if (dynamic_cast<const BoolLiteralNode*>(&valueNode))
            return "bool";
        if (auto* unaryOperator = dynamic_cast<const UnaryOperatorNode*>(&valueNode)) {
            if (unaryOperator->unaryOperator == UnaryOperator::bitwiseNot) {
                // `~` is a 64-bit complement in SQLite, and it takes a value out of the int32
                // range as readily as it brings one back in: `~2147483648` is -2147483649. The
                // field follows the operation rather than the operand, the way it already does
                // for a view column in `ViewFieldTypeInferrer`.
                return "int64_t";
            }
            if (unaryOperator->operand && (unaryOperator->unaryOperator == UnaryOperator::minus ||
                                           unaryOperator->unaryOperator == UnaryOperator::plus)) {
                // The signs standing in front of a literal belong to the value they spell with
                // it, so the width follows all of them together rather than the magnitude alone:
                // `0xFFFFFFFF80000000` is the -2147483648 an `int` holds, `-0xFFFFFFFF80000000`
                // is the 2147483648 it does not, and `-(-2147483648)` is that value again.
                std::size_t foldedSigns = 0;
                auto* signedLiteral =
                    dynamic_cast<const IntegerLiteralNode*>(withoutFoldedSigns(valueNode, foldedSigns));
                if (signedLiteral) {
                    // A value past the int64 range is a REAL for SQLite, whichever side of the
                    // range the signs leave it on.
                    if (isIntegerLiteralPastIntegerFieldRange(valueNode)) {
                        return "double";
                    }
                    return integerLiteralExceedsInt32(signedLiteral->value, foldedSigns % 2 != 0) ? "int64_t" : "int";
                }
                // A minus SQLite cannot fold into a literal — a COLLATE between the two stops
                // the folding — is a negation it computes over 64 bits while it runs the
                // statement, and that takes a value out of the int32 range as readily as a
                // folded sign does: `-(-2147483648 COLLATE BINARY)` is the 2147483648 an `int`
                // does not hold. Negating the int64 minimum leaves the integer range altogether,
                // the way the sign standing over a folded one already does. A plus SQLite's
                // parser drops, so it leaves the width of what stands under it alone.
                const std::string operandType = this->inferTypeFromNode(*unaryOperator->operand);
                if (unaryOperator->unaryOperator != UnaryOperator::minus) {
                    return operandType;
                }
                std::size_t negatedSigns = 0;
                auto* negatedLiteral = dynamic_cast<const IntegerLiteralNode*>(
                    withoutFoldedSigns(generatedOperandNode(*unaryOperator->operand), negatedSigns));
                if (negatedLiteral != nullptr && negatedSigns % 2 != 0 &&
                    integerLiteralExceedsInt64(negatedLiteral->value)) {
                    return "double";
                }
                return operandType == "int" ? "int64_t" : operandType;
            }
        }
        if (auto* nestedCase = dynamic_cast<const CaseNode*>(&valueNode)) {
            // A CASE standing in a branch of another one answers with a value of its own, so the
            // width of that branch is the width of everything the nested CASE can answer with.
            std::vector<const AstNode*> nestedResults;
            nestedResults.reserve(nestedCase->branches.size() + 1);
            for (const auto& branch: nestedCase->branches) {
                nestedResults.push_back(branch.result.get());
            }
            nestedResults.push_back(nestedCase->elseResult.get());
            return this->inferWidestTypeFromNodes(nestedResults);
        }
        if (auto* binaryOperator = dynamic_cast<const BinaryOperatorNode*>(&valueNode)) {
            // SQLite computes arithmetic and bit operations over 64-bit integers, so the result
            // leaves the int32 range even where both operands sit inside it: `2147483647 + 1` is
            // 2147483648. The same widening `ViewFieldTypeInferrer` applies to a view column.
            switch (binaryOperator->binaryOperator) {
                case BinaryOperator::add:
                case BinaryOperator::subtract:
                case BinaryOperator::multiply:
                case BinaryOperator::divide:
                case BinaryOperator::modulo: {
                    const std::string lhsType =
                        binaryOperator->lhs ? this->inferTypeFromNode(*binaryOperator->lhs) : std::string();
                    const std::string rhsType =
                        binaryOperator->rhs ? this->inferTypeFromNode(*binaryOperator->rhs) : std::string();
                    return lhsType == "double" || rhsType == "double" ? "double" : "int64_t";
                }
                case BinaryOperator::bitwiseAnd:
                case BinaryOperator::bitwiseOr:
                case BinaryOperator::shiftLeft:
                case BinaryOperator::shiftRight:
                    return "int64_t";
                default:
                    break;
            }
        }
        return "int";
    }

    std::string CodeGeneratorContext::inferWidestTypeFromNodes(const std::vector<const AstNode*>& nodes) const {
        std::string widest;
        for (const AstNode* node: nodes) {
            if (!node) {
                continue;
            }
            const std::string nodeType = this->inferTypeFromNode(*node);
            widest = widest.empty() ? nodeType : widerInferredCppType(widest, nodeType);
        }
        return widest.empty() ? "int" : widest;
    }

    std::string CodeGeneratorContext::generatePrefix() const {
        if (this->columnTypes.empty()) {
            return "";
        }
        std::string result = "struct " + this->structName + " {\n";
        for (const auto& [name, type]: this->columnTypes) {
            result += "    " + type + " " + name + defaultInitializer(type) + ";\n";
        }
        result += "};";
        return result;
    }

    std::string CodeGeneratorContext::statementVariableName(std::string_view baseName) {
        const std::string base(baseName);
        auto resolved = this->statementVariableNames.find(base);
        if (resolved != this->statementVariableNames.end()) {
            return resolved->second;
        }
        const int use = ++this->batchVariableUses[base];
        std::string name = use == 1 ? base : base + std::to_string(use);
        this->statementVariableNames.emplace(base, name);
        return name;
    }

    void CodeGeneratorContext::registerCustomFunction(CustomFunctionUse use) {
        for (const auto& existing: this->customFunctions) {
            if (existing.structName == use.structName) {
                return;
            }
        }
        this->customFunctions.push_back(std::move(use));
    }

    void CodeGeneratorContext::recordFormWithoutDefaultConstructor(std::string form) {
        for (const auto& existing: this->formsWithoutDefaultConstructor) {
            if (existing == form) {
                return;
            }
        }
        this->formsWithoutDefaultConstructor.push_back(std::move(form));
    }

    void CodeGeneratorContext::recordComment(std::string_view comment) {
        this->comments.emplace_back(comment);
    }

    void CodeGeneratorContext::recordEmittedTableType(std::string typeName) {
        if (typeName.empty()) {
            return;
        }
        if (this->emittingMatchField) {
            // Hidden from sqlite_orm's ast_iterator, so it widens no inferred FROM and is no FROM
            // to infer from either: it goes only into the set the FROM this select writes out has
            // to name.
            this->ownEmittedTableTypes.insert(std::move(typeName));
            return;
        }
        this->ownEmittedTableTypes.insert(typeName);
        this->ownVisibleEmittedTableTypes.insert(typeName);
        this->emittedTableTypes.insert(std::move(typeName));
    }

    size_t CodeGeneratorContext::commentMark() const {
        return this->comments.size();
    }

    GenerationMarks CodeGeneratorContext::mark() const {
        return GenerationMarks{this->commentMark(), this->placeholderMark()};
    }

    std::vector<std::string> CodeGeneratorContext::commentsRecordedSince(size_t mark) const {
        // A take in between leaves fewer than `mark` behind: the statement the comments belong to
        // has carried them off already, so there is nothing left for this node to report.
        std::vector<std::string> recorded;
        for (size_t index = mark; index < this->comments.size(); ++index) {
            appendUniqueString(recorded, this->comments[index]);
        }
        return recorded;
    }

    std::vector<std::string> CodeGeneratorContext::takeCommentsSince(size_t mark) {
        std::vector<std::string> taken = this->commentsRecordedSince(mark);
        this->discardCommentsSince(mark);
        return taken;
    }

    void CodeGeneratorContext::discardCommentsSince(size_t mark) {
        if (mark < this->comments.size()) {
            this->comments.erase(this->comments.begin() + static_cast<std::ptrdiff_t>(mark), this->comments.end());
        }
    }

    void CodeGeneratorContext::recordPlaceholder(PlaceholderSlot slot) {
        this->generatedPlaceholders.push_back(slot);
    }

    size_t CodeGeneratorContext::placeholderMark() const {
        return this->generatedPlaceholders.size();
    }

    bool CodeGeneratorContext::placeheldSince(size_t mark) const {
        return mark < this->generatedPlaceholders.size();
    }

    bool CodeGeneratorContext::placeheldInExpressionSince(size_t mark) const {
        for (size_t index = mark; index < this->generatedPlaceholders.size(); ++index) {
            if (this->generatedPlaceholders[index] == PlaceholderSlot::expression) {
                return true;
            }
        }
        return false;
    }

    bool CodeGeneratorContext::placeheldAsStatementSince(size_t mark) const {
        for (size_t index = mark; index < this->generatedPlaceholders.size(); ++index) {
            if (this->generatedPlaceholders[index] == PlaceholderSlot::statement) {
                return true;
            }
        }
        return false;
    }

    void CodeGeneratorContext::discardPlaceholdersSince(size_t mark) {
        if (mark < this->generatedPlaceholders.size()) {
            this->generatedPlaceholders.erase(this->generatedPlaceholders.begin() + static_cast<std::ptrdiff_t>(mark),
                                              this->generatedPlaceholders.end());
        }
    }

    void CodeGeneratorContext::markUngeneratableTable(std::string_view tableName) {
        this->ungeneratableTables.insert(normalizeSqlIdentifier(tableName));
    }

    void CodeGeneratorContext::markUngeneratableView(std::string_view viewName) {
        this->markUngeneratableTable(viewName);
        this->ungeneratableViews.insert(normalizeSqlIdentifier(viewName));
    }

    bool CodeGeneratorContext::isNameOutsideSchema(std::string_view name) const {
        if (!this->schemaObjectNames) {
            return false;
        }
        return this->schemaObjectNames->find(normalizeSqlIdentifier(name)) == this->schemaObjectNames->end();
    }

    std::string_view CodeGeneratorContext::referencedUngeneratableKind() const {
        for (const std::string& referencedName: this->referencedUngeneratableTables) {
            if (this->ungeneratableViews.find(referencedName) == this->ungeneratableViews.end()) {
                return "table";
            }
        }
        return "view";
    }

    bool CodeGeneratorContext::isUngeneratableTable(std::string_view tableName) const {
        return this->ungeneratableTables.find(normalizeSqlIdentifier(tableName)) != this->ungeneratableTables.end();
    }

    std::string CodeGeneratorContext::structNameForTable(std::string_view tableName) {
        if (this->isUngeneratableTable(tableName)) {
            this->referencedUngeneratableTables.insert(normalizeSqlIdentifier(tableName));
        }
        return toStructName(tableName);
    }

    void CodeGeneratorContext::resetForGeneration() {
        this->accumulatedErrors.clear();
        this->storedExpression = false;
        this->storedHexLiteralsTooBig.clear();
        this->ddlSerializedExpression = false;
        this->ddlBlobLiterals.clear();
        this->formsWithoutDefaultConstructor.clear();
        this->customFunctions.clear();
        this->comments.clear();
        this->generatedPlaceholders.clear();
    }

}  // namespace sqlite2orm
