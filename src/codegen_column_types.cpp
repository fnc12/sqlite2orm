#include "codegen_utils.h"
#include "select_scope_columns.h"

#include <sqlite2orm/utils.h>

#include <utility>

namespace sqlite2orm {

    namespace {

        /** Whether `column` is one of the columns the PRIMARY KEY of `createTable` is over. */
        bool columnIsInPrimaryKey(const CreateTableNode& createTable, const ColumnDef& column) {
            if (column.primaryKey) {
                return true;
            }
            const std::string columnName = normalizeSqlName(column.name);
            for (const TablePrimaryKey& primaryKey: createTable.primaryKeys) {
                for (const KeyColumn& keyColumn: primaryKey.columns) {
                    if (normalizeSqlName(keyColumn.name) == columnName) {
                        return true;
                    }
                }
            }
            return false;
        }

        /**
         *  How many terms of the PRIMARY KEY of `createTable` name `column`. A rowid table keeps
         *  every term, so `PRIMARY KEY(a, a)` is a key of two columns there, while a WITHOUT ROWID
         *  table drops the repeats and keys on `a` alone (checked against sqlite3 3.51.0 through a
         *  foreign key naming no column of such a parent).
         */
        int primaryKeyTermsOfColumn(const CreateTableNode& createTable, const ColumnDef& column) {
            if (column.primaryKey) {
                return 1;
            }
            const std::string columnName = normalizeSqlName(column.name);
            int terms = 0;
            for (const TablePrimaryKey& primaryKey: createTable.primaryKeys) {
                for (const KeyColumn& keyColumn: primaryKey.columns) {
                    if (normalizeSqlName(keyColumn.name) == columnName) {
                        ++terms;
                    }
                }
            }
            if (createTable.withoutRowid && terms > 1) {
                return 1;
            }
            return terms;
        }

        /**
         *  Whether `column` is the rowid alias of `createTable` — the column SQLite stores the
         *  rowid itself in rather than beside. Everything about it is spelling: the declared type
         *  has to be INTEGER and nothing else (an `INT PRIMARY KEY` is an ordinary column with a
         *  rowid of its own behind it), the key has to be over this column alone, and a DESC
         *  spelled on the column takes the alias away while an ASC, or a DESC spelled in the
         *  table-level `PRIMARY KEY(x DESC)` form, leaves it. A WITHOUT ROWID table has no rowid
         *  to alias at all. Checked against sqlite3 3.51.0 through `PRAGMA table_info`.
         *
         *  The table-level half of the rule is `columnAloneInTableKeyIsRowidAlias`, which codegen
         *  asks about a key it is about to write with one column rather than about the written
         *  one; here it is asked about the key the table really spells, so a key of two terms —
         *  `PRIMARY KEY(a, a)` included — is no alias.
         */
        bool columnIsRowidAlias(const CreateTableNode& createTable, const ColumnDef& column) {
            if (column.primaryKey) {
                if (createTable.withoutRowid || normalizeSqlName(column.typeName) != "integer") {
                    return false;
                }
                size_t columnKeyCount = 0;
                for (const ColumnDef& other: createTable.columns) {
                    if (other.primaryKey) {
                        ++columnKeyCount;
                    }
                }
                // A table spelling a second key, on another column or on the table, is one SQLite
                // refuses outright — no column of it is the rowid.
                return columnKeyCount == 1 && createTable.primaryKeys.empty() &&
                       column.primaryKeySortDirection != SortDirection::desc;
            }
            if (!columnAloneInTableKeyIsRowidAlias(createTable, column)) {
                return false;
            }
            const TablePrimaryKey& primaryKey = createTable.primaryKeys.front();
            return primaryKey.columns.size() == 1 &&
                   normalizeSqlName(primaryKey.columns.front().name) == normalizeSqlName(column.name);
        }

        /**
         *  Whether `column` is declared `ANY`. SQLite takes the name in every quoting it takes an
         *  identifier in — `"ANY"`, `[ANY]`, `` `ANY` `` and `'ANY'` are all accepted in a STRICT
         *  table — and in any case, while `ANY(10)` and `COMPANY` are refused there, so the name
         *  is matched whole rather than as the affinity rule matches its substrings.
         */
        bool columnTypeIsAny(const ColumnDef& column) {
            return normalizeSqlName(column.typeName) == "any";
        }

    }  // namespace

    bool columnAloneInTableKeyIsRowidAlias(const CreateTableNode& createTable, const ColumnDef& column) {
        if (createTable.withoutRowid) {
            return false;
        }
        if (normalizeSqlName(column.typeName) != "integer") {
            return false;
        }
        for (const ColumnDef& other: createTable.columns) {
            if (other.primaryKey) {
                return false;
            }
        }
        return createTable.primaryKeys.size() == 1;
    }

    std::optional<CodegenWarning> mappedTypeRowidAliasWarning(const CreateTableNode& createTable,
                                                              const ColumnDef& column) {
        if (createTable.withoutRowid || normalizeSqlName(column.typeName) == "integer") {
            return std::nullopt;
        }
        const std::string cppType = sqliteColumnTypeToCpp(createTable, column);
        if (cppType != "int64_t" && cppType != "bool") {
            return std::nullopt;
        }
        const std::string columnName = stripIdentifierQuotes(column.name);
        std::string message =
            "column '" + columnName + "' is declared " + column.typeName +
            " and is the only column of the PRIMARY KEY, and SQLite makes such a column the rowid alias only when its "
            "declared type is INTEGER exactly, so here it is an ordinary column. The generated member is " +
            cppType +
            ", which sqlite_orm declares INTEGER, so in a database created from the generated code the same key makes "
            "'" +
            columnName +
            "' the rowid alias. A database the header was generated from is unaffected — `sync_schema()` leaves it "
            "alone — but in a database created from the generated code an INSERT that leaves '" +
            columnName +
            "' out stores the next rowid in it instead of NULL, a NOT NULL that SQLite enforces on the column here, "
            "whether declared or implied by STRICT, lets that INSERT through rather than refusing it, and a value "
            "that is not an integer is refused (\"datatype mismatch\") rather than stored as it came";
        if (!column.typeNameLocation) {
            return CodegenWarning{std::move(message)};
        }
        return CodegenWarning{std::move(message), *column.typeNameLocation, underlineLengthOf(column.typeName)};
    }

    std::string sqliteColumnTypeToCpp(const CreateTableNode& createTable, const ColumnDef& column) {
        if (createTable.strict && columnTypeIsAny(column)) {
            return "std::vector<char>";
        }
        return column.typeName.empty() ? "std::vector<char>" : sqliteTypeToCpp(column.typeName);
    }

    std::optional<CodegenWarning> anyColumnTypeWarning(const CreateTableNode& createTable, const ColumnDef& column) {
        if (!columnTypeIsAny(column)) {
            return std::nullopt;
        }
        const std::string columnName = stripIdentifierQuotes(column.name);
        std::string message = "column `" + columnName + "` is declared ANY";
        if (createTable.strict) {
            message += ", which in a STRICT table holds a value of any storage class and stores it as it came. "
                       "sqlite_orm has no type that carries a storage class, so the column is mapped to "
                       "std::vector<char>, whose extractor reads every one of them rather than zeroing the ones it "
                       "cannot use: a stored INTEGER or REAL comes back as the bytes of the text SQLite renders it "
                       "as — a REAL keeps 15 significant digits that way, so 1.0/3 reads back as `0.333333333333333` "
                       "— and a value written back through the member is stored as a BLOB, whatever it was before";
        } else {
            message += ", which is a datatype of STRICT tables only: this table is not STRICT, so SQLite reads ANY as "
                       "a type name it does not know and gives the column NUMERIC affinity, which is why the column "
                       "is mapped to double. Affinity is not a constraint — text NUMERIC affinity cannot convert is "
                       "still stored as text, and `'x'` in this column reads back through the member as 0";
        }
        if (!column.typeNameLocation) {
            return CodegenWarning{std::move(message)};
        }
        return CodegenWarning{std::move(message), *column.typeNameLocation, underlineLengthOf(column.typeName)};
    }

    bool columnMemberIsNullable(const CreateTableNode& createTable, const ColumnDef& column) {
        if (column.notNull) {
            return false;
        }
        if (!createTable.withoutRowid && !createTable.strict) {
            return true;
        }
        if (!columnIsInPrimaryKey(createTable, column)) {
            return true;
        }
        // A WITHOUT ROWID table makes every column of its key NOT NULL. A STRICT table does the
        // same, except for the rowid alias: there SQLite keeps turning an inserted NULL into the
        // next rowid, and reports `notnull = 0`.
        return createTable.strict && columnIsRowidAlias(createTable, column);
    }

    std::vector<SourceTableColumn> sourceTableColumnsFromCreateTable(const CreateTableNode& createTable) {
        std::vector<SourceTableColumn> columns;
        for (const ColumnDef& column: createTable.columns) {
            const auto cppType = sqliteColumnTypeToCpp(createTable, column);
            const bool nullable = columnMemberIsNullable(createTable, column);
            // The expression is what makes a column generated; `generatedStorage` only tells
            // VIRTUAL from STORED, and stays `none` for the bare `AS (...)` spelling SQLite
            // documents as the default.
            const bool generated = column.generatedExpression != nullptr;
            columns.push_back(SourceTableColumn{stripIdentifierQuotes(column.name),
                                                cppType,
                                                nullable,
                                                generated,
                                                primaryKeyTermsOfColumn(createTable, column)});
        }
        return columns;
    }

}  // namespace sqlite2orm
