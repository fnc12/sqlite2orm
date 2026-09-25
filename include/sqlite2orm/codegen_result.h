#pragma once

#include <sqlite2orm/token.h>

#include <optional>
#include <string>
#include <vector>

namespace sqlite2orm {

    /**
     *  A codegen warning, optionally anchored to a span of the source SQL so a consumer can
     *  underline the relevant text. Implicitly constructible from a string, so the many plain
     *  `warnings.push_back("…")` sites keep compiling; only sites with a known location set one.
     *  Equality covers the span as well as the message, so a test that pins a warning pins what it
     *  underlines: anchoring a warning that was plain updates every expectation of it.
     */
    struct CodegenWarning {
        std::string message;
        /**
         *  Start of the relevant SQL token; `length` characters from here should be underlined.
         *  `column` counts characters, not bytes, so a consumer holding the SQL as text underlines
         *  from it directly — see `length`.
         */
        std::optional<SourceLocation> location;
        /**
         *  Number of characters to underline from `location` (0 when unknown). A character is one
         *  Unicode code point, whatever it takes to write in UTF-8: `«ü»` is three characters
         *  wherever it stands, both in this length and in the column of `location`, so SQL that is
         *  not all ASCII — SQLite takes non-ASCII in bare identifiers as readily as in quoted
         *  names and string literals — underlines the same text a consumer measuring characters
         *  expects. Whoever indexes the SQL by byte instead has to convert. So does a consumer
         *  counting UTF-16 code units — a browser string, a UTF-16 string type — but only above
         *  the basic multilingual plane: a code point written with a surrogate pair there, `🙂`
         *  among them, is one character here and two of those units.
         *
         *  The span stays on the line `location` names: a token written across lines — a quoted
         *  name or a string literal holding a newline, keywords split by one — is underlined up to
         *  the end of that line only, so a consumer drawing `length` characters from `location`
         *  within the line never runs past its end.
         */
        size_t length = 0;

        CodegenWarning() = default;
        CodegenWarning(std::string message) : message(std::move(message)) {}
        CodegenWarning(const char* message) : message(message) {}
        CodegenWarning(std::string message, SourceLocation location, size_t length) :
            message(std::move(message)), location(location), length(length) {}

        bool operator==(const CodegenWarning&) const = default;
    };

    /**
     *  Where a stretch of generated code came from: which statement generated it and what text of
     *  the SQL that statement is. A consumer holding both texts side by side highlights the one
     *  from the other with nothing but this — no parsing of the generated code, no guessing which
     *  line belongs to which statement.
     *
     *  Statement level: a span covers a whole fragment one statement generated, and a statement
     *  that generates two fragments standing apart — a CREATE TABLE gives both a struct and a
     *  `make_table(...)` argument — gets one span each. Spans of a text never overlap and stand in
     *  the order the code is written in; what they leave uncovered is what no statement generated,
     *  the punctuation the assembly writes itself among it: blank lines, a header's prologue, the
     *  `make_storage(...)` call the arguments sit in.
     *
     *  A list of these is statement level for good, and so is the promise that its spans never
     *  overlap: a finer map — down to the expressions inside a statement — comes as a list of its
     *  own and is never mixed into this one, so a consumer built on this list keeps working as is.
     */
    struct GeneratedCodeSpan {
        /**
         *  Start of this stretch in the generated code, counted in characters from its beginning.
         *  A character is one Unicode code point, the unit `CodegenWarning::length` counts in — SQL
         *  takes non-ASCII wherever it takes a name, and a name reaches the generated code as it
         *  is written, so a consumer measuring the code in characters indexes it directly.
         */
        size_t codeOffset = 0;
        /** Characters of generated code this stretch covers; never 0 for a recorded span. */
        size_t codeLength = 0;
        /**
         *  Which statement generated it, as an index into the statements the code was generated
         *  from — the results `joinGeneratedCodeWithSpans` was given, the `statements` of the
         *  schema a header was generated from. A statement generating nothing is simply named by
         *  no span, the way a statement dropped by codegen reports no hints either.
         */
        size_t statementIndex = 0;
        /**
         *  Start of that statement in the SQL it was parsed from, the statement's first character
         *  and not the keyword a diagnostic about it would point at. The SQL meant is the one that
         *  statement came from: a batch shares a single text, so locations across it are
         *  comparable, while a schema read from `sqlite_master` holds a text per statement and
         *  every location is in the text of its own.
         */
        SourceLocation sqlLocation;
        /**
         *  Characters of SQL the statement is written with, the terminating semicolon left out.
         *  Unlike `CodegenWarning::length` this is not cut off at the end of the line it starts on:
         *  a statement is written across as many lines as it takes, and a consumer highlighting it
         *  counts the newlines among these characters.
         */
        size_t sqlLength = 0;

        bool operator==(const GeneratedCodeSpan&) const = default;
    };

    /**
     *  A codegen hint — what form a piece of the snippet was generated as and why — optionally
     *  anchored to a span of the source SQL the way a `CodegenWarning` is, so a consumer underlines
     *  the SQL a hint explains with the machinery it already underlines warnings with. Anchor and
     *  length mean exactly what they do there, units included: see `CodegenWarning::location` and
     *  `CodegenWarning::length`.
     *  Implicitly constructible from a string, so a site that records a hint about something no AST
     *  node stands for keeps reading as it did; only sites with a node set an anchor.
     *  Equality covers the span as well as the message, so a test that pins a hint pins what it
     *  underlines: anchoring a hint that was plain updates every expectation of it.
     */
    struct CodegenComment {
        std::string message;
        /** Start of the SQL the hint explains; `length` characters from here are underlined. */
        std::optional<SourceLocation> location;
        /** Characters to underline from `location` (0 when unknown), counted as `CodegenWarning::length` counts them. */
        size_t length = 0;

        CodegenComment() = default;
        CodegenComment(std::string message) : message(std::move(message)) {}
        CodegenComment(const char* message) : message(message) {}
        CodegenComment(std::string message, SourceLocation location, size_t length) :
            message(std::move(message)), location(location), length(length) {}

        bool operator==(const CodegenComment&) const = default;
    };

    struct Option {
        std::string value;
        std::string code;
        std::string description;
        bool hidden = false;
        /**
         *  Optional notes when this alternative is shown or chosen (e.g. build requirements); any
         *  consumer may show them, and underline the SQL of the ones that carry an anchor.
         */
        std::vector<CodegenComment> comments;
        /**
         *  Minimum C++ standard this variant compiles against (14 by default; 20 for options that
         *  rely on C++20 sqlite_orm features). Options requiring more than `CodeGenPolicy::targetCppStandard`
         *  are dropped before the result is returned, so a consumer never sees an unusable variant.
         */
        int minCppStandard = 14;

        bool operator==(const Option&) const = default;
    };

    struct DecisionPoint {
        int id = 0;
        std::string category;
        std::string chosenValue;
        std::string chosenCode;
        std::vector<Option> options;

        bool operator==(const DecisionPoint&) const = default;
    };

    struct CodeGenResult {
        std::string code;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        std::vector<std::string> errors;
        /**
         *  Optional hints explaining the forms the snippet was generated as, deduplicated by message
         *  the way warnings are — a form met twice is reported once, anchored at the first of them.
         *  Every entry point that generates from an AST node reports the ones recorded while it ran,
         *  so a whole statement carries the comments of every clause of its body and a single node
         *  carries its own. A hint explains generated code, so a fragment that is thrown away — a
         *  subquery replaced by a placeholder, a statement that ends up with no `code` at all —
         *  reports none of the ones its generation recorded.
         */
        std::vector<CodegenComment> comments;
        /**
         *  Which statement each stretch of `code` was generated from, in the order the code is
         *  written in. Filled by whoever assembles code from more than one statement — the schema
         *  header — because that is who knows the offsets; a single statement is generated by
         *  itself and the caller already holds the AST node it passed in, so nothing is recorded
         *  for it.
         */
        std::vector<GeneratedCodeSpan> spans;

        bool operator==(const CodeGenResult&) const = default;
    };

    struct CreateTableParts {
        std::string structDeclaration;
        std::string makeTableExpression;
        /**
         *  The `table_mapping_style` decision point, offered only when the policy targets C++26:
         *  `make_table` (the classical form) against `reflection` (an annotated struct mapped by
         *  `make_table<T>()`). Each option's `code` is the pair these parts hold — the struct
         *  declaration, a blank line and the make_table expression — so an option stands for the
         *  whole mapping of the table, not for one of the two halves.
         */
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        /**
         *  Optional hints for the generated table, from its CHECK, DEFAULT and generated-column
         *  expressions. Empty when `makeTableExpression` is: a table that is not merged into the
         *  storage is code the consumer never gets, so nothing is left for a hint to explain.
         */
        std::vector<CodegenComment> comments;
        /**
         *  Whether `structDeclaration` is the reflected form, i.e. whether it carries sqlite_orm
         *  annotations. Whoever places the declaration has to know: the names inside an annotation
         *  get unqualified lookup at the point of the struct, not inside whatever function uses the
         *  mapping, so such a struct needs sqlite_orm's names visible where it is written.
         */
        bool structIsReflected = false;
    };

    struct CreateViewParts {
        std::string structDeclaration;
        std::string makeViewExpression;
        std::vector<DecisionPoint> decisionPoints;
        std::vector<CodegenWarning> warnings;
        /**
         *  Optional hints for the generated view, from the expressions of its body. Empty when
         *  `makeViewExpression` is, for the same reason as the table's: a view that is not merged
         *  into the storage leaves no generated form for a hint to be about.
         */
        std::vector<CodegenComment> comments;
    };

}  // namespace sqlite2orm
