#pragma once

#include <sqlite2orm/ast.h>
#include <sqlite2orm/codegen_policy.h>
#include <sqlite2orm/codegen_result.h>

#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace sqlite2orm {

    struct TableAliasInfo {
        std::string ormAliasType;
        std::string baseStructName;
    };

    /** A column of a known CREATE TABLE (or view), used to infer view struct field types. */
    struct SourceTableColumn {
        std::string sqlName;
        std::string cppType;
        bool nullable = false;
        /** A generated column, which SQLite computes and an INSERT never supplies a value for. */
        bool generated = false;
    };

    struct Cpp20TableAliasDeclaration {
        std::string variableName;
        std::string baseStructName;
        std::string sqlAlias;
    };

    /** A user-defined / extension function referenced in a statement, turned into a func<>() call. */
    struct CustomFunctionUse {
        std::string sqlName;  // original SQL name, e.g. "morton_encode"
        std::string structName;  // C++ struct name, e.g. "MortonEncode"
        std::vector<std::string> argTypes;  // best-effort C++ type per argument
        std::vector<std::string> argNames;  // parameter names (column name when known, else argN)
        std::string returnType = "int";  // best-effort result type

        bool operator==(const CustomFunctionUse&) const = default;
    };

    /** Where a placeholder a generator produced stands in the code the statement generates. */
    enum class PlaceholderSlot {
        /** The whole code of the statement: a `/*` … `*\/` line of its own, which compiles. */
        statement,
        /** Inside the code of something larger, where a comment is not an expression. */
        expression,
    };

    class CodeGeneratorContext {
      public:
        std::string structName = "User";
        const CodeGenPolicy* codeGenPolicy = nullptr;

        int nextDecisionPointId = 1;
        int nextBindParamIndex = 0;
        std::vector<std::string> accumulatedErrors;
        /**
         *  Set while generating an expression the statement only stores the text of: a column
         *  DEFAULT, a CHECK, a view or a trigger body. SQLite raises `hex literal too big` in
         *  `codeInteger()`, when it compiles an expression, so such a statement is accepted with
         *  a hex literal no compiled expression may hold — and that C++ cannot spell either.
         */
        bool storedExpression = false;
        /**
         *  The hex literals past the int64 range met since the last `generateStoredExpression()`,
         *  as SQLite names them. The generator that owns the clause leaves it out of the generated
         *  code with a warning instead of failing the whole statement.
         */
        std::vector<std::string> storedHexLiteralsTooBig;
        /**
         *  The expressions emitted since the last reset whose sqlite_orm type has no default
         *  constructor, named as SQLite spells them. `make_trigger()` keeps a trigger's WHEN
         *  expression in an `optional_container`, which default-constructs the expression before
         *  assigning it, so a WHEN clause built from any of these does not compile at all. The
         *  trigger generator clears this around the WHEN clause and warns about what it finds.
         */
        std::vector<std::string> formsWithoutDefaultConstructor;
        /**
         *  The comments explaining the generated forms met since the last reset, in the order they
         *  were recorded and a form met twice recorded twice — the readers (`takeCommentsSince`
         *  and `commentsRecordedSince`) are what deduplicate, so that each of them answers with
         *  the distinct comments of its own stretch of the generation.
         *  A comment belongs to the statement whose body the expression was generated in, and
         *  an expression is generated from every clause there is — a CHECK, a column DEFAULT, a
         *  view body, a trigger WHEN, a subquery, a CTE — so the clause generators would each have
         *  to carry the list up by hand to keep it. They do not have to: the generator records the
         *  comment here, and the statement-level entry points (`CodeGenerator::generate`,
         *  `createTableParts`, `createViewParts`) take what was recorded since they started into
         *  their result.
         */
        std::vector<std::string> comments;
        /**
         *  The placeholders generation has produced since the last reset, in the order they were
         *  produced, each saying where it stands. A statement whose whole code is a placeholder
         *  reads as a comment line and compiles; one standing inside the code of something larger
         *  — `storage.select(as<XAlias>(/*` … `*\/))` — is a comment where C++ expects an
         *  expression, so the statement holding it cannot be generated at all. Only
         *  `unsupportedPlaceholder` and `unsupportedStatementPlaceholder` record here, which is
         *  what keeps the journal complete.
         */
        std::vector<PlaceholderSlot> generatedPlaceholders;
        /**
         *  The tables of the current batch that cannot be mapped at all — a STORED generated
         *  column holding such a hex literal leaves the whole table out, because a column that
         *  lost its `as(...)` would be an ordinary column. sqlite_orm cannot reference a type it
         *  does not map, so a foreign key naming one of these is left out too. Normalized names,
         *  filled by whoever assembles a whole schema.
         */
        std::set<std::string> ungeneratableTables;
        /**
         *  The `ungeneratableTables` entries that name a view rather than a table. A view left out
         *  of the storage is a name sqlite_orm has no type for exactly as an ungeneratable table
         *  is, so it joins them; this set only tells the two apart when a warning names the kind.
         */
        std::set<std::string> ungeneratableViews;
        /**
         *  The `ungeneratableTables` entries the statement being generated has turned into a
         *  struct name. Every table reference reaches its struct through `structNameForTable()`,
         *  so however deeply a reference is nested — a subquery in a WHERE, a trigger WHEN clause,
         *  a CTE — it is recorded here, and the caller leaves the whole statement out.
         */
        std::set<std::string> referencedUngeneratableTables;
        std::map<std::string, std::string> columnTypes;
        std::map<std::string, std::string> fromTableAliasToStructName;
        std::map<std::string, TableAliasInfo> activeTableAliases;
        int nextAliasLetter = 0;
        std::map<std::string, std::string> activeCteTypedefByTableKey;
        std::map<std::string, std::string> cteBaseStructByKey;
        /** CTE key (normalized) → explicit SQL column names from the CTE definition. */
        std::map<std::string, std::vector<std::string>> cteColumnNamesByTableKey;
        std::optional<std::string> implicitSingleSourceCteTypedef;
        std::optional<std::string> implicitCteFromTableKeyNorm;
        std::map<std::string, std::string> activeSelectColumnAliases;
        std::map<std::string, std::string> activeSelectColumnAliasCpp20Vars;
        std::optional<std::string> columnAliasStyleOverride;

        /** Normalized table/view name → columns; filled from CREATE TABLE statements seen in this batch. */
        std::map<std::string, std::vector<SourceTableColumn>> sourceTableColumnsByNormalizedName;

        /** Struct of the table a trigger is ON; OLD/NEW refs bind to it, not to the DML target table. */
        std::optional<std::string> triggerSubjectStructName;

        std::optional<std::string> activeWithCteStyle;
        std::map<std::string, std::string> withCteLegacyColVarByPipeKey;
        std::map<std::string, std::string> withCteCpp20MonikerVarByCteKey;
        std::map<std::string, std::string> withCteCpp20ColVarByPipeKey;
        std::map<std::string, std::string> withCteIndexedColVarByPipeKey;
        std::vector<std::string> pendingAnchorCteBindings;
        std::vector<Cpp20TableAliasDeclaration> cpp20TableAliasDeclarations;

        bool suppressWithCteStyleDecisionPoint = false;
        bool suppressTableAliasStyleDecisionPoint = false;
        /**
         *  Set while generating the outer SELECT of a WITH: a bare `SELECT *` must use the
         *  `select(asterisk<T>())` form (which nests into `storage.with(...)`), not `storage.get_all<T>()`
         *  (a storage method that cannot be a `with()` argument). Consumed once by the outer select.
         */
        bool withOuterSelect = false;
        /**
         *  Set while generating the operand of a logical NOT. `operator!` is the one sqlite_orm
         *  operator that keeps the `c(...)` wrapper its operand carries instead of unwrapping it,
         *  and the walker that collects the tables a statement reads stops at such a wrapper. A
         *  column reference generated here therefore takes the `column<T>(&T::x)` form, which
         *  names the same column and which that walker does read.
         */
        bool columnRefUnderLogicalNot = false;
        /**
         *  Set by the column reference branch when `columnRefUnderLogicalNot` made it emit that
         *  column-pointer form, and read back by the NOT that asked for it. A column naming a
         *  SELECT alias answers earlier, with `get<Alias>()`, and leaves this false: no column
         *  pointer came out of it, so it keeps the wrapper and the explanation that form replaces.
         */
        bool emittedColumnPointerUnderLogicalNot = false;
        /**
         *  Set by the column reference branch when the form it emitted names the table the column
         *  belongs to — `&T::x` or `column<T>(&T::x)`. A SELECT alias answers earlier with
         *  `get<Alias>()`, and a CTE column with a form naming the CTE, and both leave it false.
         *  `make_index` deduces the table an index is made for from its first argument, so an index
         *  that starts with anything else has to spell that table out.
         */
        bool emittedTableTypedColumnRef = false;

        /** User-defined / extension functions used in the current statement (deduplicated by struct name). */
        std::vector<CustomFunctionUse> customFunctions;

        /** Records a custom function use if its struct name is not already present. */
        void registerCustomFunction(CustomFunctionUse use);

        /** Records a form whose sqlite_orm type has no default constructor, once per spelling. */
        void recordFormWithoutDefaultConstructor(std::string form);

        /** Records a comment explaining a generated form. */
        void recordComment(std::string_view comment);

        /** The count `commentsRecordedSince` measures from: how many comments stand recorded now. */
        size_t commentMark() const;

        /**
         *  Copies out the distinct comments recorded past `commentMark()`, i.e. the ones whatever ran since
         *  recorded. This is how an entry point reports the comments of the node it was handed
         *  without taking the ones its statement had already recorded around it.
         */
        std::vector<std::string> commentsRecordedSince(size_t mark) const;

        /**
         *  Moves out the distinct comments recorded past `mark`, leaving what was recorded before it
         *  in place. This is how a statement generated from a generator that was handed something
         *  else first carries off its own comments only: the earlier ones stay with whoever marked
         *  before them, so no mark taken further out ever names a stretch that has been emptied
         *  under it.
         */
        std::vector<std::string> takeCommentsSince(size_t mark);

        /**
         *  Drops the comments recorded past `mark`, leaving the earlier ones in place. A generator
         *  that throws away what it generated — a fragment replaced by a placeholder, a statement
         *  that ends up with no code at all — marks before it starts and drops here: a comment
         *  explains the form some generated code took, and a consumer shown one for code it did not
         *  get reads it as a statement about the code it did.
         */
        void discardCommentsSince(size_t mark);

        /** Records the placeholder a funnelled `unsupportedPlaceholder…` is about to hand back. */
        void recordPlaceholder(PlaceholderSlot slot);

        /** The count the `placeheld…Since` readers measure from: how many stand recorded now. */
        size_t placeholderMark() const;

        /** Whether anything at all was placeheld past `mark`. */
        bool placeheldSince(size_t mark) const;

        /**
         *  Whether a placeholder standing in an expression slot was produced past `mark`, i.e.
         *  whether the code generated since then holds a comment where C++ expects an expression.
         */
        bool placeheldInExpressionSince(size_t mark) const;

        /**
         *  Whether a placeholder standing for a whole statement was produced past `mark`. Such a
         *  placeholder is a comment line of its own and compiles where a statement stands — and
         *  only there, so whoever is about to nest the code holding it inside something larger
         *  asks this first and gives the nesting up.
         */
        bool placeheldAsStatementSince(size_t mark) const;

        /**
         *  Drops what was placeheld past `mark`, leaving the earlier placeholders in place. A
         *  generator that throws away what it generated drops them along with the code that held
         *  them, exactly as it drops the comments recorded for it.
         */
        void discardPlaceholdersSince(size_t mark);

        /**
         *  How many statements of the current batch already declared each result variable
         *  (`rows`, `vtab`, …). `processMultiSql` carries it from one statement to the next so
         *  that every statement gets its own name.
         */
        std::map<std::string, int> batchVariableUses;
        /**
         *  Result variable names resolved for the statement being generated. Cached so that the
         *  main code and every regenerated alternative of one statement agree on the name.
         */
        std::map<std::string, std::string> statementVariableNames;

        /** `rows` for the first statement declaring it in the batch, then `rows2`, `rows3`, … */
        std::string statementVariableName(std::string_view baseName);

        bool useCpp20ColumnAliasStyle() const;
        bool useCpp20TableAliasStyle() const;
        bool withCteLegacyColalias() const;
        bool withCteCpp20Monikers() const;
        bool columnRefIsSelectAliasNoWrap(const ColumnRefNode& ref) const;
        bool isExplicitCteColumn(std::string_view cteKeyNorm, std::string_view columnName) const;

        void registerSourceTable(std::string_view tableName, std::vector<SourceTableColumn> columns);

        /** Records that `tableName` is left out of the generated storage. */
        void markUngeneratableTable(std::string_view tableName);

        /** Records that `viewName`, a view, is left out of the generated storage. */
        void markUngeneratableView(std::string_view viewName);

        /** Whether `tableName` names a table of this batch that is left out of the generated storage. */
        bool isUngeneratableTable(std::string_view tableName) const;

        /** `view` when every name the last statement referenced is a view, `table` otherwise. */
        std::string_view referencedUngeneratableKind() const;

        /** Struct name of a referenced table, recording the reference when the table is not generated. */
        std::string structNameForTable(std::string_view tableName);

        const SourceTableColumn* findSourceTableColumn(std::string_view tableName, std::string_view columnName) const;

        /** Best-effort C++ type for a custom-function argument: schema type when known, else the name heuristic. */
        std::string customFunctionArgType(const AstNode& argument) const;

        void registerColumn(const std::string& cppName, const std::string& cppType);
        void registerPrefixColumn(const std::string& cppName, const std::string& cppType);
        std::string syntheticColumnCppType(std::string_view cppIdentifier) const;
        std::string inferTypeFromNode(const AstNode& node) const;
        std::string generatePrefix() const;

        void resetForGeneration();
    };

}  // namespace sqlite2orm
