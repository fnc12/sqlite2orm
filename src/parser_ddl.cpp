#include "parser_ddl.h"
#include <sqlite2orm/parser.h>
#include <sqlite2orm/utils.h>

namespace sqlite2orm {

    DdlParser::DdlParser(Parser& parser, TokenStream& tokenStream)
        : parser(parser), tokenStream(tokenStream) {}

    std::string DdlParser::parseColumnTypeName() {
        std::string typeName;
        int parenthesisDepth = 0;
        while(!atEnd()) {
            if(parenthesisDepth == 0) {
                if(check(TokenType::comma) || check(TokenType::rightParen)) break;
                if(check(TokenType::kwPrimary) || check(TokenType::kwNot) ||
                   check(TokenType::kwUnique) || check(TokenType::kwCheck) ||
                   check(TokenType::kwDefault) || check(TokenType::kwCollate) ||
                   check(TokenType::kwReferences) || check(TokenType::kwGenerated) ||
                   check(TokenType::kwConstraint) || check(TokenType::kwAutoincrement) ||
                   check(TokenType::kwAs)) break;
            }

            if(!typeName.empty()) {
                char last = typeName.back();
                bool currentIsSpecial = check(TokenType::leftParen) ||
                                         check(TokenType::rightParen) ||
                                         check(TokenType::comma);
                if(last != '(' && !currentIsSpecial) {
                    typeName += " ";
                }
            }

            if(check(TokenType::leftParen)) parenthesisDepth++;
            else if(check(TokenType::rightParen)) parenthesisDepth--;

            typeName += std::string(current().value);
            advanceToken();
        }
        return typeName;
    }

    ForeignKeyAction DdlParser::parseForeignKeyAction() {
        if(match(TokenType::kwCascade)) return ForeignKeyAction::cascade;
        if(match(TokenType::kwRestrict)) return ForeignKeyAction::restrict_;
        if(check(TokenType::kwSet) && peekToken(1).type == TokenType::kwNull) {
            advanceToken();
            advanceToken();
            return ForeignKeyAction::setNull;
        }
        if(check(TokenType::kwSet) && peekToken(1).type == TokenType::kwDefault) {
            advanceToken();
            advanceToken();
            return ForeignKeyAction::setDefault;
        }
        if(check(TokenType::kwNo) && peekToken(1).type == TokenType::kwAction) {
            advanceToken();
            advanceToken();
            return ForeignKeyAction::noAction;
        }
        return ForeignKeyAction::none;
    }

    ForeignKeyClause DdlParser::parseForeignKeyClause() {
        ForeignKeyClause fk;
        if(atEnd()) return fk;
        fk.table = std::string(current().value);
        advanceToken();
        if(match(TokenType::leftParen)) {
            if(!atEnd()) {
                fk.column = std::string(current().value);
                advanceToken();
            }
            match(TokenType::rightParen);
        }
        while(check(TokenType::kwOn) && !atEnd()) {
            advanceToken();
            if(match(TokenType::kwDelete)) {
                fk.onDelete = parseForeignKeyAction();
            } else if(match(TokenType::kwUpdate)) {
                fk.onUpdate = parseForeignKeyAction();
            } else {
                break;
            }
        }
        if(check(TokenType::kwNot) && peekToken(1).type == TokenType::kwDeferrable) {
            advanceToken();
            advanceToken();
            fk.deferrability = Deferrability::notDeferrable;
        } else if(match(TokenType::kwDeferrable)) {
            fk.deferrability = Deferrability::deferrable;
        }
        if(match(TokenType::kwInitially)) {
            if(match(TokenType::kwDeferred)) {
                fk.initially = InitialConstraintMode::deferred;
            } else if(match(TokenType::kwImmediate)) {
                fk.initially = InitialConstraintMode::immediate;
            }
        }
        return fk;
    }

    ConflictClause DdlParser::parseConflictClause() {
        if(!check(TokenType::kwOn)) return ConflictClause::none;
        advanceToken();
        if(!match(TokenType::kwConflict)) return ConflictClause::none;
        if(match(TokenType::kwRollback)) return ConflictClause::rollback;
        if(match(TokenType::kwAbort)) return ConflictClause::abort;
        if(match(TokenType::kwFail)) return ConflictClause::fail;
        if(match(TokenType::kwIgnore)) return ConflictClause::ignore;
        if(match(TokenType::kwReplace)) return ConflictClause::replace;
        return ConflictClause::none;
    }

    AstNodePointer DdlParser::parseDefaultValue() {
        if(match(TokenType::leftParen)) {
            auto expr = this->parser.parseExpression();
            match(TokenType::rightParen);
            return expr;
        }
        if(check(TokenType::plus) || check(TokenType::minus)) {
            auto op = check(TokenType::minus)
                ? UnaryOperator::minus : UnaryOperator::plus;
            auto location = current().location;
            advanceToken();
            auto operand = this->parser.parsePrimary();
            return std::make_unique<UnaryOperatorNode>(op, std::move(operand), location);
        }
        return this->parser.parsePrimary();
    }

    void DdlParser::parseColumnConstraints(ColumnDef& columnDef) {
        while(!check(TokenType::comma) && !check(TokenType::rightParen) && !atEnd()) {
            if(match(TokenType::kwConstraint)) {
                if(isColumnNameToken()) advanceToken();
                continue;
            }
            if(check(TokenType::kwPrimary)) {
                advanceToken();
                match(TokenType::kwKey);
                columnDef.primaryKey = true;
                if(check(TokenType::kwAsc) || check(TokenType::kwDesc)) advanceToken();
                columnDef.primaryKeyConflict = parseConflictClause();
                if(match(TokenType::kwAutoincrement)) {
                    columnDef.autoincrement = true;
                }
            } else if(check(TokenType::kwNot) && peekToken(1).type == TokenType::kwNull) {
                advanceToken();
                advanceToken();
                columnDef.notNull = true;
                if(check(TokenType::kwOn)) {
                    advanceToken();
                    if(match(TokenType::kwConflict)) advanceToken();
                }
            } else if(check(TokenType::kwDefault)) {
                advanceToken();
                columnDef.defaultValue = parseDefaultValue();
            } else if(check(TokenType::kwUnique)) {
                advanceToken();
                columnDef.unique = true;
                columnDef.uniqueConflict = parseConflictClause();
            } else if(check(TokenType::kwCheck)) {
                advanceToken();
                if(match(TokenType::leftParen)) {
                    columnDef.checkExpression = this->parser.parseExpression();
                    match(TokenType::rightParen);
                }
            } else if(check(TokenType::kwReferences)) {
                advanceToken();
                columnDef.foreignKey = parseForeignKeyClause();
            } else if(check(TokenType::kwCollate)) {
                advanceToken();
                if(!atEnd()) {
                    columnDef.collation = std::string(current().value);
                    advanceToken();
                }
            } else if(check(TokenType::kwGenerated) || (check(TokenType::kwAs) && peekToken(1).type == TokenType::leftParen)) {
                columnDef.generatedAlways = false;
                if(match(TokenType::kwGenerated)) {
                    match(TokenType::kwAlways);
                    columnDef.generatedAlways = true;
                }
                match(TokenType::kwAs);
                if(match(TokenType::leftParen)) {
                    columnDef.generatedExpression = this->parser.parseExpression();
                    match(TokenType::rightParen);
                }
                if(match(TokenType::kwStored)) {
                    columnDef.generatedStorage = ColumnDef::GeneratedStorage::stored;
                } else if(match(TokenType::kwVirtual)) {
                    columnDef.generatedStorage = ColumnDef::GeneratedStorage::virtual_;
                }
            } else if(check(TokenType::kwAutoincrement)) {
                advanceToken();
                columnDef.autoincrement = true;
            } else if(check(TokenType::leftParen)) {
                int depth = 1;
                advanceToken();
                while(depth > 0 && !atEnd()) {
                    if(check(TokenType::leftParen)) depth++;
                    else if(check(TokenType::rightParen)) depth--;
                    if(depth > 0) advanceToken();
                }
                if(depth == 0) advanceToken();
            } else {
                advanceToken();
            }
        }
    }

    TableForeignKey DdlParser::parseTableForeignKey() {
        advanceToken();
        match(TokenType::kwKey);
        std::string column;
        if(match(TokenType::leftParen)) {
            if(!atEnd()) {
                column = std::string(current().value);
                advanceToken();
            }
            match(TokenType::rightParen);
        }
        match(TokenType::kwReferences);
        auto references = parseForeignKeyClause();
        return TableForeignKey{std::move(column), std::move(references)};
    }

    ColumnDef DdlParser::parseColumnDef() {
        std::string name(current().value);
        advanceToken();

        std::string typeName;
        if(!check(TokenType::comma) && !check(TokenType::rightParen) && !atEnd()) {
            typeName = parseColumnTypeName();
        }

        ColumnDef columnDef{std::move(name), std::move(typeName)};
        parseColumnConstraints(columnDef);
        return columnDef;
    }

    AstNodePointer DdlParser::parseCreate() {
        if(!check(TokenType::kwCreate)) return nullptr;
        auto location = current().location;
        advanceToken();

        if(match(TokenType::kwTemp)) {
            if(check(TokenType::kwTrigger)) {
                advanceToken();
                return parseCreateTriggerAfterKeyword(location, true);
            }
            if(check(TokenType::kwView)) {
                advanceToken();
                return parseCreateViewTail(location);
            }
            if(match(TokenType::kwVirtual)) {
                if(!match(TokenType::kwTable)) return nullptr;
                return parseCreateVirtualTableTail(location, true);
            }
            if(!match(TokenType::kwTable)) return nullptr;
            return parseCreateTableTail(location);
        }
        if(match(TokenType::kwTemporary)) {
            if(check(TokenType::kwTrigger)) {
                advanceToken();
                return parseCreateTriggerAfterKeyword(location, true);
            }
            if(check(TokenType::kwView)) {
                advanceToken();
                return parseCreateViewTail(location);
            }
            if(match(TokenType::kwVirtual)) {
                if(!match(TokenType::kwTable)) return nullptr;
                return parseCreateVirtualTableTail(location, true);
            }
            if(!match(TokenType::kwTable)) return nullptr;
            return parseCreateTableTail(location);
        }

        if(check(TokenType::kwTrigger)) {
            advanceToken();
            return parseCreateTriggerAfterKeyword(location, false);
        }
        if(match(TokenType::kwUnique)) {
            if(!match(TokenType::kwIndex)) return nullptr;
            return parseCreateIndexAfterKeyword(location, true);
        }
        if(check(TokenType::kwIndex)) {
            advanceToken();
            return parseCreateIndexAfterKeyword(location, false);
        }
        if(match(TokenType::kwVirtual)) {
            if(!match(TokenType::kwTable)) return nullptr;
            return parseCreateVirtualTableTail(location, false);
        }
        if(check(TokenType::kwView)) {
            advanceToken();
            return parseCreateViewTail(location);
        }
        if(!match(TokenType::kwTable)) return nullptr;
        return parseCreateTableTail(location);
    }

    AstNodePointer DdlParser::parseCreateViewTail(SourceLocation location) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> viewSchemaName;
        std::string viewName;
        if(!this->parser.parseDmlQualifiedTable(viewSchemaName, viewName)) return nullptr;

        std::vector<std::string> columnNames;
        if(match(TokenType::leftParen)) {
            if(!check(TokenType::rightParen)) {
                do {
                    if(!isColumnNameToken()) return nullptr;
                    columnNames.emplace_back(std::string(current().value));
                    advanceToken();
                } while(match(TokenType::comma));
            }
            if(!match(TokenType::rightParen)) return nullptr;
        }

        if(!match(TokenType::kwAs)) return nullptr;
        AstNodePointer selectAst = this->parser.parseSelect();
        if(!selectAst) return nullptr;

        return std::make_unique<CreateViewNode>(location, ifNotExists, std::move(viewSchemaName), std::move(viewName),
                                                  std::move(columnNames), std::move(selectAst));
    }

    AstNodePointer DdlParser::parseCreateTableTail(SourceLocation location) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        if(!isColumnNameToken()) return nullptr;
        std::string tableName(current().value);
        advanceToken();

        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            tableName = std::string(current().value);
            advanceToken();
        }

        if(!match(TokenType::leftParen)) return nullptr;

        std::vector<ColumnDef> columns;
        std::vector<TableForeignKey> foreignKeys;
        std::vector<TablePrimaryKey> primaryKeys;
        std::vector<TableUnique> uniques;
        std::vector<TableCheck> checks;

        auto parseTableConstraint = [&]() -> bool {
            if(check(TokenType::kwConstraint)) {
                advanceToken();
                if(!atEnd()) advanceToken();
            }
            if(check(TokenType::kwForeign)) {
                foreignKeys.push_back(parseTableForeignKey());
                return true;
            } else if(check(TokenType::kwPrimary)) {
                advanceToken();
                match(TokenType::kwKey);
                std::vector<std::string> cols;
                if(match(TokenType::leftParen)) {
                    if(!atEnd()) {
                        cols.emplace_back(current().value);
                        advanceToken();
                    }
                    while(match(TokenType::comma)) {
                        if(!atEnd()) {
                            cols.emplace_back(current().value);
                            advanceToken();
                        }
                    }
                    match(TokenType::rightParen);
                }
                primaryKeys.push_back(TablePrimaryKey{std::move(cols)});
                return true;
            } else if(check(TokenType::kwUnique)) {
                advanceToken();
                std::vector<std::string> cols;
                if(match(TokenType::leftParen)) {
                    if(!atEnd()) {
                        cols.emplace_back(current().value);
                        advanceToken();
                    }
                    while(match(TokenType::comma)) {
                        if(!atEnd()) {
                            cols.emplace_back(current().value);
                            advanceToken();
                        }
                    }
                    match(TokenType::rightParen);
                }
                uniques.push_back(TableUnique{std::move(cols)});
                return true;
            } else if(check(TokenType::kwCheck)) {
                advanceToken();
                if(match(TokenType::leftParen)) {
                    auto expression = this->parser.parseExpression();
                    match(TokenType::rightParen);
                    checks.push_back(TableCheck{std::move(expression)});
                }
                return true;
            }
            return false;
        };

        if(!check(TokenType::rightParen)) {
            if(!parseTableConstraint()) {
                columns.push_back(parseColumnDef());
            }
            while(match(TokenType::comma)) {
                if(check(TokenType::rightParen)) break;
                if(!parseTableConstraint()) {
                    columns.push_back(parseColumnDef());
                }
            }
        }

        if(!match(TokenType::rightParen)) return nullptr;

        auto node = std::make_unique<CreateTableNode>(
            std::move(tableName), std::move(columns), std::move(foreignKeys), ifNotExists, location);
        node->primaryKeys = std::move(primaryKeys);
        node->uniques = std::move(uniques);
        node->checks = std::move(checks);

        while(check(TokenType::kwWithout) || check(TokenType::kwStrict)) {
            if(check(TokenType::kwWithout)) {
                advanceToken();
                if(!atEnd() && toLowerAscii(std::string(current().value)) == "rowid") {
                    advanceToken();
                    node->withoutRowid = true;
                }
            } else if(match(TokenType::kwStrict)) {
                node->strict = true;
            }
            if(!match(TokenType::comma)) break;
        }

        return node;
    }

    AstNodePointer DdlParser::parseCreateTriggerAfterKeyword(SourceLocation location, bool temporary) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> triggerSchema;
        std::string triggerName;
        if(!isColumnNameToken()) return nullptr;
        std::string firstTriggerId(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            triggerSchema = std::move(firstTriggerId);
            triggerName = std::string(current().value);
            advanceToken();
        } else {
            triggerName = std::move(firstTriggerId);
        }

        TriggerTiming timing;
        if(match(TokenType::kwBefore)) {
            timing = TriggerTiming::before;
        } else if(match(TokenType::kwAfter)) {
            timing = TriggerTiming::after;
        } else if(check(TokenType::kwInstead)) {
            advanceToken();
            if(!match(TokenType::kwOf)) return nullptr;
            timing = TriggerTiming::insteadOf;
        } else {
            return nullptr;
        }

        TriggerEventKind eventKind = TriggerEventKind::insert_;
        std::vector<std::string> updateOfColumns;
        if(match(TokenType::kwDelete)) {
            eventKind = TriggerEventKind::delete_;
        } else if(match(TokenType::kwInsert)) {
            eventKind = TriggerEventKind::insert_;
        } else if(match(TokenType::kwUpdate)) {
            if(match(TokenType::kwOf)) {
                eventKind = TriggerEventKind::updateOf;
                if(!isColumnNameToken()) return nullptr;
                updateOfColumns.emplace_back(current().value);
                advanceToken();
                while(match(TokenType::comma)) {
                    if(!isColumnNameToken()) return nullptr;
                    updateOfColumns.emplace_back(current().value);
                    advanceToken();
                }
            } else {
                eventKind = TriggerEventKind::update_;
            }
        } else {
            return nullptr;
        }

        if(!match(TokenType::kwOn)) return nullptr;
        std::optional<std::string> tableSchema;
        std::string tableName;
        if(!this->parser.parseDmlQualifiedTable(tableSchema, tableName)) return nullptr;

        bool forEachRow = false;
        if(check(TokenType::kwFor)) {
            advanceToken();
            if(!match(TokenType::kwEach)) return nullptr;
            if(!match(TokenType::kwRow)) return nullptr;
            forEachRow = true;
        }

        AstNodePointer whenClause;
        if(match(TokenType::kwWhen)) {
            whenClause = this->parser.parseExpression();
            if(!whenClause) return nullptr;
        }

        if(!match(TokenType::kwBegin)) return nullptr;

        if(check(TokenType::kwEnd)) return nullptr;

        std::vector<AstNodePointer> body;
        while(!check(TokenType::kwEnd) && !atEnd()) {
            AstNodePointer stmt = parseTriggerBodyStatement();
            if(!stmt) return nullptr;
            body.push_back(std::move(stmt));
            if(!match(TokenType::semicolon)) {
                if(check(TokenType::kwEnd)) {
                    break;
                }
                return nullptr;
            }
        }
        if(!match(TokenType::kwEnd)) return nullptr;

        auto node = std::make_unique<CreateTriggerNode>(location);
        node->triggerSchemaName = std::move(triggerSchema);
        node->triggerName = std::move(triggerName);
        node->ifNotExists = ifNotExists;
        node->temporary = temporary;
        node->timing = timing;
        node->eventKind = eventKind;
        node->updateOfColumns = std::move(updateOfColumns);
        node->tableSchemaName = std::move(tableSchema);
        node->tableName = std::move(tableName);
        node->forEachRow = forEachRow;
        node->whenClause = std::move(whenClause);
        node->bodyStatements = std::move(body);
        return node;
    }

    AstNodePointer DdlParser::parseTriggerBodyStatement() {
        if(check(TokenType::kwSelect)) {
            return this->parser.parseSelect();
        }
        if(check(TokenType::kwInsert)) {
            return this->parser.parseInsertStatement(false);
        }
        if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
            return this->parser.parseInsertStatement(true);
        }
        if(check(TokenType::kwUpdate)) {
            return this->parser.parseUpdateStatement();
        }
        if(check(TokenType::kwDelete)) {
            return this->parser.parseDeleteStatement();
        }
        return nullptr;
    }

    bool DdlParser::parseIndexColumnSpec(IndexColumnSpec& out) {
        out = IndexColumnSpec{};
        out.expression = this->parser.parseExpression();
        if(!out.expression) return false;
        if(auto* collateExpr = dynamic_cast<CollateNode*>(out.expression.get())) {
            out.collation = collateExpr->collationName;
            out.expression = std::move(collateExpr->operand);
        }
        while(true) {
            if(out.collation.empty() && match(TokenType::kwCollate)) {
                if(!isColumnNameToken()) return false;
                out.collation = std::string(current().value);
                advanceToken();
                continue;
            }
            if(match(TokenType::kwAsc)) {
                if(out.sortDirection != SortDirection::none) return false;
                out.sortDirection = SortDirection::asc;
                continue;
            }
            if(match(TokenType::kwDesc)) {
                if(out.sortDirection != SortDirection::none) return false;
                out.sortDirection = SortDirection::desc;
                continue;
            }
            break;
        }
        return true;
    }

    AstNodePointer DdlParser::parseCreateIndexAfterKeyword(SourceLocation location, bool unique) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> indexSchema;
        std::string indexName;
        if(!isColumnNameToken()) return nullptr;
        std::string firstIndexId(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            indexSchema = std::move(firstIndexId);
            indexName = std::string(current().value);
            advanceToken();
        } else {
            indexName = std::move(firstIndexId);
        }

        if(!match(TokenType::kwOn)) return nullptr;
        std::optional<std::string> tableSchema;
        std::string tableName;
        if(!this->parser.parseDmlQualifiedTable(tableSchema, tableName)) return nullptr;

        if(!match(TokenType::leftParen)) return nullptr;
        std::vector<IndexColumnSpec> columns;
        if(!check(TokenType::rightParen)) {
            IndexColumnSpec spec;
            if(!parseIndexColumnSpec(spec)) return nullptr;
            columns.push_back(std::move(spec));
            while(match(TokenType::comma)) {
                IndexColumnSpec nextSpec;
                if(!parseIndexColumnSpec(nextSpec)) return nullptr;
                columns.push_back(std::move(nextSpec));
            }
        }
        if(!match(TokenType::rightParen)) return nullptr;

        AstNodePointer whereClause;
        if(match(TokenType::kwWhere)) {
            whereClause = this->parser.parseExpression();
            if(!whereClause) return nullptr;
        }

        auto node = std::make_unique<CreateIndexNode>(location);
        node->unique = unique;
        node->ifNotExists = ifNotExists;
        node->indexSchemaName = std::move(indexSchema);
        node->indexName = std::move(indexName);
        node->tableSchemaName = std::move(tableSchema);
        node->tableName = std::move(tableName);
        node->indexedColumns = std::move(columns);
        node->whereClause = std::move(whereClause);
        return node;
    }

    AstNodePointer DdlParser::parseCreateVirtualTableTail(SourceLocation location, bool temporary) {
        bool ifNotExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) return nullptr;
            if(!match(TokenType::kwExists)) return nullptr;
            ifNotExists = true;
        }

        std::optional<std::string> tableSchema;
        std::string vtabName;
        if(!isColumnNameToken()) return nullptr;
        std::string firstTableId(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            if(!isColumnNameToken()) return nullptr;
            tableSchema = std::move(firstTableId);
            vtabName = std::string(current().value);
            advanceToken();
        } else {
            vtabName = std::move(firstTableId);
        }

        if(!match(TokenType::kwUsing)) return nullptr;
        if(!isColumnNameToken()) return nullptr;
        std::string moduleName(current().value);
        advanceToken();

        std::vector<AstNodePointer> moduleArguments;
        if(match(TokenType::leftParen)) {
            if(!check(TokenType::rightParen)) {
                while(true) {
                    auto arg = this->parser.parseExpression();
                    if(!arg) return nullptr;
                    moduleArguments.push_back(std::move(arg));
                    if(match(TokenType::comma)) {
                        continue;
                    }
                    break;
                }
            }
            if(!match(TokenType::rightParen)) return nullptr;
        }

        auto node = std::make_unique<CreateVirtualTableNode>(location);
        node->temporary = temporary;
        node->ifNotExists = ifNotExists;
        node->tableSchemaName = std::move(tableSchema);
        node->tableName = std::move(vtabName);
        node->moduleName = std::move(moduleName);
        node->moduleArguments = std::move(moduleArguments);
        return node;
    }

    AstNodePointer DdlParser::parseTransactionControlStatement() {
        const SourceLocation location = current().location;
        if(check(TokenType::kwBegin)) {
            advanceToken();
            auto node = std::make_unique<TransactionControlNode>(location);
            node->kind = TransactionControlNode::Kind::begin;
            if(match(TokenType::kwDeferred)) {
                node->beginMode = BeginTransactionMode::deferred;
            } else if(match(TokenType::kwImmediate)) {
                node->beginMode = BeginTransactionMode::immediate;
            } else if(match(TokenType::kwExclusive)) {
                node->beginMode = BeginTransactionMode::exclusive;
            } else {
                node->beginMode = BeginTransactionMode::plain;
            }
            match(TokenType::kwTransaction);
            return node;
        }
        if(check(TokenType::kwCommit) || check(TokenType::kwEnd)) {
            advanceToken();
            match(TokenType::kwTransaction);
            auto node = std::make_unique<TransactionControlNode>(location);
            node->kind = TransactionControlNode::Kind::commit;
            return node;
        }
        if(check(TokenType::kwRollback)) {
            advanceToken();
            auto node = std::make_unique<TransactionControlNode>(location);
            node->kind = TransactionControlNode::Kind::rollback;
            if(match(TokenType::kwTo)) {
                (void)match(TokenType::kwSavepoint);
                if(!isColumnNameToken()) {
                    return nullptr;
                }
                node->rollbackToSavepoint = std::string(current().value);
                advanceToken();
                return node;
            }
            match(TokenType::kwTransaction);
            return node;
        }
        return nullptr;
    }

    AstNodePointer DdlParser::parseVacuumStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        if(atEnd() || check(TokenType::semicolon)) {
            return std::make_unique<VacuumStatementNode>(location);
        }
        if(isColumnNameToken()) {
            std::string schema = std::string(current().value);
            advanceToken();
            return std::make_unique<VacuumStatementNode>(location, std::move(schema));
        }
        return nullptr;
    }

    AstNodePointer DdlParser::parseDropStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        DropObjectKind kind;
        if(match(TokenType::kwTable)) {
            kind = DropObjectKind::table;
        } else if(match(TokenType::kwIndex)) {
            kind = DropObjectKind::index;
        } else if(match(TokenType::kwTrigger)) {
            kind = DropObjectKind::trigger;
        } else if(match(TokenType::kwView)) {
            kind = DropObjectKind::view;
        } else {
            return nullptr;
        }
        bool ifExists = false;
        if(check(TokenType::kwIf)) {
            advanceToken();
            if(!match(TokenType::kwNot)) {
                return nullptr;
            }
            if(!match(TokenType::kwExists)) {
                return nullptr;
            }
            ifExists = true;
        }
        std::optional<std::string> schemaName;
        std::string objectName;
        if(!this->parser.parseDmlQualifiedTable(schemaName, objectName)) {
            return nullptr;
        }
        auto node = std::make_unique<DropStatementNode>(location);
        node->objectKind = kind;
        node->ifExists = ifExists;
        node->schemaName = std::move(schemaName);
        node->objectName = std::move(objectName);
        return node;
    }

    AstNodePointer DdlParser::parseAlterTableStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        if(!match(TokenType::kwTable)) {
            return nullptr;
        }
        std::optional<std::string> tableSchemaName;
        std::string tableName;
        if(!this->parser.parseDmlQualifiedTable(tableSchemaName, tableName)) {
            return nullptr;
        }
        skipToSemicolon();
        return std::make_unique<AlterTableStatementNode>(location, std::move(tableSchemaName), std::move(tableName));
    }

    AstNodePointer DdlParser::parseSavepointStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        if(!isColumnNameToken()) return nullptr;
        std::string name(current().value);
        advanceToken();
        return std::make_unique<SavepointNode>(location, std::move(name));
    }

    AstNodePointer DdlParser::parseReleaseStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        (void)match(TokenType::kwSavepoint);
        if(!isColumnNameToken()) return nullptr;
        std::string name(current().value);
        advanceToken();
        return std::make_unique<ReleaseNode>(location, std::move(name));
    }

    AstNodePointer DdlParser::parseAttachStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        (void)match(TokenType::kwDatabase);
        auto fileExpr = this->parser.parseExpression();
        if(!fileExpr) return nullptr;
        if(!match(TokenType::kwAs)) return nullptr;
        if(!isColumnNameToken()) return nullptr;
        std::string schemaName(current().value);
        advanceToken();
        return std::make_unique<AttachDatabaseNode>(location, std::move(fileExpr), std::move(schemaName));
    }

    AstNodePointer DdlParser::parseDetachStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        (void)match(TokenType::kwDatabase);
        if(!isColumnNameToken()) return nullptr;
        std::string schemaName(current().value);
        advanceToken();
        return std::make_unique<DetachDatabaseNode>(location, std::move(schemaName));
    }

    AstNodePointer DdlParser::parseAnalyzeStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        auto node = std::make_unique<AnalyzeNode>(location);
        if(!atEnd() && isColumnNameToken()) {
            node->schemaOrTableName = std::string(current().value);
            advanceToken();
            if(match(TokenType::dot)) {
                if(!isColumnNameToken()) return nullptr;
                node->tableName = std::string(current().value);
                advanceToken();
            }
        }
        return node;
    }

    AstNodePointer DdlParser::parseReindexStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        auto node = std::make_unique<ReindexNode>(location);
        if(!atEnd() && isColumnNameToken()) {
            node->schemaOrObjectName = std::string(current().value);
            advanceToken();
            if(match(TokenType::dot)) {
                if(!isColumnNameToken()) return nullptr;
                node->objectName = std::string(current().value);
                advanceToken();
            }
        }
        return node;
    }

    AstNodePointer DdlParser::parsePragmaStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        auto node = std::make_unique<PragmaNode>(location);
        if(!isColumnNameToken()) return nullptr;
        std::string firstName(current().value);
        advanceToken();
        if(match(TokenType::dot)) {
            node->schemaName = std::move(firstName);
            if(!isColumnNameToken()) return nullptr;
            node->pragmaName = std::string(current().value);
            advanceToken();
        } else {
            node->pragmaName = std::move(firstName);
        }
        if(match(TokenType::eq) || match(TokenType::eq2)) {
            node->value = this->parser.parseExpression();
            if(!node->value) return nullptr;
        } else if(match(TokenType::leftParen)) {
            node->value = this->parser.parseExpression();
            if(!node->value) return nullptr;
            if(!match(TokenType::rightParen)) return nullptr;
        }
        return node;
    }

    AstNodePointer DdlParser::parseExplainStatement() {
        const SourceLocation location = current().location;
        advanceToken();
        bool queryPlan = false;
        if(match(TokenType::kwQuery)) {
            if(!match(TokenType::kwPlan)) return nullptr;
            queryPlan = true;
        }
        AstNodePointer innerStatement;
        if(check(TokenType::kwSelect) || check(TokenType::kwWith)) {
            innerStatement = this->parser.parseSelect();
        } else if(check(TokenType::kwInsert)) {
            innerStatement = this->parser.parseInsertStatement(false);
        } else if(check(TokenType::kwReplace) && peekToken(1).type == TokenType::kwInto) {
            innerStatement = this->parser.parseInsertStatement(true);
        } else if(check(TokenType::kwUpdate)) {
            innerStatement = this->parser.parseUpdateStatement();
        } else if(check(TokenType::kwDelete)) {
            innerStatement = this->parser.parseDeleteStatement();
        } else if(check(TokenType::kwCreate)) {
            innerStatement = parseCreate();
        } else if(check(TokenType::kwDrop)) {
            innerStatement = parseDropStatement();
        } else if(check(TokenType::kwAlter)) {
            innerStatement = parseAlterTableStatement();
        } else {
            innerStatement = this->parser.parseExpression();
        }
        if(!innerStatement) return nullptr;
        return std::make_unique<ExplainNode>(location, queryPlan, std::move(innerStatement));
    }

}  // namespace sqlite2orm
