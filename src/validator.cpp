#include <sqlite2orm/validator.h>
#include <sqlite2orm/pragma_sqlite_orm.h>
#include <sqlite2orm/utils.h>

#include <unordered_set>

namespace sqlite2orm {

    namespace {
        const std::unordered_set<std::string>& knownFunctions() {
            static const std::unordered_set<std::string> functions = {
                "avg", "count", "group_concat", "max", "min", "sum", "total",
                "abs", "changes", "char", "coalesce", "glob", "hex", "ifnull", "iif",
                "instr", "last_insert_rowid", "length", "like", "likelihood", "likely",
                "lower", "ltrim", "nullif", "printf", "quote",
                "random", "randomblob", "replace", "round", "rtrim", "soundex",
                "substr", "substring", "total_changes", "trim", "typeof",
                "unicode", "unlikely", "upper", "zeroblob",
                "date", "time", "datetime", "julianday", "strftime",
                "acos", "acosh", "asin", "asinh", "atan", "atanh", "atan2",
                "ceil", "ceiling", "cos", "cosh", "degrees", "exp", "floor",
                "ln", "log", "log2", "log10", "mod", "pi", "pow", "power",
                "radians", "sign", "sin", "sinh", "sqrt", "tan", "tanh", "trunc",
                "json", "json_array", "json_array_length", "json_extract",
                "json_insert", "json_object", "json_patch", "json_remove",
                "json_replace", "json_set", "json_type", "json_valid", "json_quote",
                "json_group_array", "json_group_object", "json_each", "json_tree",
                "highlight", "match", "rank",
                "row_number", "dense_rank", "percent_rank", "cume_dist", "ntile",
                "lag", "lead", "first_value", "last_value", "nth_value",
            };
            return functions;
        }
    }

    std::vector<ValidationError> Validator::validate(const AstNode& astNode) {
        std::vector<ValidationError> errors;

        if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            if(unaryOp->unaryOperator == UnaryOperator::plus) {
                errors.push_back(ValidationError{
                    "unary plus (+expr) is not supported in sqlite_orm",
                    unaryOp->location,
                    "UnaryOperatorNode"
                });
            }
            auto operandErrors = validate(*unaryOp->operand);
            errors.insert(errors.end(), operandErrors.begin(), operandErrors.end());
        } else if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            if(binaryOp->binaryOperator == BinaryOperator::isOp ||
               binaryOp->binaryOperator == BinaryOperator::isNot) {
                errors.push_back(ValidationError{
                    "binary IS / IS NOT is not supported in sqlite_orm "
                    "(only is_null / is_not_null for NULL checks)",
                    binaryOp->location,
                    "BinaryOperatorNode"
                });
            }
            if(binaryOp->binaryOperator == BinaryOperator::isDistinctFrom ||
               binaryOp->binaryOperator == BinaryOperator::isNotDistinctFrom) {
                errors.push_back(ValidationError{
                    "IS [NOT] DISTINCT FROM is not supported in sqlite_orm",
                    binaryOp->location,
                    "BinaryOperatorNode"
                });
            }
            auto lhsErrors = validate(*binaryOp->lhs);
            errors.insert(errors.end(), lhsErrors.begin(), lhsErrors.end());
            auto rhsErrors = validate(*binaryOp->rhs);
            errors.insert(errors.end(), rhsErrors.begin(), rhsErrors.end());
        } else if(auto* node = dynamic_cast<const IsNullNode*>(&astNode)) {
            auto e = validate(*node->operand);
            errors.insert(errors.end(), e.begin(), e.end());
        } else if(auto* node = dynamic_cast<const IsNotNullNode*>(&astNode)) {
            auto e = validate(*node->operand);
            errors.insert(errors.end(), e.begin(), e.end());
        } else if(auto* node = dynamic_cast<const BetweenNode*>(&astNode)) {
            for(auto* child : {node->operand.get(), node->low.get(), node->high.get()}) {
                auto e = validate(*child);
                errors.insert(errors.end(), e.begin(), e.end());
            }
        } else if(auto* node = dynamic_cast<const InNode*>(&astNode)) {
            auto e = validate(*node->operand);
            errors.insert(errors.end(), e.begin(), e.end());
            if(!node->tableName.empty()) {
                std::string normalizedInTable = toLowerAscii(node->tableName);
                if(this->knownCteTableNames.find(normalizedInTable) == this->knownCteTableNames.end()) {
                    errors.push_back(ValidationError{
                        "IN table-name is not supported in sqlite_orm",
                        node->location,
                        "InNode"
                    });
                }
            } else if(node->subquerySelect) {
                auto se = validate(*node->subquerySelect);
                errors.insert(errors.end(), se.begin(), se.end());
            } else {
                for(auto& val : node->values) {
                    auto ve = validate(*val);
                    errors.insert(errors.end(), ve.begin(), ve.end());
                }
            }
        } else if(auto* node = dynamic_cast<const SubqueryNode*>(&astNode)) {
            auto e = validate(*node->select);
            errors.insert(errors.end(), e.begin(), e.end());
        } else if(auto* node = dynamic_cast<const ExistsNode*>(&astNode)) {
            auto e = validate(*node->select);
            errors.insert(errors.end(), e.begin(), e.end());
        } else if(auto* node = dynamic_cast<const LikeNode*>(&astNode)) {
            auto e = validate(*node->operand);
            errors.insert(errors.end(), e.begin(), e.end());
            auto pe = validate(*node->pattern);
            errors.insert(errors.end(), pe.begin(), pe.end());
            if(node->escape) {
                auto ee = validate(*node->escape);
                errors.insert(errors.end(), ee.begin(), ee.end());
            }
        } else if(auto* node = dynamic_cast<const GlobNode*>(&astNode)) {
            auto e = validate(*node->operand);
            errors.insert(errors.end(), e.begin(), e.end());
            auto pe = validate(*node->pattern);
            errors.insert(errors.end(), pe.begin(), pe.end());
        } else if(auto* node = dynamic_cast<const CastNode*>(&astNode)) {
            auto e = validate(*node->operand);
            errors.insert(errors.end(), e.begin(), e.end());
        } else if(auto* node = dynamic_cast<const CaseNode*>(&astNode)) {
            if(node->operand) {
                auto e = validate(*node->operand);
                errors.insert(errors.end(), e.begin(), e.end());
            }
            for(auto& branch : node->branches) {
                auto ce = validate(*branch.condition);
                errors.insert(errors.end(), ce.begin(), ce.end());
                auto re = validate(*branch.result);
                errors.insert(errors.end(), re.begin(), re.end());
            }
            if(node->elseResult) {
                auto ee = validate(*node->elseResult);
                errors.insert(errors.end(), ee.begin(), ee.end());
            }
        } else if(auto* bindParam = dynamic_cast<const BindParameterNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "bind parameter " + std::string(bindParam->value) +
                " is not supported — use C++ variables instead",
                bindParam->location,
                "BindParameterNode"
            });
        } else if(auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            auto operandErrors = validate(*collateNode->operand);
            errors.insert(errors.end(), operandErrors.begin(), operandErrors.end());
        } else if(auto* func = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            std::string lowerName = toLowerAscii(func->name);
            if(knownFunctions().count(lowerName) == 0) {
                errors.push_back(ValidationError{
                    "unknown function: " + func->name,
                    func->location,
                    "FunctionCallNode"
                });
            }
            for(auto& arg : func->arguments) {
                auto argErrors = validate(*arg);
                errors.insert(errors.end(), argErrors.begin(), argErrors.end());
            }
            if(func->filterWhere) {
                auto fe = validate(*func->filterWhere);
                errors.insert(errors.end(), fe.begin(), fe.end());
            }
            if(func->over) {
                for(const auto& part : func->over->partitionBy) {
                    auto pe = validate(*part);
                    errors.insert(errors.end(), pe.begin(), pe.end());
                }
                for(const auto& ob : func->over->orderBy) {
                    if(ob.expression) {
                        auto oe = validate(*ob.expression);
                        errors.insert(errors.end(), oe.begin(), oe.end());
                    }
                }
                if(func->over->frame) {
                    const auto& fr = *func->over->frame;
                    if(fr.start.expr) {
                        auto se = validate(*fr.start.expr);
                        errors.insert(errors.end(), se.begin(), se.end());
                    }
                    if(fr.end.expr) {
                        auto ee = validate(*fr.end.expr);
                        errors.insert(errors.end(), ee.begin(), ee.end());
                    }
                }
            }
        } else if(auto* createTable = dynamic_cast<const CreateTableNode*>(&astNode)) {
            if(createTable->columns.empty()) {
                errors.push_back(ValidationError{
                    "CREATE TABLE must have at least one column",
                    createTable->location,
                    "CreateTableNode"
                });
            }
            std::unordered_set<std::string> seen;
            for(const auto& col : createTable->columns) {
                auto lower = toLowerAscii(col.name);
                if(!seen.insert(lower).second) {
                    errors.push_back(ValidationError{
                        "duplicate column name: " + col.name,
                        createTable->location,
                        "CreateTableNode"
                    });
                }
                if(col.autoincrement && !col.primaryKey) {
                    errors.push_back(ValidationError{
                        "AUTOINCREMENT requires PRIMARY KEY on column: " + col.name,
                        createTable->location,
                        "CreateTableNode"
                    });
                }
            }
            if(createTable->strict) {
                errors.push_back(ValidationError{
                    "STRICT tables are not supported in sqlite_orm",
                    createTable->location,
                    "CreateTableNode"
                });
            }
        } else if(auto* compoundNode = dynamic_cast<const CompoundSelectNode*>(&astNode)) {
            for(const auto& childSelect : compoundNode->selects) {
                auto childErrors = validate(*childSelect);
                errors.insert(errors.end(), childErrors.begin(), childErrors.end());
            }
        } else if(auto* withNode = dynamic_cast<const WithQueryNode*>(&astNode)) {
            for(const auto& cte : withNode->clause.tables) {
                this->knownCteTableNames.insert(toLowerAscii(cte.cteName));
            }
            for(const auto& cte : withNode->clause.tables) {
                auto cteErrors = validate(*cte.query);
                errors.insert(errors.end(), cteErrors.begin(), cteErrors.end());
            }
            auto statementErrors = validate(*withNode->statement);
            errors.insert(errors.end(), statementErrors.begin(), statementErrors.end());
        } else if(auto* selectNode = dynamic_cast<const SelectNode*>(&astNode)) {
            for(const auto& fromItem : selectNode->fromClause) {
                if(fromItem.table.derivedSelect) {
                    errors.push_back(ValidationError{
                        "subselect in FROM is not supported in sqlite_orm",
                        fromItem.table.derivedSelect->location,
                        "SelectNode"
                    });
                    auto derivedErrors = validate(*fromItem.table.derivedSelect);
                    errors.insert(errors.end(), derivedErrors.begin(), derivedErrors.end());
                }
                if(!fromItem.table.tableFunctionArgs.empty()) {
                    errors.push_back(ValidationError{
                        "table-valued function in FROM is not supported in sqlite_orm codegen",
                        selectNode->location,
                        "SelectNode"
                    });
                    for(const auto& arg : fromItem.table.tableFunctionArgs) {
                        if(arg) {
                            auto argErrors = validate(*arg);
                            errors.insert(errors.end(), argErrors.begin(), argErrors.end());
                        }
                    }
                }
                if(fromItem.leadingJoin == JoinKind::naturalLeftJoin) {
                    errors.push_back(ValidationError{
                        "NATURAL LEFT JOIN is not supported in sqlite_orm",
                        selectNode->location,
                        "SelectNode"
                    });
                }
                const bool needsOnOrUsing = fromItem.leadingJoin == JoinKind::innerJoin ||
                    fromItem.leadingJoin == JoinKind::joinPlain || fromItem.leadingJoin == JoinKind::leftJoin ||
                    fromItem.leadingJoin == JoinKind::leftOuterJoin;
                if(needsOnOrUsing && !fromItem.onExpression && fromItem.usingColumnNames.empty()) {
                    errors.push_back(ValidationError{
                        "JOIN requires ON or USING clause",
                        selectNode->location,
                        "SelectNode"
                    });
                }
                if(fromItem.onExpression) {
                    auto onErrors = validate(*fromItem.onExpression);
                    errors.insert(errors.end(), onErrors.begin(), onErrors.end());
                }
            }
            for(const auto& col : selectNode->columns) {
                if(col.expression) {
                    auto colErrors = validate(*col.expression);
                    errors.insert(errors.end(), colErrors.begin(), colErrors.end());
                }
            }
            if(selectNode->whereClause) {
                auto wErrors = validate(*selectNode->whereClause);
                errors.insert(errors.end(), wErrors.begin(), wErrors.end());
            }
            if(selectNode->groupBy) {
                for(const auto& expr : selectNode->groupBy->expressions) {
                    auto gErrors = validate(*expr);
                    errors.insert(errors.end(), gErrors.begin(), gErrors.end());
                }
                if(selectNode->groupBy->having) {
                    auto hErrors = validate(*selectNode->groupBy->having);
                    errors.insert(errors.end(), hErrors.begin(), hErrors.end());
                }
            }
            for(const auto& namedWindow : selectNode->namedWindows) {
                if(!namedWindow.definition) {
                    continue;
                }
                const auto& windowDef = *namedWindow.definition;
                for(const auto& partExpr : windowDef.partitionBy) {
                    auto partErrors = validate(*partExpr);
                    errors.insert(errors.end(), partErrors.begin(), partErrors.end());
                }
                for(const auto& orderTerm : windowDef.orderBy) {
                    if(orderTerm.expression) {
                        auto orderErrors = validate(*orderTerm.expression);
                        errors.insert(errors.end(), orderErrors.begin(), orderErrors.end());
                    }
                }
                if(windowDef.frame) {
                    const auto& frameSpec = *windowDef.frame;
                    if(frameSpec.start.expr) {
                        auto startErrors = validate(*frameSpec.start.expr);
                        errors.insert(errors.end(), startErrors.begin(), startErrors.end());
                    }
                    if(frameSpec.end.expr) {
                        auto endErrors = validate(*frameSpec.end.expr);
                        errors.insert(errors.end(), endErrors.begin(), endErrors.end());
                    }
                }
            }
            for(const auto& term : selectNode->orderBy) {
                auto oErrors = validate(*term.expression);
                errors.insert(errors.end(), oErrors.begin(), oErrors.end());
            }
        } else if(auto* insertNode = dynamic_cast<const InsertNode*>(&astNode)) {
            for(const auto& row : insertNode->valueRows) {
                for(const auto& cell : row) {
                    auto ve = validate(*cell);
                    errors.insert(errors.end(), ve.begin(), ve.end());
                }
            }
            if(insertNode->selectStatement) {
                auto se = validate(*insertNode->selectStatement);
                errors.insert(errors.end(), se.begin(), se.end());
            }
            if(insertNode->upsertConflictWhere) {
                auto ue = validate(*insertNode->upsertConflictWhere);
                errors.insert(errors.end(), ue.begin(), ue.end());
            }
            for(const auto& as : insertNode->upsertUpdateAssignments) {
                auto ve = validate(*as.value);
                errors.insert(errors.end(), ve.begin(), ve.end());
            }
            if(insertNode->upsertUpdateWhere) {
                auto we = validate(*insertNode->upsertUpdateWhere);
                errors.insert(errors.end(), we.begin(), we.end());
            }
        } else if(auto* updateNode = dynamic_cast<const UpdateNode*>(&astNode)) {
            if(updateNode->assignments.empty()) {
                errors.push_back(ValidationError{"UPDATE requires at least one SET assignment", updateNode->location,
                                                 "UpdateNode"});
            }
            for(const auto& as : updateNode->assignments) {
                auto ve = validate(*as.value);
                errors.insert(errors.end(), ve.begin(), ve.end());
            }
            if(!updateNode->fromClause.empty()) {
                errors.push_back(ValidationError{
                    "UPDATE ... FROM ... is not supported in sqlite_orm",
                    updateNode->location,
                    "UpdateNode"
                });
            }
            if(updateNode->whereClause) {
                auto we = validate(*updateNode->whereClause);
                errors.insert(errors.end(), we.begin(), we.end());
            }
        } else if(auto* deleteNode = dynamic_cast<const DeleteNode*>(&astNode)) {
            if(deleteNode->whereClause) {
                auto we = validate(*deleteNode->whereClause);
                errors.insert(errors.end(), we.begin(), we.end());
            }
        } else if(auto* raiseNode = dynamic_cast<const RaiseNode*>(&astNode)) {
            if(raiseNode->message) {
                auto me = validate(*raiseNode->message);
                errors.insert(errors.end(), me.begin(), me.end());
            } else if(raiseNode->kind != RaiseKind::ignore) {
                errors.push_back(ValidationError{
                    "RAISE(ROLLBACK|ABORT|FAIL) requires a message expression",
                    raiseNode->location,
                    "RaiseNode"});
            }
        } else if(auto* createTrigger = dynamic_cast<const CreateTriggerNode*>(&astNode)) {
            if(createTrigger->bodyStatements.empty()) {
                errors.push_back(ValidationError{
                    "CREATE TRIGGER body must contain at least one statement",
                    createTrigger->location,
                    "CreateTriggerNode"});
            }
            if(createTrigger->whenClause) {
                auto we = validate(*createTrigger->whenClause);
                errors.insert(errors.end(), we.begin(), we.end());
            }
            for(const auto& st : createTrigger->bodyStatements) {
                auto se = validate(*st);
                errors.insert(errors.end(), se.begin(), se.end());
            }
        } else if(auto* createIndex = dynamic_cast<const CreateIndexNode*>(&astNode)) {
            if(createIndex->indexedColumns.empty()) {
                errors.push_back(ValidationError{
                    "CREATE INDEX requires at least one indexed column",
                    createIndex->location,
                    "CreateIndexNode"});
            }
            for(const auto& ic : createIndex->indexedColumns) {
                if(ic.expression) {
                    auto ie = validate(*ic.expression);
                    errors.insert(errors.end(), ie.begin(), ie.end());
                }
            }
            if(createIndex->whereClause) {
                auto we = validate(*createIndex->whereClause);
                errors.insert(errors.end(), we.begin(), we.end());
            }
        } else if(auto* cvt = dynamic_cast<const CreateVirtualTableNode*>(&astNode)) {
            for(const auto& p : cvt->moduleArguments) {
                auto ae = validate(*p);
                errors.insert(errors.end(), ae.begin(), ae.end());
            }
            std::string mod = toLowerAscii(cvt->moduleName);
            if(mod == "fts5" && cvt->moduleArguments.empty()) {
                errors.push_back(ValidationError{
                    "FTS5 virtual table requires at least one module argument (column)",
                    cvt->location,
                    "CreateVirtualTableNode"});
            }
            if(mod == "dbstat" && cvt->moduleArguments.size() > 1) {
                errors.push_back(ValidationError{
                    "dbstat virtual table accepts at most one optional schema argument",
                    cvt->location,
                    "CreateVirtualTableNode"});
            }
            if(mod == "rtree" || mod == "rtree_i32") {
                const size_t n = cvt->moduleArguments.size();
                if(n == 0) {
                    errors.push_back(ValidationError{
                        "RTREE virtual table requires module column arguments",
                        cvt->location,
                        "CreateVirtualTableNode"});
                } else {
                    bool allCol = true;
                    for(const auto& p : cvt->moduleArguments) {
                        if(!dynamic_cast<const ColumnRefNode*>(p.get())) {
                            allCol = false;
                            break;
                        }
                    }
                    if(allCol && (n < 3 || n > 11 || (n % 2 == 0))) {
                        errors.push_back(ValidationError{
                            "RTREE virtual table must declare 3, 5, 7, 9, or 11 column arguments",
                            cvt->location,
                            "CreateVirtualTableNode"});
                    }
                }
            }
        } else if(auto* alterTable = dynamic_cast<const AlterTableStatementNode*>(&astNode)) {
            std::string display = alterTable->tableName;
            if(alterTable->tableSchemaName) {
                display = *alterTable->tableSchemaName + "." + display;
            }
            errors.push_back(ValidationError{
                "ALTER TABLE " + display +
                    " is not mapped to standalone calls; in sqlite_orm schema alignment is done via "
                    "storage.sync_schema() from your make_storage(...) definition",
                alterTable->location,
                "AlterTableStatementNode"});
        } else if(auto* createView = dynamic_cast<const CreateViewNode*>(&astNode)) {
            std::string displayName;
            if(createView->viewSchemaName) {
                displayName = *createView->viewSchemaName;
                displayName += '.';
            }
            displayName += createView->viewName;
            errors.push_back(ValidationError{
                "CREATE VIEW " + displayName + " is not supported for sqlite_orm code generation yet",
                createView->location,
                "CreateViewNode"});
            if(createView->selectQuery) {
                auto se = validate(*createView->selectQuery);
                errors.insert(errors.end(), se.begin(), se.end());
            }
        } else if(auto* node = dynamic_cast<const SavepointNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "SAVEPOINT is not supported in sqlite_orm",
                node->location, "SavepointNode"});
        } else if(auto* node = dynamic_cast<const ReleaseNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "RELEASE is not supported in sqlite_orm",
                node->location, "ReleaseNode"});
        } else if(auto* node = dynamic_cast<const AttachDatabaseNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "ATTACH DATABASE is not supported in sqlite_orm",
                node->location, "AttachDatabaseNode"});
        } else if(auto* node = dynamic_cast<const DetachDatabaseNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "DETACH DATABASE is not supported in sqlite_orm",
                node->location, "DetachDatabaseNode"});
        } else if(auto* node = dynamic_cast<const AnalyzeNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "ANALYZE is not supported in sqlite_orm",
                node->location, "AnalyzeNode"});
        } else if(auto* node = dynamic_cast<const ReindexNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "REINDEX is not supported in sqlite_orm",
                node->location, "ReindexNode"});
        } else if(auto* node = dynamic_cast<const PragmaNode*>(&astNode)) {
            if(auto pragmaError = validatePragmaForSqliteOrm(*node)) {
                errors.push_back(ValidationError{std::move(*pragmaError), node->location, "PragmaNode"});
            }
            if(node->value) {
                auto valueErrors = validate(*node->value);
                errors.insert(errors.end(), valueErrors.begin(), valueErrors.end());
            }
        } else if(auto* node = dynamic_cast<const ExplainNode*>(&astNode)) {
            errors.push_back(ValidationError{
                "EXPLAIN is not supported in sqlite_orm",
                node->location, "ExplainNode"});
            if(node->statement) {
                auto se = validate(*node->statement);
                errors.insert(errors.end(), se.begin(), se.end());
            }
        }

        auto validateReturning = [&](const std::vector<ReturningColumn>& returning,
                                     SourceLocation location, const char* nodeType) {
            if(!returning.empty()) {
                errors.push_back(ValidationError{
                    "RETURNING clause is not supported in sqlite_orm",
                    location, nodeType});
            }
        };

        auto validateNullsOrdering = [&](const std::vector<OrderByTerm>& orderBy,
                                         SourceLocation location, const char* nodeType) {
            for(const auto& term : orderBy) {
                if(term.nulls != NullsOrdering::none) {
                    errors.push_back(ValidationError{
                        "NULLS FIRST / NULLS LAST is not supported in sqlite_orm",
                        location, nodeType});
                    break;
                }
            }
        };

        if(auto* insertNode = dynamic_cast<const InsertNode*>(&astNode)) {
            validateReturning(insertNode->returning, insertNode->location, "InsertNode");
        } else if(auto* updateNode = dynamic_cast<const UpdateNode*>(&astNode)) {
            validateReturning(updateNode->returning, updateNode->location, "UpdateNode");
        } else if(auto* deleteNode = dynamic_cast<const DeleteNode*>(&astNode)) {
            validateReturning(deleteNode->returning, deleteNode->location, "DeleteNode");
        }

        if(auto* selectNode = dynamic_cast<const SelectNode*>(&astNode)) {
            validateNullsOrdering(selectNode->orderBy, selectNode->location, "SelectNode");
        }

        return errors;
    }

}  // namespace sqlite2orm
