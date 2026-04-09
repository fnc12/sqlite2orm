#pragma once

#include <sqlite2orm/ast_expr.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sqlite2orm {

    struct SelectColumn {
        std::shared_ptr<AstNode> expression;
        std::string alias;

        bool operator==(const SelectColumn& other) const {
            if(this->alias != other.alias) return false;
            if(!this->expression && !other.expression) return true;
            if(!this->expression || !other.expression) return false;
            return *this->expression == *other.expression;
        }
    };

    struct FromTableClause {
        std::optional<std::string> schemaName;
        std::string tableName;
        std::optional<std::string> alias;
        /** When non-null, `FROM (SELECT ...)` derived table; `tableName` is unused. */
        std::shared_ptr<AstNode> derivedSelect;
        /** For table-valued functions: `FROM func(arg1, arg2)` */
        std::vector<std::shared_ptr<AstNode>> tableFunctionArgs;

        bool operator==(const FromTableClause& other) const {
            if(this->schemaName != other.schemaName || this->tableName != other.tableName ||
               this->alias != other.alias) {
                return false;
            }
            if(static_cast<bool>(this->derivedSelect) != static_cast<bool>(other.derivedSelect)) {
                return false;
            }
            if(this->derivedSelect && !(*this->derivedSelect == *other.derivedSelect)) {
                return false;
            }
            if(this->tableFunctionArgs.size() != other.tableFunctionArgs.size()) {
                return false;
            }
            for(size_t i = 0; i < this->tableFunctionArgs.size(); ++i) {
                auto& a = this->tableFunctionArgs[i];
                auto& b = other.tableFunctionArgs[i];
                if(!a && !b) continue;
                if(!a || !b) return false;
                if(*a != *b) return false;
            }
            return true;
        }
    };

    struct FromClauseItem {
        JoinKind leadingJoin = JoinKind::none;
        FromTableClause table;
        std::shared_ptr<AstNode> onExpression;
        std::vector<std::string> usingColumnNames;

        bool operator==(const FromClauseItem& other) const {
            if(this->leadingJoin != other.leadingJoin || this->table != other.table ||
               this->usingColumnNames != other.usingColumnNames)
                return false;
            if(!this->onExpression && !other.onExpression) return true;
            if(!this->onExpression || !other.onExpression) return false;
            return *this->onExpression == *other.onExpression;
        }
    };

    struct GroupByClause {
        std::vector<std::shared_ptr<AstNode>> expressions;
        std::shared_ptr<AstNode> having;

        bool operator==(const GroupByClause& other) const {
            if(this->expressions.size() != other.expressions.size()) return false;
            for(size_t i = 0; i < this->expressions.size(); ++i) {
                if(!this->expressions.at(i) && !other.expressions.at(i)) continue;
                if(!this->expressions.at(i) || !other.expressions.at(i)) return false;
                if(*this->expressions.at(i) != *other.expressions.at(i)) return false;
            }
            if(!this->having && !other.having) return true;
            if(!this->having || !other.having) return false;
            return *this->having == *other.having;
        }
    };

    /** Single CTE: `name [(col,...)] AS [MATERIALIZED|NOT MATERIALIZED] (subquery)` — `query` is the full SELECT. */
    struct CommonTableExpression {
        std::string cteName;
        std::vector<std::string> columnNames;
        CteMaterialization materialization = CteMaterialization::none;
        AstNodePointer query;

        bool operator==(const CommonTableExpression& o) const {
            return cteName == o.cteName && columnNames == o.columnNames && materialization == o.materialization &&
                   astNodesEqual(query, o.query);
        }
    };

    struct WithClause {
        bool recursive = false;
        std::vector<CommonTableExpression> tables;

        bool operator==(const WithClause& o) const = default;
    };

    /** `WITH [RECURSIVE] …` followed by SELECT / INSERT / REPLACE INTO / UPDATE / DELETE. */
    struct WithQueryNode : AstNode {
        WithClause clause;
        AstNodePointer statement;

        WithQueryNode(WithClause clause, AstNodePointer statement, SourceLocation location)
            : AstNode(location), clause(std::move(clause)), statement(std::move(statement)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const WithQueryNode*>(&other);
            return o && clause == o->clause && astNodesEqual(statement, o->statement);
        }
    };

    struct CompoundSelectNode : AstNode {
        std::vector<AstNodePointer> selects;
        std::vector<CompoundSelectOperator> operators;

        CompoundSelectNode(std::vector<AstNodePointer> selects, std::vector<CompoundSelectOperator> operators,
                           SourceLocation location)
            : AstNode(location), selects(std::move(selects)), operators(std::move(operators)) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const CompoundSelectNode*>(&other);
            if(!o || this->operators != o->operators || this->selects.size() != o->selects.size()) {
                return false;
            }
            for(size_t i = 0; i < this->selects.size(); ++i) {
                if(!astNodesEqual(this->selects.at(i), o->selects.at(i))) {
                    return false;
                }
            }
            return true;
        }
    };

    struct SelectNode : AstNode {
        bool distinct = false;
        bool selectAll = false;
        std::vector<SelectColumn> columns;
        std::vector<FromClauseItem> fromClause;
        std::shared_ptr<AstNode> whereClause;
        std::vector<OrderByTerm> orderBy;
        std::optional<GroupByClause> groupBy;
        std::vector<NamedWindowDefinition> namedWindows;
        int limitValue = -1;
        int offsetValue = -1;

        SelectNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const SelectNode*>(&other);
            if(!o) return false;
            if(this->distinct != o->distinct || this->selectAll != o->selectAll ||
               this->fromClause != o->fromClause || this->columns != o->columns ||
               this->orderBy != o->orderBy || this->groupBy != o->groupBy ||
               this->namedWindows != o->namedWindows || this->limitValue != o->limitValue ||
               this->offsetValue != o->offsetValue)
                return false;
            if(!this->whereClause && !o->whereClause) return true;
            if(!this->whereClause || !o->whereClause) return false;
            return *this->whereClause == *o->whereClause;
        }
    };

    struct UpdateAssignment {
        std::string columnName;
        AstNodePointer value;

        bool operator==(const UpdateAssignment& other) const {
            return this->columnName == other.columnName && astNodesEqual(this->value, other.value);
        }
    };

    struct ReturningColumn {
        AstNodePointer expression;
        std::string alias;

        bool operator==(const ReturningColumn& other) const {
            return alias == other.alias && astNodesEqual(expression, other.expression);
        }
    };

    struct InsertNode : AstNode {
        bool replaceInto = false;
        ConflictClause orConflict = ConflictClause::none;
        std::optional<std::string> schemaName;
        std::string tableName;
        std::vector<std::string> columnNames;
        InsertDataKind dataKind = InsertDataKind::values;
        std::vector<std::vector<AstNodePointer>> valueRows;
        AstNodePointer selectStatement;

        bool hasUpsertClause = false;
        std::vector<std::string> upsertConflictColumns;
        AstNodePointer upsertConflictWhere;
        InsertUpsertAction upsertAction = InsertUpsertAction::none;
        std::vector<UpdateAssignment> upsertUpdateAssignments;
        AstNodePointer upsertUpdateWhere;

        std::vector<ReturningColumn> returning;

        InsertNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const InsertNode*>(&other);
            if(!o || this->replaceInto != o->replaceInto || this->orConflict != o->orConflict ||
               this->schemaName != o->schemaName || this->tableName != o->tableName ||
               this->columnNames != o->columnNames || this->dataKind != o->dataKind ||
               this->hasUpsertClause != o->hasUpsertClause ||
               this->upsertConflictColumns != o->upsertConflictColumns ||
               this->upsertAction != o->upsertAction ||
               this->upsertUpdateAssignments != o->upsertUpdateAssignments ||
               this->returning != o->returning) {
                return false;
            }
            if(this->valueRows.size() != o->valueRows.size()) return false;
            for(size_t i = 0; i < this->valueRows.size(); ++i) {
                const auto& a = this->valueRows[i];
                const auto& b = o->valueRows[i];
                if(a.size() != b.size()) return false;
                for(size_t j = 0; j < a.size(); ++j) {
                    if(!astNodesEqual(a[j], b[j])) return false;
                }
            }
            if(!astNodesEqual(this->selectStatement, o->selectStatement)) return false;
            if(!astNodesEqual(this->upsertConflictWhere, o->upsertConflictWhere)) return false;
            return astNodesEqual(this->upsertUpdateWhere, o->upsertUpdateWhere);
        }
    };

    struct UpdateNode : AstNode {
        ConflictClause orConflict = ConflictClause::none;
        std::optional<std::string> schemaName;
        std::string tableName;
        std::vector<UpdateAssignment> assignments;
        std::vector<FromClauseItem> fromClause;
        AstNodePointer whereClause;
        std::vector<ReturningColumn> returning;

        UpdateNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const UpdateNode*>(&other);
            if(!o || this->orConflict != o->orConflict || this->schemaName != o->schemaName ||
               this->tableName != o->tableName || this->assignments != o->assignments ||
               this->fromClause != o->fromClause || this->returning != o->returning) {
                return false;
            }
            return astNodesEqual(this->whereClause, o->whereClause);
        }
    };

    struct DeleteNode : AstNode {
        std::optional<std::string> schemaName;
        std::string tableName;
        AstNodePointer whereClause;
        std::vector<ReturningColumn> returning;

        DeleteNode(SourceLocation location) : AstNode(location) {}

        bool operator==(const AstNode& other) const override {
            auto* o = dynamic_cast<const DeleteNode*>(&other);
            if(!o || this->schemaName != o->schemaName || this->tableName != o->tableName ||
               this->returning != o->returning)
                return false;
            return astNodesEqual(this->whereClause, o->whereClause);
        }
    };

}  // namespace sqlite2orm
