#include <sqlite2orm/codegen.h>

#include <sqlite2orm/utils.h>

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace sqlite2orm {

    namespace {

        bool policyEquals(const CodeGenPolicy* policy, std::string_view category, std::string_view value) {
            if(!policy) {
                return false;
            }
            const auto it = policy->chosenAlternativeValueByCategory.find(std::string(category));
            return it != policy->chosenAlternativeValueByCategory.end() && it->second == value;
        }

        std::string strip_identifier_quotes(std::string_view identifier) {
            if(identifier.size() >= 2) {
                char first = identifier.front();
                char last = identifier.back();
                if((first == '"' && last == '"') ||
                   (first == '`' && last == '`') ||
                   (first == '[' && last == ']')) {
                    return std::string(identifier.substr(1, identifier.size() - 2));
                }
            }
            return std::string(identifier);
        }

        std::string to_cpp_identifier(std::string_view sqlName) {
            auto stripped = strip_identifier_quotes(sqlName);
            std::string result;
            result.reserve(stripped.size());
            for(char c : stripped) {
                if(std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                    result += c;
                } else {
                    result += '_';
                }
            }
            if(!result.empty() && std::isdigit(static_cast<unsigned char>(result[0]))) {
                result = "_" + result;
            }
            return result;
        }

        std::string identifier_to_cpp_string_literal(std::string_view sqlIdentifier) {
            auto body = strip_identifier_quotes(sqlIdentifier);
            std::string result = "\"";
            for(char c : body) {
                if(c == '\\') {
                    result += "\\\\";
                } else if(c == '"') {
                    result += "\\\"";
                } else if(c == '\n') {
                    result += "\\n";
                } else if(c == '\r') {
                    result += "\\r";
                } else if(c == '\t') {
                    result += "\\t";
                } else {
                    result += c;
                }
            }
            result += '"';
            return result;
        }

        std::string sql_string_to_cpp(std::string_view sqlString) {
            auto content = sqlString.substr(1, sqlString.size() - 2);
            std::string result = "\"";
            for(size_t i = 0; i < content.size(); ++i) {
                char c = content[i];
                if(c == '\'' && i + 1 < content.size() && content[i + 1] == '\'') {
                    result += '\'';
                    ++i;
                } else if(c == '\\') {
                    result += "\\\\";
                } else if(c == '"') {
                    result += "\\\"";
                } else if(c == '\n') {
                    result += "\\n";
                } else if(c == '\r') {
                    result += "\\r";
                } else if(c == '\t') {
                    result += "\\t";
                } else {
                    result += c;
                }
            }
            result += '"';
            return result;
        }

        std::string_view binary_operator_string(BinaryOperator binaryOperator) {
            switch(binaryOperator) {
                case BinaryOperator::logicalOr:             return " or ";
                case BinaryOperator::logicalAnd:            return " and ";
                case BinaryOperator::equals:         return " == ";
                case BinaryOperator::notEquals:      return " != ";
                case BinaryOperator::lessThan:       return " < ";
                case BinaryOperator::lessOrEqual:    return " <= ";
                case BinaryOperator::greaterThan:    return " > ";
                case BinaryOperator::greaterOrEqual: return " >= ";
                case BinaryOperator::add:            return " + ";
                case BinaryOperator::subtract:       return " - ";
                case BinaryOperator::multiply:       return " * ";
                case BinaryOperator::divide:         return " / ";
                case BinaryOperator::modulo:         return " % ";
                case BinaryOperator::concatenate:    return " || ";
                case BinaryOperator::bitwiseAnd:     return " & ";
                case BinaryOperator::bitwiseOr:      return " | ";
                case BinaryOperator::shiftLeft:      return " << ";
                case BinaryOperator::shiftRight:     return " >> ";
                case BinaryOperator::isOp:           // unreachable: blocked in generateNode()
                case BinaryOperator::isNot:
                case BinaryOperator::isDistinctFrom:
                case BinaryOperator::isNotDistinctFrom: return {};
                case BinaryOperator::jsonArrow:      return " -> ";
                case BinaryOperator::jsonArrow2:     return " ->> ";
            }
            return {};
        }

        std::string_view binary_functional_name(BinaryOperator binaryOperator) {
            switch(binaryOperator) {
                case BinaryOperator::logicalOr:             return "or_";
                case BinaryOperator::logicalAnd:            return "and_";
                case BinaryOperator::equals:         return "is_equal";
                case BinaryOperator::notEquals:      return "is_not_equal";
                case BinaryOperator::lessThan:       return "lesser_than";
                case BinaryOperator::lessOrEqual:    return "lesser_or_equal";
                case BinaryOperator::greaterThan:    return "greater_than";
                case BinaryOperator::greaterOrEqual: return "greater_or_equal";
                case BinaryOperator::add:            return "add";
                case BinaryOperator::subtract:       return "sub";
                case BinaryOperator::multiply:       return "mul";
                case BinaryOperator::divide:         return "div";
                case BinaryOperator::modulo:         return "mod";
                case BinaryOperator::concatenate:    return "conc";
                case BinaryOperator::bitwiseAnd:     return "bitwise_and";
                case BinaryOperator::bitwiseOr:      return "bitwise_or";
                case BinaryOperator::shiftLeft:      return "bitwise_shift_left";
                case BinaryOperator::shiftRight:     return "bitwise_shift_right";
                case BinaryOperator::isOp:           // unreachable: blocked in generateNode()
                case BinaryOperator::isNot:
                case BinaryOperator::isDistinctFrom:
                case BinaryOperator::isNotDistinctFrom: return {};
                case BinaryOperator::jsonArrow:      return "json_extract";
                case BinaryOperator::jsonArrow2:     return "json_extract";
            }
            return {};
        }

        std::string normalize_table_key(std::string_view sqlIdentifier) {
            return toLowerAscii(strip_identifier_quotes(sqlIdentifier));
        }

        bool ends_with(std::string_view string, std::string_view suffix) {
            return string.size() >= suffix.size() &&
                   string.compare(string.size() - suffix.size(), suffix.size(), suffix) == 0;
        }

        std::optional<std::string> extract_storage_select_argument(std::string_view generated) {
            constexpr std::string_view prefix = "auto rows = storage.select(";
            if(generated.size() <= prefix.size() + 2 || !generated.starts_with(prefix) || !ends_with(generated, ");")) {
                return std::nullopt;
            }
            return std::string(generated.substr(prefix.size(), generated.size() - prefix.size() - 2));
        }

        std::string strip_storage_prefix_and_trailing_semicolon(std::string c) {
            static constexpr std::string_view kStoragePrefix = "storage.";
            if(c.size() >= kStoragePrefix.size() && c.compare(0, kStoragePrefix.size(), kStoragePrefix) == 0) {
                c.erase(0, kStoragePrefix.size());
            }
            while(!c.empty() && std::isspace(static_cast<unsigned char>(c.back()))) {
                c.pop_back();
            }
            if(!c.empty() && c.back() == ';') {
                c.pop_back();
            }
            while(!c.empty() && std::isspace(static_cast<unsigned char>(c.back()))) {
                c.pop_back();
            }
            return c;
        }

        std::string blob_to_cpp(std::string_view blobLiteral) {
            auto hex = blobLiteral.substr(2, blobLiteral.size() - 3);
            if(hex.empty()) {
                return "std::vector<char>{}";
            }
            std::string result = "std::vector<char>{";
            for(size_t i = 0; i < hex.size(); i += 2) {
                if(i > 0) result += ", ";
                result += "'\\x";
                result += hex[i];
                if(i + 1 < hex.size()) result += hex[i + 1];
                result += "'";
            }
            result += "}";
            return result;
        }

        bool is_leaf_node(const AstNode& astNode) {
            return dynamic_cast<const IntegerLiteralNode*>(&astNode) ||
                   dynamic_cast<const RealLiteralNode*>(&astNode) ||
                   dynamic_cast<const StringLiteralNode*>(&astNode) ||
                   dynamic_cast<const NullLiteralNode*>(&astNode) ||
                   dynamic_cast<const BoolLiteralNode*>(&astNode) ||
                   dynamic_cast<const BlobLiteralNode*>(&astNode) ||
                   dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode) ||
                   dynamic_cast<const ColumnRefNode*>(&astNode) ||
                   dynamic_cast<const QualifiedColumnRefNode*>(&astNode) ||
                   dynamic_cast<const NewRefNode*>(&astNode) ||
                   dynamic_cast<const OldRefNode*>(&astNode) ||
                   dynamic_cast<const ExcludedRefNode*>(&astNode) ||
                   dynamic_cast<const RaiseNode*>(&astNode);
        }

        std::string wrap(std::string_view code) {
            return "c(" + std::string(code) + ")";
        }

        std::string sqlite_type_to_cpp(std::string_view typeName) {
            std::string lower = toLowerAscii(typeName);
            if(lower.find("bool") != std::string::npos) return "bool";
            if(lower.find("int") != std::string::npos) return "int64_t";
            if(lower.find("char") != std::string::npos ||
               lower.find("clob") != std::string::npos ||
               lower.find("text") != std::string::npos) return "std::string";
            if(lower.find("blob") != std::string::npos || lower.empty()) return "std::vector<char>";
            if(lower.find("real") != std::string::npos ||
               lower.find("floa") != std::string::npos ||
               lower.find("doub") != std::string::npos) return "double";
            return "double";
        }

        std::string default_initializer(std::string_view cppType) {
            if(cppType == "int" || cppType == "int64_t") return " = 0";
            if(cppType == "double") return " = 0.0";
            if(cppType == "bool") return " = false";
            return "";
        }

        std::string to_struct_name(std::string_view sqlName) {
            auto base = to_cpp_identifier(sqlName);
            std::string result;
            result.reserve(base.size());
            bool atWordStart = true;
            for(char c : base) {
                if(c == '_') {
                    atWordStart = true;
                    continue;
                }
                if(atWordStart) {
                    result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                    atWordStart = false;
                } else {
                    result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
            }
            if(result.empty() && !base.empty()) {
                return base;
            }
            return result;
        }

        std::string_view join_sqlite_orm_api_name(JoinKind joinKind) {
            switch(joinKind) {
            case JoinKind::crossJoin: return "cross_join";
            case JoinKind::innerJoin: return "inner_join";
            case JoinKind::leftJoin: return "left_join";
            case JoinKind::leftOuterJoin: return "left_outer_join";
            case JoinKind::joinPlain: return "join";
            case JoinKind::naturalInnerJoin: return "natural_join";
            default: return "inner_join";
            }
        }

        std::string_view compound_select_api(CompoundSelectOperator compoundOperator) {
            switch(compoundOperator) {
            case CompoundSelectOperator::unionDistinct: return "union_";
            case CompoundSelectOperator::unionAll: return "union_all";
            case CompoundSelectOperator::intersect: return "intersect";
            case CompoundSelectOperator::except: return "except";
            }
            return "union_";
        }

        std::string dml_insert_or_prefix(ConflictClause c) {
            switch(c) {
            case ConflictClause::rollback: return "or_rollback(), ";
            case ConflictClause::abort: return "or_abort(), ";
            case ConflictClause::fail: return "or_fail(), ";
            case ConflictClause::ignore: return "or_ignore(), ";
            case ConflictClause::replace: return "or_replace(), ";
            default: return "";
            }
        }

    }

    CodeGenResult CodeGenerator::generate(const AstNode& astNode) {
        accumulatedErrors.clear();
        auto result = generateNode(astNode);
        if(!accumulatedErrors.empty()) {
            return CodeGenResult{{}, {}, {}, std::move(accumulatedErrors)};
        }
        return result;
    }

    CodeGenResult CodeGenerator::generateNode(const AstNode& astNode) {
        if(auto* integerLiteral = dynamic_cast<const IntegerLiteralNode*>(&astNode)) {
            return CodeGenResult{std::string(integerLiteral->value), {}};
        } else if(auto* realLiteral = dynamic_cast<const RealLiteralNode*>(&astNode)) {
            return CodeGenResult{std::string(realLiteral->value), {}};
        } else if(auto* stringLiteral = dynamic_cast<const StringLiteralNode*>(&astNode)) {
            return CodeGenResult{sql_string_to_cpp(stringLiteral->value), {}};
        } else if(dynamic_cast<const NullLiteralNode*>(&astNode)) {
            return CodeGenResult{"nullptr", {}};
        } else if(auto* boolLiteral = dynamic_cast<const BoolLiteralNode*>(&astNode)) {
            return CodeGenResult{boolLiteral->value ? "true" : "false", {}};
        } else if(auto* raiseNode = dynamic_cast<const RaiseNode*>(&astNode)) {
            if(raiseNode->kind == RaiseKind::ignore) {
                return CodeGenResult{"raise_ignore()", {}};
            }
            if(!raiseNode->message) {
                return CodeGenResult{"raise_ignore()", {}};
            }
            std::vector<std::string> raiseWarnings;
            const char* api = "raise_abort";
            if(raiseNode->kind == RaiseKind::rollback) {
                api = "raise_rollback";
            } else if(raiseNode->kind == RaiseKind::abort) {
                api = "raise_abort";
            } else if(raiseNode->kind == RaiseKind::fail) {
                api = "raise_fail";
            }
            if(auto* strLit = dynamic_cast<const StringLiteralNode*>(raiseNode->message.get())) {
                return CodeGenResult{std::string(api) + "(" + sql_string_to_cpp(strLit->value) + ")", {}};
            }
            raiseWarnings.push_back(
                "RAISE(ROLLBACK|ABORT|FAIL, ...) message should be a SQL string literal for sqlite_orm raise_*()");
            return CodeGenResult{std::string(api) + "(\"\")", {}, std::move(raiseWarnings)};
        } else if(auto* blobLiteral = dynamic_cast<const BlobLiteralNode*>(&astNode)) {
            return CodeGenResult{blob_to_cpp(blobLiteral->value), {}};
        } else if(auto* currentDt = dynamic_cast<const CurrentDatetimeLiteralNode*>(&astNode)) {
            switch(currentDt->kind) {
            case CurrentDatetimeKind::time:
                return CodeGenResult{"current_time()", {}};
            case CurrentDatetimeKind::date:
                return CodeGenResult{"current_date()", {}};
            case CurrentDatetimeKind::timestamp:
                return CodeGenResult{"current_timestamp()", {}};
            }
            return CodeGenResult{"current_timestamp()", {}};
        } else if(auto* columnRef = dynamic_cast<const ColumnRefNode*>(&astNode)) {
            auto cppName = to_cpp_identifier(columnRef->columnName);
            registerColumn(cppName, "int");
            if(this->implicitSingleSourceCteTypedef) {
                std::string colLit = identifier_to_cpp_string_literal(strip_identifier_quotes(columnRef->columnName));
                std::string cteCol = "column<" + *this->implicitSingleSourceCteTypedef + ">(" + colLit + ")";
                return CodeGenResult{std::move(cteCol), {}};
            }
            std::string memberPointer = "&" + this->structName + "::" + cppName;
            std::string columnPointer = "column<" + this->structName + ">(" + memberPointer + ")";
            const bool useColumnPtr = policyEquals(this->codeGenPolicy, "column_ref_style", "column_pointer");
            const std::string& emittedCol = useColumnPtr ? columnPointer : memberPointer;
            const std::string chosenCol = useColumnPtr ? "column_pointer" : "member_pointer";
            return CodeGenResult{
                emittedCol,
                {DecisionPoint{
                    this->nextDecisionPointId++,
                    "column_ref_style",
                    chosenCol,
                    emittedCol,
                    {Alternative{"column_pointer", columnPointer, "explicit mapped type (inheritance / ambiguity)"}}}}};
        } else if(auto* qualifiedRef = dynamic_cast<const QualifiedColumnRefNode*>(&astNode)) {
            std::vector<std::string> qualWarnings;
            if(qualifiedRef->schemaName) {
                qualWarnings.push_back(
                    "schema-qualified column " + std::string(*qualifiedRef->schemaName) + "." +
                    std::string(qualifiedRef->tableName) + "." + std::string(qualifiedRef->columnName) +
                    " is not represented in sqlite_orm mapping; generated column reference uses table `" +
                    std::string(qualifiedRef->tableName) + "` only");
            }
            std::string tableKeyNorm = normalize_table_key(qualifiedRef->tableName);
            auto cteIt = this->activeCteTypedefByTableKey.find(tableKeyNorm);
            if(cteIt != this->activeCteTypedefByTableKey.end()) {
                std::string colLit =
                    identifier_to_cpp_string_literal(strip_identifier_quotes(qualifiedRef->columnName));
                return CodeGenResult{"column<" + cteIt->second + ">(" + colLit + ")", {},
                                     std::move(qualWarnings)};
            }
            std::string tableKey = std::string(qualifiedRef->tableName);
            std::string structForColumn = to_struct_name(qualifiedRef->tableName);
            auto aliasIt = this->fromTableAliasToStructName.find(tableKey);
            if(aliasIt != this->fromTableAliasToStructName.end()) {
                structForColumn = aliasIt->second;
            }
            std::string memberPointer = "&" + structForColumn + "::" + to_cpp_identifier(qualifiedRef->columnName);
            std::string columnPointer = "column<" + structForColumn + ">(" + memberPointer + ")";
            const bool useQColPtr = policyEquals(this->codeGenPolicy, "column_ref_style", "column_pointer");
            const std::string& emittedQ = useQColPtr ? columnPointer : memberPointer;
            const std::string chosenQ = useQColPtr ? "column_pointer" : "member_pointer";
            return CodeGenResult{
                emittedQ,
                {DecisionPoint{
                    this->nextDecisionPointId++,
                    "column_ref_style",
                    chosenQ,
                    emittedQ,
                    {Alternative{"column_pointer", columnPointer, "explicit mapped type (inheritance / ambiguity)"}}}},
                std::move(qualWarnings)};
        } else if(auto* qualifiedAsterisk = dynamic_cast<const QualifiedAsteriskNode*>(&astNode)) {
            std::vector<std::string> qualifiedAsteriskWarnings;
            if(qualifiedAsterisk->schemaName) {
                qualifiedAsteriskWarnings.push_back(
                    "schema-qualified SELECT result column " + *qualifiedAsterisk->schemaName + "." +
                    qualifiedAsterisk->tableName +
                    ".* is not represented in sqlite_orm; generated code uses asterisk<" +
                    to_struct_name(qualifiedAsterisk->tableName) + ">() (table type only)");
            }
            return CodeGenResult{"asterisk<" + to_struct_name(qualifiedAsterisk->tableName) + ">()", {},
                                 std::move(qualifiedAsteriskWarnings)};
        } else if(auto* newRef = dynamic_cast<const NewRefNode*>(&astNode)) {
            auto cppName = to_cpp_identifier(newRef->columnName);
            registerColumn(cppName, "int");
            return CodeGenResult{"new_(&" + this->structName + "::" + cppName + ")", {}};
        } else if(auto* oldRef = dynamic_cast<const OldRefNode*>(&astNode)) {
            auto cppName = to_cpp_identifier(oldRef->columnName);
            registerColumn(cppName, "int");
            return CodeGenResult{"old(&" + this->structName + "::" + cppName + ")", {}};
        } else if(auto* excludedRef = dynamic_cast<const ExcludedRefNode*>(&astNode)) {
            auto cppName = to_cpp_identifier(excludedRef->columnName);
            return CodeGenResult{"excluded(&" + this->structName + "::" + cppName + ")", {}};
        } else if(auto* binaryOp = dynamic_cast<const BinaryOperatorNode*>(&astNode)) {
            if(binaryOp->binaryOperator == BinaryOperator::isOp ||
               binaryOp->binaryOperator == BinaryOperator::isNot ||
               binaryOp->binaryOperator == BinaryOperator::isDistinctFrom ||
               binaryOp->binaryOperator == BinaryOperator::isNotDistinctFrom) {
                accumulatedErrors.push_back(
                    "binary IS / IS NOT / IS [NOT] DISTINCT FROM "
                    "is not supported in sqlite_orm");
                return CodeGenResult{"/* unsupported IS expression */"};
            }
            auto leftResult = generateNode(*binaryOp->lhs);
            auto rightResult = generateNode(*binaryOp->rhs);

            if(auto* leftCol = dynamic_cast<const ColumnRefNode*>(binaryOp->lhs.get())) {
                registerColumn(to_cpp_identifier(leftCol->columnName), inferTypeFromNode(*binaryOp->rhs));
            }
            if(auto* rightCol = dynamic_cast<const ColumnRefNode*>(binaryOp->rhs.get())) {
                registerColumn(to_cpp_identifier(rightCol->columnName), inferTypeFromNode(*binaryOp->lhs));
            }

            auto decisionPoints = std::move(leftResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                                  std::make_move_iterator(rightResult.decisionPoints.begin()),
                                  std::make_move_iterator(rightResult.decisionPoints.end()));

            bool leftLeaf = is_leaf_node(*binaryOp->lhs);
            bool rightLeaf = is_leaf_node(*binaryOp->rhs);

            std::string wrappedLeft = leftLeaf ? wrap(leftResult.code) : leftResult.code;
            std::string wrappedRight = rightLeaf ? wrap(rightResult.code) : rightResult.code;

            auto funcName = binary_functional_name(binaryOp->binaryOperator);
            std::string functionalCode = std::string(funcName) + "(" + leftResult.code + ", " + rightResult.code + ")";

            auto op = binary_operator_string(binaryOp->binaryOperator);
            std::string wrapLeftCode = wrappedLeft + std::string(op) + rightResult.code;
            std::string wrapRightCode = leftResult.code + std::string(op) + wrappedRight;
            std::string wrapBothCode = wrappedLeft + std::string(op) + wrappedRight;

            std::string chosenExprVal = "operator_wrap_left";
            std::string emittedExpr = wrapLeftCode;
            if(policyEquals(this->codeGenPolicy, "expr_style", "operator_wrap_right")) {
                chosenExprVal = "operator_wrap_right";
                emittedExpr = wrapRightCode;
            } else if(policyEquals(this->codeGenPolicy, "expr_style", "functional")) {
                chosenExprVal = "functional";
                emittedExpr = functionalCode;
            } else if(policyEquals(this->codeGenPolicy, "expr_style", "operator_wrap_both")) {
                chosenExprVal = "operator_wrap_both";
                emittedExpr = wrapBothCode;
            }
            if(binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
               binaryOp->binaryOperator == BinaryOperator::jsonArrow2) {
                chosenExprVal = "functional";
                emittedExpr = functionalCode;
            }

            decisionPoints.push_back(DecisionPoint{
                this->nextDecisionPointId++,
                "expr_style",
                chosenExprVal,
                emittedExpr,
                {
                    Alternative{"operator_wrap_right", wrapRightCode, "wrap right operand"},
                    Alternative{"functional", functionalCode, "functional style"},
                    Alternative{"operator_wrap_both", wrapBothCode, "wrap both operands", true},
                }
            });

            std::vector<std::string> binWarnings;
            binWarnings.insert(binWarnings.end(),
                               std::make_move_iterator(leftResult.warnings.begin()),
                               std::make_move_iterator(leftResult.warnings.end()));
            binWarnings.insert(binWarnings.end(),
                               std::make_move_iterator(rightResult.warnings.begin()),
                               std::make_move_iterator(rightResult.warnings.end()));

            if(binaryOp->binaryOperator == BinaryOperator::jsonArrow ||
               binaryOp->binaryOperator == BinaryOperator::jsonArrow2) {
                binWarnings.push_back("JSON -> / ->> operator is mapped to json_extract() "
                                      "— return type may differ from sqlite");
            }

            return CodeGenResult{std::move(emittedExpr), std::move(decisionPoints), std::move(binWarnings)};
        } else if(auto* unaryOp = dynamic_cast<const UnaryOperatorNode*>(&astNode)) {
            auto operandResult = generateNode(*unaryOp->operand);
            auto decisionPoints = std::move(operandResult.decisionPoints);

            if(unaryOp->unaryOperator == UnaryOperator::plus) {
                return CodeGenResult{operandResult.code, std::move(decisionPoints)};
            }

            bool operandLeaf = is_leaf_node(*unaryOp->operand);
            std::string operandStr = operandLeaf
                ? wrap(operandResult.code)
                : "(" + operandResult.code + ")";

            std::string_view funcName;
            std::string_view opStr;
            if(unaryOp->unaryOperator == UnaryOperator::minus) {
                funcName = "minus";
                opStr = "-";
            } else if(unaryOp->unaryOperator == UnaryOperator::bitwiseNot) {
                funcName = "bitwise_not";
                opStr = "~";
            } else {
                funcName = "not_";
                opStr = "not ";
            }
            std::string operatorCode = std::string(opStr) + operandStr;
            std::string functionalCode = std::string(funcName) + "(" + operandResult.code + ")";

            std::vector<Alternative> alternatives;
            if(unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                alternatives.push_back(Alternative{"operator_excl", "!" + operandStr, "use ! instead of not"});
            }
            alternatives.push_back(Alternative{"functional", functionalCode, "functional style"});

            std::string chosenUnaryVal = "operator";
            std::string emittedUnary = operatorCode;
            if(policyEquals(this->codeGenPolicy, "expr_style", "functional")) {
                chosenUnaryVal = "functional";
                emittedUnary = functionalCode;
            } else if(policyEquals(this->codeGenPolicy, "expr_style", "operator_excl") &&
                      unaryOp->unaryOperator == UnaryOperator::logicalNot) {
                chosenUnaryVal = "operator_excl";
                emittedUnary = "!" + operandStr;
            }

            decisionPoints.push_back(DecisionPoint{
                this->nextDecisionPointId++,
                "expr_style",
                chosenUnaryVal,
                emittedUnary,
                std::move(alternatives)
            });

            return CodeGenResult{std::move(emittedUnary), std::move(decisionPoints)};
        } else if(auto* isNullNode = dynamic_cast<const IsNullNode*>(&astNode)) {
            auto operandResult = generateNode(*isNullNode->operand);
            return CodeGenResult{"is_null(" + operandResult.code + ")", std::move(operandResult.decisionPoints)};
        } else if(auto* isNotNullNode = dynamic_cast<const IsNotNullNode*>(&astNode)) {
            auto operandResult = generateNode(*isNotNullNode->operand);
            return CodeGenResult{"is_not_null(" + operandResult.code + ")", std::move(operandResult.decisionPoints)};
        } else if(auto* betweenNode = dynamic_cast<const BetweenNode*>(&astNode)) {
            auto operandResult = generateNode(*betweenNode->operand);
            auto lowResult = generateNode(*betweenNode->low);
            auto highResult = generateNode(*betweenNode->high);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(betweenNode->operand.get())) {
                registerColumn(to_cpp_identifier(col->columnName), inferTypeFromNode(*betweenNode->low));
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                std::make_move_iterator(lowResult.decisionPoints.begin()),
                std::make_move_iterator(lowResult.decisionPoints.end()));
            decisionPoints.insert(decisionPoints.end(),
                std::make_move_iterator(highResult.decisionPoints.begin()),
                std::make_move_iterator(highResult.decisionPoints.end()));

            std::string betweenCode = "between(" + operandResult.code + ", " +
                                       lowResult.code + ", " + highResult.code + ")";
            std::string code = betweenNode->negated ? "not_(" + betweenCode + ")" : betweenCode;
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* subqueryNode = dynamic_cast<const SubqueryNode*>(&astNode)) {
            auto sub = tryCodegenSelectLikeSubquery(*subqueryNode->select);
            if(sub.code.empty()) {
                sub.warnings.insert(sub.warnings.begin(),
                                    "scalar subquery (SELECT ...) is not mapped to sqlite_orm codegen");
                return CodeGenResult{"/* (SELECT ...) */", std::move(sub.decisionPoints), std::move(sub.warnings)};
            }
            return CodeGenResult{sub.code, std::move(sub.decisionPoints), std::move(sub.warnings)};
        } else if(auto* existsNode = dynamic_cast<const ExistsNode*>(&astNode)) {
            auto sub = tryCodegenSelectLikeSubquery(*existsNode->select);
            if(sub.code.empty()) {
                sub.warnings.insert(sub.warnings.begin(),
                                    "EXISTS (SELECT ...) is not mapped to sqlite_orm codegen");
                return CodeGenResult{"/* EXISTS (SELECT ...) */", std::move(sub.decisionPoints),
                                     std::move(sub.warnings)};
            }
            return CodeGenResult{"exists(" + sub.code + ")", std::move(sub.decisionPoints),
                                 std::move(sub.warnings)};
        } else if(auto* inNode = dynamic_cast<const InNode*>(&astNode)) {
            if(!inNode->tableName.empty()) {
                auto operandResult = generateNode(*inNode->operand);
                operandResult.warnings.push_back("IN table-name is not supported in sqlite_orm codegen");
                return CodeGenResult{"/* " + operandResult.code + " IN " + inNode->tableName + " */",
                                     std::move(operandResult.decisionPoints),
                                     std::move(operandResult.warnings)};
            }
            if(inNode->subquerySelect) {
                auto operandResult = generateNode(*inNode->operand);
                auto sub = tryCodegenSelectLikeSubquery(*inNode->subquerySelect);
                auto decisionPoints = std::move(operandResult.decisionPoints);
                decisionPoints.insert(decisionPoints.end(),
                                      std::make_move_iterator(sub.decisionPoints.begin()),
                                      std::make_move_iterator(sub.decisionPoints.end()));
                std::vector<std::string> inSubWarnings;
                inSubWarnings.insert(inSubWarnings.end(),
                                     std::make_move_iterator(operandResult.warnings.begin()),
                                     std::make_move_iterator(operandResult.warnings.end()));
                inSubWarnings.insert(inSubWarnings.end(),
                                     std::make_move_iterator(sub.warnings.begin()),
                                     std::make_move_iterator(sub.warnings.end()));
                if(sub.code.empty()) {
                    inSubWarnings.push_back("IN (SELECT ...) is not mapped to sqlite_orm codegen");
                    return CodeGenResult{"/* IN (SELECT ...) */", std::move(decisionPoints),
                                         std::move(inSubWarnings)};
                }
                std::string code =
                    inNode->negated ? "not_in(" + operandResult.code + ", " + sub.code + ")"
                                    : "in(" + operandResult.code + ", " + sub.code + ")";
                return CodeGenResult{code, std::move(decisionPoints), std::move(inSubWarnings)};
            }
            auto operandResult = generateNode(*inNode->operand);
            auto decisionPoints = std::move(operandResult.decisionPoints);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(inNode->operand.get())) {
                if(!inNode->values.empty()) {
                    registerColumn(to_cpp_identifier(col->columnName), inferTypeFromNode(*inNode->values.at(0)));
                }
            }

            std::string valuesList;
            for(size_t i = 0; i < inNode->values.size(); ++i) {
                auto valueResult = generateNode(*inNode->values.at(i));
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(valueResult.decisionPoints.begin()),
                    std::make_move_iterator(valueResult.decisionPoints.end()));
                if(i > 0) valuesList += ", ";
                valuesList += valueResult.code;
            }

            std::string inCode = "in(" + operandResult.code + ", {" + valuesList + "})";
            if(inNode->negated) {
                std::string notInCode = "not_in(" + operandResult.code + ", {" + valuesList + "})";
                decisionPoints.push_back(DecisionPoint{
                    this->nextDecisionPointId++,
                    "negation_style",
                    "not_in",
                    notInCode,
                    {Alternative{"not_wrapper", "not_(" + inCode + ")", "use not_() wrapper"}}
                });
                return CodeGenResult{notInCode, std::move(decisionPoints)};
            }
            return CodeGenResult{inCode, std::move(decisionPoints)};
        } else if(auto* likeNode = dynamic_cast<const LikeNode*>(&astNode)) {
            auto operandResult = generateNode(*likeNode->operand);
            auto patternResult = generateNode(*likeNode->pattern);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(likeNode->operand.get())) {
                registerColumn(to_cpp_identifier(col->columnName), "std::string");
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                std::make_move_iterator(patternResult.decisionPoints.begin()),
                std::make_move_iterator(patternResult.decisionPoints.end()));

            std::string likeCode = "like(" + operandResult.code + ", " + patternResult.code;
            if(likeNode->escape) {
                auto escapeResult = generateNode(*likeNode->escape);
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(escapeResult.decisionPoints.begin()),
                    std::make_move_iterator(escapeResult.decisionPoints.end()));
                likeCode += ", " + escapeResult.code;
            }
            likeCode += ")";

            std::string code = likeNode->negated ? "not_(" + likeCode + ")" : likeCode;
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* globNode = dynamic_cast<const GlobNode*>(&astNode)) {
            auto operandResult = generateNode(*globNode->operand);
            auto patternResult = generateNode(*globNode->pattern);

            if(auto* col = dynamic_cast<const ColumnRefNode*>(globNode->operand.get())) {
                registerColumn(to_cpp_identifier(col->columnName), "std::string");
            }

            auto decisionPoints = std::move(operandResult.decisionPoints);
            decisionPoints.insert(decisionPoints.end(),
                std::make_move_iterator(patternResult.decisionPoints.begin()),
                std::make_move_iterator(patternResult.decisionPoints.end()));

            std::string globCode = "glob(" + operandResult.code + ", " + patternResult.code + ")";
            std::string code = globNode->negated ? "not_(" + globCode + ")" : globCode;
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* castNode = dynamic_cast<const CastNode*>(&astNode)) {
            auto operandResult = generateNode(*castNode->operand);
            std::string cppType = sqlite_type_to_cpp(castNode->typeName);
            return CodeGenResult{"cast<" + cppType + ">(" + operandResult.code + ")",
                                 std::move(operandResult.decisionPoints)};
        } else if(auto* caseNode = dynamic_cast<const CaseNode*>(&astNode)) {
            std::vector<DecisionPoint> decisionPoints;
            std::string returnType = "int";
            if(!caseNode->branches.empty()) {
                returnType = inferTypeFromNode(*caseNode->branches.at(0).result);
            }
            std::string code = "case_<" + returnType + ">(";
            if(caseNode->operand) {
                auto operandResult = generateNode(*caseNode->operand);
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(operandResult.decisionPoints.begin()),
                    std::make_move_iterator(operandResult.decisionPoints.end()));
                code += operandResult.code;
            }
            code += ")";
            for(auto& branch : caseNode->branches) {
                auto condResult = generateNode(*branch.condition);
                auto resResult = generateNode(*branch.result);
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(condResult.decisionPoints.begin()),
                    std::make_move_iterator(condResult.decisionPoints.end()));
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(resResult.decisionPoints.begin()),
                    std::make_move_iterator(resResult.decisionPoints.end()));
                code += ".when(" + condResult.code + ", then(" + resResult.code + "))";
            }
            if(caseNode->elseResult) {
                auto elseResult = generateNode(*caseNode->elseResult);
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(elseResult.decisionPoints.begin()),
                    std::make_move_iterator(elseResult.decisionPoints.end()));
                code += ".else_(" + elseResult.code + ")";
            }
            code += ".end()";
            return CodeGenResult{code, std::move(decisionPoints)};
        } else if(auto* bindParam = dynamic_cast<const BindParameterNode*>(&astNode)) {
            std::string paramStr(bindParam->value);
            std::string cppVar;
            if(paramStr.size() > 1 && (paramStr[0] == ':' || paramStr[0] == '@' || paramStr[0] == '$')) {
                cppVar = to_cpp_identifier(paramStr.substr(1));
            } else if(paramStr == "?") {
                cppVar = "bindParam" + std::to_string(++this->nextBindParamIndex);
            } else if(paramStr.size() > 1 && paramStr[0] == '?') {
                cppVar = "bindParam" + paramStr.substr(1);
            } else {
                cppVar = "/* " + paramStr + " */";
            }
            return CodeGenResult{cppVar, {},
                                 {"bind parameter " + paramStr + " -> C++ variable '" + cppVar +
                                  "'; for prepared statements use storage.prepare() + get<N>(stmt)"}};
        } else if(auto* collateNode = dynamic_cast<const CollateNode*>(&astNode)) {
            auto operandResult = generateNode(*collateNode->operand);
            operandResult.warnings.push_back("COLLATE " + collateNode->collationName +
                                              " on expressions is not directly supported in sqlite_orm codegen");
            return operandResult;
        } else if(auto* funcCall = dynamic_cast<const FunctionCallNode*>(&astNode)) {
            std::string funcName = toLowerAscii(funcCall->name);
            std::vector<DecisionPoint> decisionPoints;
            std::vector<std::string> funcWarnings;
            std::string baseCode;

            if(funcCall->star) {
                if(funcName == "count" && !this->fromTableAliasToStructName.empty()) {
                    baseCode = "count<" + this->structName + ">()";
                } else {
                    baseCode = funcName + "()";
                }
            } else {
                std::string argList;
                for(size_t i = 0; i < funcCall->arguments.size(); ++i) {
                    auto argResult = generateNode(*funcCall->arguments.at(i));
                    decisionPoints.insert(decisionPoints.end(),
                        std::make_move_iterator(argResult.decisionPoints.begin()),
                        std::make_move_iterator(argResult.decisionPoints.end()));
                    funcWarnings.insert(funcWarnings.end(),
                        std::make_move_iterator(argResult.warnings.begin()),
                        std::make_move_iterator(argResult.warnings.end()));
                    if(i > 0) argList += ", ";
                    argList += argResult.code;
                }
                if(funcCall->distinct && !argList.empty()) {
                    baseCode = funcName + "(distinct(" + argList + "))";
                } else {
                    baseCode = funcName + "(" + argList + ")";
                }
            }

            if(funcCall->filterWhere) {
                auto fr = generateNode(*funcCall->filterWhere);
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(fr.decisionPoints.begin()),
                    std::make_move_iterator(fr.decisionPoints.end()));
                funcWarnings.insert(funcWarnings.end(),
                    std::make_move_iterator(fr.warnings.begin()),
                    std::make_move_iterator(fr.warnings.end()));
                baseCode += ".filter(where(" + fr.code + "))";
            }
            if(funcCall->over) {
                std::string overArgs = codegenOverClause(*funcCall->over, decisionPoints, funcWarnings);
                if(overArgs.empty()) {
                    funcWarnings.push_back(
                        "OVER clause could not be mapped to sqlite_orm (.over(...)); emitted bare function only");
                } else {
                    baseCode += ".over(" + overArgs + ")";
                }
            }
            return CodeGenResult{std::move(baseCode), std::move(decisionPoints), std::move(funcWarnings)};
        } else if(auto* insertNode = dynamic_cast<const InsertNode*>(&astNode)) {
            std::vector<std::string> warnings;
            if(insertNode->schemaName) {
                warnings.push_back("schema-qualified table in INSERT is not represented in sqlite_orm mapping");
            }
            std::string tableStruct = to_struct_name(insertNode->tableName);
            std::string savedStruct = this->structName;
            this->structName = tableStruct;

            std::string verb = insertNode->replaceInto ? "replace" : "insert";
            std::string orPrefix = insertNode->replaceInto ? std::string() : dml_insert_or_prefix(insertNode->orConflict);

            std::vector<DecisionPoint> dps;
            std::string middle;
            if(insertNode->dataKind == InsertDataKind::defaultValues) {
                middle = "default_values()";
            } else if(insertNode->dataKind == InsertDataKind::values) {
                std::string cols = "columns(";
                for(size_t i = 0; i < insertNode->columnNames.size(); ++i) {
                    if(i > 0) {
                        cols += ", ";
                    }
                    cols += "&" + tableStruct + "::" + to_cpp_identifier(insertNode->columnNames[i]);
                }
                cols += ")";
                std::string vals = "values(";
                for(size_t r = 0; r < insertNode->valueRows.size(); ++r) {
                    if(r > 0) {
                        vals += ", ";
                    }
                    vals += "std::make_tuple(";
                    const auto& row = insertNode->valueRows[r];
                    for(size_t columnIndex = 0; columnIndex < row.size(); ++columnIndex) {
                        if(columnIndex > 0) {
                            vals += ", ";
                        }
                        auto cell = generateNode(*row[columnIndex]);
                        dps.insert(dps.end(),
                                   std::make_move_iterator(cell.decisionPoints.begin()),
                                   std::make_move_iterator(cell.decisionPoints.end()));
                        warnings.insert(warnings.end(),
                                         std::make_move_iterator(cell.warnings.begin()),
                                         std::make_move_iterator(cell.warnings.end()));
                        vals += cell.code;
                    }
                    vals += ")";
                }
                vals += ")";
                middle = cols + ", " + vals;
            } else {
                auto sub = tryCodegenSelectLikeSubquery(*insertNode->selectStatement);
                warnings.insert(warnings.end(),
                                std::make_move_iterator(sub.warnings.begin()),
                                std::make_move_iterator(sub.warnings.end()));
                dps.insert(dps.end(),
                           std::make_move_iterator(sub.decisionPoints.begin()),
                           std::make_move_iterator(sub.decisionPoints.end()));
                if(sub.code.empty()) {
                    this->structName = savedStruct;
                    return CodeGenResult{"/* INSERT ... SELECT: inner SELECT not mapped to sqlite_orm */",
                                         std::move(dps), std::move(warnings)};
                }
                if(!insertNode->columnNames.empty()) {
                    std::string cols = "columns(";
                    for(size_t i = 0; i < insertNode->columnNames.size(); ++i) {
                        if(i > 0) {
                            cols += ", ";
                        }
                        cols += "&" + tableStruct + "::" + to_cpp_identifier(insertNode->columnNames[i]);
                    }
                    cols += ")";
                    middle = cols + ", " + sub.code;
                } else {
                    middle = sub.code;
                }
            }

            std::string upsertSuffix;
            if(insertNode->hasUpsertClause) {
                if(insertNode->upsertConflictWhere) {
                    warnings.push_back(
                        "ON CONFLICT target WHERE is not represented in sqlite_orm on_conflict(); generated code "
                        "omits that predicate");
                }
                std::string onTarget;
                if(insertNode->upsertConflictColumns.empty()) {
                    onTarget = "on_conflict()";
                } else if(insertNode->upsertConflictColumns.size() == 1u) {
                    onTarget = "on_conflict(&" + tableStruct + "::" +
                               to_cpp_identifier(insertNode->upsertConflictColumns[0]) + ")";
                } else {
                    onTarget = "on_conflict(columns(";
                    for(size_t ui = 0; ui < insertNode->upsertConflictColumns.size(); ++ui) {
                        if(ui > 0) {
                            onTarget += ", ";
                        }
                        onTarget += "&" + tableStruct + "::" + to_cpp_identifier(insertNode->upsertConflictColumns[ui]);
                    }
                    onTarget += "))";
                }
                if(insertNode->upsertAction == InsertUpsertAction::doNothing) {
                    upsertSuffix = ", " + onTarget + ".do_nothing()";
                } else if(insertNode->upsertAction == InsertUpsertAction::doUpdate) {
                    std::string setArgs;
                    for(size_t assignmentIndex = 0; assignmentIndex < insertNode->upsertUpdateAssignments.size(); ++assignmentIndex) {
                        if(assignmentIndex > 0) {
                            setArgs += ", ";
                        }
                        auto valueResult = generateNode(*insertNode->upsertUpdateAssignments[assignmentIndex].value);
                        dps.insert(dps.end(),
                                   std::make_move_iterator(valueResult.decisionPoints.begin()),
                                   std::make_move_iterator(valueResult.decisionPoints.end()));
                        warnings.insert(warnings.end(),
                                         std::make_move_iterator(valueResult.warnings.begin()),
                                         std::make_move_iterator(valueResult.warnings.end()));
                        std::string cppCol = to_cpp_identifier(insertNode->upsertUpdateAssignments[assignmentIndex].columnName);
                        setArgs += "c(&" + tableStruct + "::" + cppCol + ") = " + valueResult.code;
                    }
                    upsertSuffix = ", " + onTarget + ".do_update(set(" + setArgs + ")";
                    if(insertNode->upsertUpdateWhere) {
                        auto whereResult = generateNode(*insertNode->upsertUpdateWhere);
                        dps.insert(dps.end(),
                                   std::make_move_iterator(whereResult.decisionPoints.begin()),
                                   std::make_move_iterator(whereResult.decisionPoints.end()));
                        warnings.insert(warnings.end(),
                                         std::make_move_iterator(whereResult.warnings.begin()),
                                         std::make_move_iterator(whereResult.warnings.end()));
                        upsertSuffix += ", where(" + whereResult.code + ")";
                    }
                    upsertSuffix += ")";
                }
            }

            this->structName = savedStruct;
            std::string code =
                "storage." + verb + "(" + orPrefix + "into<" + tableStruct + ">(), " + middle + upsertSuffix + ");";
            if(insertNode->replaceInto) {
                std::string insertOrReplace =
                    "storage.insert(or_replace(), into<" + tableStruct + ">(), " + middle + upsertSuffix + ");";
                dps.push_back(DecisionPoint{
                    this->nextDecisionPointId++,
                    "replace_style",
                    "replace_call",
                    code,
                    {Alternative{"insert_or_replace",
                                 insertOrReplace,
                                 "same semantics via raw insert(or_replace(), into<T>(), ...)"}}});
            }
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if(auto* updateNode = dynamic_cast<const UpdateNode*>(&astNode)) {
            std::vector<std::string> warnings;
            if(updateNode->schemaName) {
                warnings.push_back("schema-qualified table in UPDATE is not represented in sqlite_orm mapping");
            }
            if(updateNode->orConflict != ConflictClause::none) {
                warnings.push_back(
                    "UPDATE OR modifier is not represented in sqlite_orm; generated code uses update_all(...) without OR");
            }
            if(!updateNode->fromClause.empty()) {
                warnings.push_back("UPDATE ... FROM ... is not supported in sqlite_orm — "
                                   "FROM clause is ignored in codegen");
            }
            std::string tableStruct = to_struct_name(updateNode->tableName);
            std::string savedStruct = this->structName;
            this->structName = tableStruct;
            std::vector<DecisionPoint> dps;
            std::string setArgs;
            for(size_t assignmentIndex = 0; assignmentIndex < updateNode->assignments.size(); ++assignmentIndex) {
                if(assignmentIndex > 0) {
                    setArgs += ", ";
                }
                auto valueResult = generateNode(*updateNode->assignments[assignmentIndex].value);
                dps.insert(dps.end(),
                           std::make_move_iterator(valueResult.decisionPoints.begin()),
                           std::make_move_iterator(valueResult.decisionPoints.end()));
                warnings.insert(warnings.end(),
                               std::make_move_iterator(valueResult.warnings.begin()),
                               std::make_move_iterator(valueResult.warnings.end()));
                std::string cppCol = to_cpp_identifier(updateNode->assignments[assignmentIndex].columnName);
                setArgs += "c(&" + tableStruct + "::" + cppCol + ") = " + valueResult.code;
            }
            std::string code = "storage.update_all(set(" + setArgs + ")";
            if(updateNode->whereClause) {
                auto whereResult = generateNode(*updateNode->whereClause);
                dps.insert(dps.end(),
                           std::make_move_iterator(whereResult.decisionPoints.begin()),
                           std::make_move_iterator(whereResult.decisionPoints.end()));
                warnings.insert(warnings.end(),
                               std::make_move_iterator(whereResult.warnings.begin()),
                               std::make_move_iterator(whereResult.warnings.end()));
                code += ", where(" + whereResult.code + ")";
            }
            code += ");";
            this->structName = savedStruct;
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if(auto* deleteNode = dynamic_cast<const DeleteNode*>(&astNode)) {
            std::vector<std::string> warnings;
            if(deleteNode->schemaName) {
                warnings.push_back("schema-qualified table in DELETE is not represented in sqlite_orm mapping");
            }
            std::string tableStruct = to_struct_name(deleteNode->tableName);
            std::string savedStruct = this->structName;
            this->structName = tableStruct;
            std::vector<DecisionPoint> dps;
            std::string code = "storage.remove_all<" + tableStruct + ">()";
            if(deleteNode->whereClause) {
                auto whereResult = generateNode(*deleteNode->whereClause);
                dps.insert(dps.end(),
                           std::make_move_iterator(whereResult.decisionPoints.begin()),
                           std::make_move_iterator(whereResult.decisionPoints.end()));
                warnings.insert(warnings.end(),
                               std::make_move_iterator(whereResult.warnings.begin()),
                               std::make_move_iterator(whereResult.warnings.end()));
                code = "storage.remove_all<" + tableStruct + ">(where(" + whereResult.code + "))";
            }
            code += ";";
            this->structName = savedStruct;
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if(auto* createTable = dynamic_cast<const CreateTableNode*>(&astNode)) {
            const CreateTableParts parts = this->createTableParts(*createTable);
            std::string code = parts.structDeclaration + "\nauto storage = make_storage(\"\",\n    " +
                               parts.makeTableExpression + ");";
            return CodeGenResult{std::move(code), {}, std::vector<std::string>(parts.warnings)};
        } else if(auto* createTrigger = dynamic_cast<const CreateTriggerNode*>(&astNode)) {
            std::vector<std::string> warnings;
            if(createTrigger->ifNotExists) {
                warnings.push_back(
                    "CREATE TRIGGER IF NOT EXISTS is not represented in sqlite_orm make_trigger(); generated code "
                    "omits IF NOT EXISTS");
            }
            if(createTrigger->temporary) {
                warnings.push_back(
                    "TEMP/TEMPORARY TRIGGER is not represented in sqlite_orm make_trigger(); generated code does not "
                    "mark the trigger as temporary");
            }
            if(createTrigger->triggerSchemaName) {
                warnings.push_back(
                    "schema-qualified trigger name is not represented in sqlite_orm; generated code uses unqualified "
                    "trigger name only");
            }
            if(createTrigger->tableSchemaName) {
                warnings.push_back(
                    "schema-qualified ON table in TRIGGER is not represented in sqlite_orm mapping");
            }

            std::string subject = to_struct_name(createTrigger->tableName);
            std::string savedStruct = this->structName;
            this->structName = subject;

            std::string timingHead;
            switch(createTrigger->timing) {
            case TriggerTiming::before: timingHead = "before()"; break;
            case TriggerTiming::after: timingHead = "after()"; break;
            case TriggerTiming::insteadOf: timingHead = "instead_of()"; break;
            }

            std::string typeChain = timingHead;
            switch(createTrigger->eventKind) {
            case TriggerEventKind::delete_: typeChain += ".delete_()"; break;
            case TriggerEventKind::insert_: typeChain += ".insert()"; break;
            case TriggerEventKind::update_: typeChain += ".update()"; break;
            case TriggerEventKind::updateOf:
                typeChain += ".update_of(";
                for(size_t ci = 0; ci < createTrigger->updateOfColumns.size(); ++ci) {
                    if(ci > 0) {
                        typeChain += ", ";
                    }
                    typeChain += "&" + subject + "::" + to_cpp_identifier(createTrigger->updateOfColumns[ci]);
                }
                typeChain += ")";
                break;
            }

            std::string base = typeChain + ".on<" + subject + ">()";
            if(createTrigger->forEachRow) {
                base += ".for_each_row()";
            }
            std::vector<DecisionPoint> dps;
            if(createTrigger->whenClause) {
                auto whenResult = generateNode(*createTrigger->whenClause);
                dps.insert(dps.end(), std::make_move_iterator(whenResult.decisionPoints.begin()),
                           std::make_move_iterator(whenResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(whenResult.warnings.begin()),
                               std::make_move_iterator(whenResult.warnings.end()));
                base += ".when(" + whenResult.code + ")";
            }

            std::string stepsJoined;
            for(const auto& step : createTrigger->bodyStatements) {
                auto sr = generateTriggerStep(*step, subject);
                dps.insert(dps.end(), std::make_move_iterator(sr.decisionPoints.begin()),
                           std::make_move_iterator(sr.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(sr.warnings.begin()),
                               std::make_move_iterator(sr.warnings.end()));
                if(!stepsJoined.empty()) {
                    stepsJoined += ", ";
                }
                stepsJoined += sr.code;
            }

            this->structName = savedStruct;

            std::string trigLit = identifier_to_cpp_string_literal(createTrigger->triggerName);
            std::string code = "make_trigger(" + trigLit + ", " + base + ".begin(" + stepsJoined + "));";
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if(auto* createIndex = dynamic_cast<const CreateIndexNode*>(&astNode)) {
            std::vector<std::string> warnings;
            if(createIndex->indexSchemaName) {
                warnings.push_back(
                    "schema-qualified INDEX name is not represented in sqlite_orm; generated code uses unqualified "
                    "index name");
            }
            if(createIndex->tableSchemaName) {
                warnings.push_back(
                    "schema-qualified table in CREATE INDEX is not represented in sqlite_orm mapping");
            }
            if(!createIndex->ifNotExists) {
                warnings.push_back(
                    "sqlite_orm serializes indexes as CREATE INDEX IF NOT EXISTS; SQL without IF NOT EXISTS differs "
                    "from serialized output");
            }

            std::string tableStruct = to_struct_name(createIndex->tableName);
            std::string savedStruct = this->structName;
            this->structName = tableStruct;

            std::string fn = createIndex->unique ? "make_unique_index" : "make_index";
            std::string idxLit = identifier_to_cpp_string_literal(createIndex->indexName);
            std::vector<DecisionPoint> dps;
            std::string colParts;
            for(const auto& indexedColumn : createIndex->indexedColumns) {
                if(!colParts.empty()) {
                    colParts += ", ";
                }
                auto expressionResult = generateNode(*indexedColumn.expression);
                dps.insert(dps.end(), std::make_move_iterator(expressionResult.decisionPoints.begin()),
                           std::make_move_iterator(expressionResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(expressionResult.warnings.begin()),
                               std::make_move_iterator(expressionResult.warnings.end()));
                std::string part = "indexed_column(" + expressionResult.code + ")";
                if(!indexedColumn.collation.empty()) {
                    std::string collLower = toLowerAscii(indexedColumn.collation);
                    if(collLower == "nocase") {
                        part += ".collate(\"nocase\")";
                    } else if(collLower == "binary") {
                        part += ".collate(\"binary\")";
                    } else if(collLower == "rtrim") {
                        part += ".collate(\"rtrim\")";
                    } else {
                        warnings.push_back("COLLATE " + indexedColumn.collation +
                                             " is not a built-in collation; generated .collate(...) uses literal "
                                             "name as in SQL");
                        part += ".collate(" + identifier_to_cpp_string_literal(indexedColumn.collation) + ")";
                    }
                }
                if(indexedColumn.sortDirection == SortDirection::asc) {
                    part += ".asc()";
                } else if(indexedColumn.sortDirection == SortDirection::desc) {
                    part += ".desc()";
                }
                colParts += part;
            }

            std::string code = fn + "(" + idxLit + ", " + colParts;
            if(createIndex->whereClause) {
                auto whereResult = generateNode(*createIndex->whereClause);
                dps.insert(dps.end(), std::make_move_iterator(whereResult.decisionPoints.begin()),
                           std::make_move_iterator(whereResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(whereResult.warnings.begin()),
                               std::make_move_iterator(whereResult.warnings.end()));
                code += ", where(" + whereResult.code + ")";
            }
            code += ");";

            this->structName = savedStruct;
            return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
        } else if(auto* transactionControlNode = dynamic_cast<const TransactionControlNode*>(&astNode)) {
            if(transactionControlNode->kind == TransactionControlNode::Kind::begin) {
                switch(transactionControlNode->beginMode) {
                    case BeginTransactionMode::deferred:
                        return CodeGenResult{"storage.begin_deferred_transaction();", {}, {}};
                    case BeginTransactionMode::immediate:
                        return CodeGenResult{"storage.begin_immediate_transaction();", {}, {}};
                    case BeginTransactionMode::exclusive:
                        return CodeGenResult{"storage.begin_exclusive_transaction();", {}, {}};
                    case BeginTransactionMode::plain:
                    default:
                        return CodeGenResult{"storage.begin_transaction();", {}, {}};
                }
            }
            if(transactionControlNode->kind == TransactionControlNode::Kind::commit) {
                return CodeGenResult{"storage.commit();", {}, {}};
            }
            if(transactionControlNode->rollbackToSavepoint) {
                return CodeGenResult{"/* ROLLBACK TO SAVEPOINT */",
                                     {},
                                     {"ROLLBACK TO SAVEPOINT is not supported in the sqlite_orm storage API"}};
            }
            return CodeGenResult{"storage.rollback();", {}, {}};
        } else if(auto* vacuumStatementNode = dynamic_cast<const VacuumStatementNode*>(&astNode)) {
            if(vacuumStatementNode->schemaName) {
                return CodeGenResult{
                    "storage.vacuum();",
                    {},
                    {"VACUUM with explicit schema is not modeled separately in sqlite_orm::storage_base::vacuum()"}};
            }
            return CodeGenResult{"storage.vacuum();", {}, {}};
        } else if(auto* dropStatementNode = dynamic_cast<const DropStatementNode*>(&astNode)) {
            std::vector<std::string> warnings;
            if(dropStatementNode->schemaName) {
                warnings.push_back(
                    "schema-qualified name in DROP is not represented in sqlite_orm; generated call uses unqualified "
                    "name only");
            }
            const std::string lit = identifier_to_cpp_string_literal(dropStatementNode->objectName);
            switch(dropStatementNode->objectKind) {
                case DropObjectKind::table:
                    return CodeGenResult{
                        (dropStatementNode->ifExists ? "storage.drop_table_if_exists(" : "storage.drop_table(") + lit +
                            ");",
                        {},
                        std::move(warnings)};
                case DropObjectKind::index:
                    return CodeGenResult{
                        (dropStatementNode->ifExists ? "storage.drop_index_if_exists(" : "storage.drop_index(") + lit +
                            ");",
                        {},
                        std::move(warnings)};
                case DropObjectKind::trigger:
                    return CodeGenResult{(dropStatementNode->ifExists ? "storage.drop_trigger_if_exists("
                                                                       : "storage.drop_trigger(") +
                                             lit + ");",
                                         {},
                                         std::move(warnings)};
                case DropObjectKind::view:
                    warnings.push_back(
                        "DROP VIEW is not supported as a sqlite_orm storage method; sqlite_orm sync_schema() applies "
                        "to mapped tables/indexes/triggers, not views");
                    return CodeGenResult{"/* DROP VIEW: not supported as storage.drop_* in sqlite_orm */",
                                         {},
                                         std::move(warnings)};
                default:
                    return CodeGenResult{"/* DROP */", {}, std::move(warnings)};
            }
        } else if(auto* createVirtualTableNode = dynamic_cast<const CreateVirtualTableNode*>(&astNode)) {
            std::vector<std::string> warnings;
            std::vector<DecisionPoint> dps;

            if(createVirtualTableNode->tableSchemaName) {
                warnings.push_back(
                    "schema-qualified VIRTUAL TABLE name is not represented in sqlite_orm; generated code uses "
                    "unqualified table name only");
            }
            if(!createVirtualTableNode->ifNotExists) {
                warnings.push_back(
                    "sqlite_orm serializes virtual tables as CREATE VIRTUAL TABLE IF NOT EXISTS; SQL without IF NOT "
                    "EXISTS differs from serialized output");
            }
            if(createVirtualTableNode->temporary) {
                warnings.push_back(
                    "TEMP/TEMPORARY VIRTUAL TABLE is not represented in sqlite_orm virtual table mapping; generated "
                    "code does not mark the table as temporary");
            }

            std::string mod = toLowerAscii(createVirtualTableNode->moduleName);
            std::string nameLit = identifier_to_cpp_string_literal(createVirtualTableNode->tableName);
            std::string sName = to_struct_name(createVirtualTableNode->tableName);

            auto all_simple_column_refs = [&]() -> bool {
                for(const auto& moduleArgument : createVirtualTableNode->moduleArguments) {
                    if(!dynamic_cast<const ColumnRefNode*>(moduleArgument.get())) {
                        return false;
                    }
                }
                return true;
            };

            if(mod == "fts5") {
                if(createVirtualTableNode->moduleArguments.empty()) {
                    warnings.push_back("FTS5 requires at least one column argument for sqlite_orm::using_fts5()");
                    return CodeGenResult{"/* CREATE VIRTUAL TABLE: fts5 (no columns) */", std::move(dps),
                                         std::move(warnings)};
                }
                if(!all_simple_column_refs()) {
                    warnings.push_back(
                        "FTS5 module arguments that are not plain column names cannot be mapped to "
                        "sqlite_orm::using_fts5()");
                    return CodeGenResult{"/* CREATE VIRTUAL TABLE: fts5 (unmapped arguments) */", std::move(dps),
                                         std::move(warnings)};
                }
                std::string code = "struct " + sName + " {\n";
                for(const auto& moduleArgument : createVirtualTableNode->moduleArguments) {
                    auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                    auto cppName = to_cpp_identifier(columnRef->columnName);
                    code += "    std::string " + cppName + ";\n";
                }
                code += "};\n\n";
                std::string savedStruct = this->structName;
                this->structName = sName;
                std::string colParts;
                for(const auto& moduleArgument : createVirtualTableNode->moduleArguments) {
                    auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                    CodeGenResult moduleArgumentCodegen = generateNode(*moduleArgument);
                    dps.insert(dps.end(),
                               std::make_move_iterator(moduleArgumentCodegen.decisionPoints.begin()),
                               std::make_move_iterator(moduleArgumentCodegen.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(moduleArgumentCodegen.warnings.begin()),
                                    std::make_move_iterator(moduleArgumentCodegen.warnings.end()));
                    if(!colParts.empty()) {
                        colParts += ", ";
                    }
                    std::string rawCol = strip_identifier_quotes(columnRef->columnName);
                    colParts +=
                        "make_column(" + identifier_to_cpp_string_literal(rawCol) + ", " + moduleArgumentCodegen.code + ")";
                }
                this->structName = savedStruct;
                code += "auto vtab = make_virtual_table<" + sName + ">(" + nameLit + ", using_fts5(" + colParts +
                        "));\n";
                return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
            }

            if(mod == "rtree" || mod == "rtree_i32") {
                const size_t moduleArgumentsCount = createVirtualTableNode->moduleArguments.size();
                if(moduleArgumentsCount < 3 || moduleArgumentsCount > 11 || (moduleArgumentsCount % 2 == 0)) {
                    warnings.push_back(
                        "RTREE virtual table for sqlite_orm needs 3, 5, 7, 9, or 11 simple column identifiers (id + "
                        "min/max pairs)");
                    return CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (invalid column count) */", std::move(dps),
                                         std::move(warnings)};
                }
                if(!all_simple_column_refs()) {
                    warnings.push_back(
                        "RTREE module arguments that are not plain column names cannot be mapped to sqlite_orm "
                        "using_rtree() / using_rtree_i32()");
                    return CodeGenResult{"/* CREATE VIRTUAL TABLE: rtree (unmapped arguments) */", std::move(dps),
                                         std::move(warnings)};
                }
                const bool i32 = (mod == "rtree_i32");
                std::string code = "struct " + sName + " {\n";
                for(size_t i = 0; i < moduleArgumentsCount; ++i) {
                    auto* columnRef =
                        static_cast<const ColumnRefNode*>(createVirtualTableNode->moduleArguments[i].get());
                    auto cppName = to_cpp_identifier(columnRef->columnName);
                    if(i == 0) {
                        code += "    int64_t " + cppName + " = 0;\n";
                    } else if(i32) {
                        code += "    int32_t " + cppName + " = 0;\n";
                    } else {
                        code += "    float " + cppName + " = 0.0;\n";
                    }
                }
                code += "};\n\n";
                std::string savedStruct = this->structName;
                this->structName = sName;
                std::string colParts;
                for(const auto& moduleArgument : createVirtualTableNode->moduleArguments) {
                    auto* columnRef = static_cast<const ColumnRefNode*>(moduleArgument.get());
                    CodeGenResult moduleArgumentCodegen = generateNode(*moduleArgument);
                    dps.insert(dps.end(),
                               std::make_move_iterator(moduleArgumentCodegen.decisionPoints.begin()),
                               std::make_move_iterator(moduleArgumentCodegen.decisionPoints.end()));
                    warnings.insert(warnings.end(),
                                    std::make_move_iterator(moduleArgumentCodegen.warnings.begin()),
                                    std::make_move_iterator(moduleArgumentCodegen.warnings.end()));
                    if(!colParts.empty()) {
                        colParts += ", ";
                    }
                    std::string rawCol = strip_identifier_quotes(columnRef->columnName);
                    colParts +=
                        "make_column(" + identifier_to_cpp_string_literal(rawCol) + ", " + moduleArgumentCodegen.code + ")";
                }
                this->structName = savedStruct;
                const char* usingFn = i32 ? "using_rtree_i32" : "using_rtree";
                code += "auto vtab = make_virtual_table<" + sName + ">(" + nameLit + ", " + usingFn + "(" + colParts +
                        "));\n";
                return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
            }

            if(mod == "generate_series") {
                if(!createVirtualTableNode->moduleArguments.empty()) {
                    warnings.push_back(
                        "generate_series module arguments are not mapped to sqlite_orm; expected empty argument list "
                        "for make_virtual_table<generate_series>(..., internal::using_generate_series())");
                    return CodeGenResult{"/* CREATE VIRTUAL TABLE: generate_series (unmapped arguments) */",
                                         std::move(dps), std::move(warnings)};
                }
                std::string code = "auto vtab = make_virtual_table<generate_series>(" + nameLit +
                                   ", internal::using_generate_series());\n";
                return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
            }

            if(mod == "dbstat") {
                if(createVirtualTableNode->moduleArguments.size() > 1) {
                    warnings.push_back(
                        "dbstat accepts at most one optional schema string argument for sqlite_orm::using_dbstat()");
                    return CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (too many arguments) */", std::move(dps),
                                         std::move(warnings)};
                }
                if(createVirtualTableNode->moduleArguments.empty()) {
                    std::string code =
                        "auto vtab = make_virtual_table<dbstat>(" + nameLit + ", using_dbstat());\n";
                    return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
                }
                if(auto* sl = dynamic_cast<const StringLiteralNode*>(createVirtualTableNode->moduleArguments[0].get())) {
                    std::string code = "auto vtab = make_virtual_table<dbstat>(" + nameLit + ", using_dbstat(" +
                                       sql_string_to_cpp(sl->value) + "));\n";
                    return CodeGenResult{std::move(code), std::move(dps), std::move(warnings)};
                }
                warnings.push_back(
                    "dbstat optional argument should be a SQL string literal for sqlite_orm::using_dbstat(\"...\")");
                return CodeGenResult{"/* CREATE VIRTUAL TABLE: dbstat (unmapped argument) */", std::move(dps),
                                     std::move(warnings)};
            }

            warnings.push_back("virtual table module \"" + std::string(createVirtualTableNode->moduleName) +
                               "\" has no sqlite_orm mapping in sqlite2orm codegen");
            return CodeGenResult{"/* CREATE VIRTUAL TABLE: unknown module */", std::move(dps), std::move(warnings)};
        }
        if(auto* withQueryNode = dynamic_cast<const WithQueryNode*>(&astNode)) {
            return generateWithQuery(*withQueryNode);
        }
        if(auto* compoundSelectNode = dynamic_cast<const CompoundSelectNode*>(&astNode)) {
            auto inner = tryCodegenCompoundSelectSubexpression(*compoundSelectNode);
            std::vector<std::string> compoundWarnings = std::move(inner.warnings);
            if(inner.code.empty()) {
                compoundWarnings.insert(compoundWarnings.begin(),
                                        "compound SELECT (UNION / INTERSECT / EXCEPT) is not mapped to sqlite_orm "
                                        "codegen");
                return CodeGenResult{"/* compound SELECT */", std::move(inner.decisionPoints),
                                     std::move(compoundWarnings)};
            }
            return CodeGenResult{"auto rows = storage.select(" + inner.code + ");",
                                 std::move(inner.decisionPoints), std::move(compoundWarnings)};
        }
        if(auto* selectNode = dynamic_cast<const SelectNode*>(&astNode)) {
            std::vector<std::string> selectWarnings;
            std::vector<DecisionPoint> selectDecisionPoints;
            for(const auto& fromItem : selectNode->fromClause) {
                if(fromItem.table.derivedSelect) {
                    selectWarnings.push_back("subselect in FROM is not supported in sqlite_orm codegen");
                    return CodeGenResult{"/* SELECT with derived FROM */", std::move(selectDecisionPoints),
                                         std::move(selectWarnings)};
                }
            }
            this->fromTableAliasToStructName.clear();
            auto structForFromTable = [&](std::string_view tableSqlName) -> std::string {
                auto k = normalize_table_key(tableSqlName);
                if(auto cteLookup = this->activeCteTypedefByTableKey.find(k);
                   cteLookup != this->activeCteTypedefByTableKey.end()) {
                    return cteLookup->second;
                }
                return to_struct_name(tableSqlName);
            };
            if(!selectNode->fromClause.empty()) {
                for(const auto& fromItem : selectNode->fromClause) {
                    const auto& ft = fromItem.table;
                    if(ft.schemaName) {
                        selectWarnings.push_back(
                            "FROM clause schema qualifier '" + *ft.schemaName + "' for table '" + ft.tableName +
                            "' is not represented in sqlite_orm mapping");
                    }
                    std::string mappedStructName = structForFromTable(ft.tableName);
                    this->fromTableAliasToStructName[ft.tableName] = mappedStructName;
                    if(ft.alias) {
                        this->fromTableAliasToStructName[*ft.alias] = mappedStructName;
                    }
                }
                this->structName = structForFromTable(selectNode->fromClause.at(0).table.tableName);
            }

            std::optional<std::string> implicitCte;
            if(selectNode->fromClause.size() == 1u) {
                auto k = normalize_table_key(selectNode->fromClause.at(0).table.tableName);
                if(auto it = this->activeCteTypedefByTableKey.find(k);
                   it != this->activeCteTypedefByTableKey.end()) {
                    implicitCte = it->second;
                }
            }
            struct ImplicitCteScope {
                CodeGenerator* gen;
                std::optional<std::string> saved;
                ImplicitCteScope(CodeGenerator* g, std::optional<std::string> impl)
                    : gen(g), saved(std::move(g->implicitSingleSourceCteTypedef)) {
                    gen->implicitSingleSourceCteTypedef = std::move(impl);
                }
                ~ImplicitCteScope() { gen->implicitSingleSourceCteTypedef = std::move(saved); }
            } implicitScope{this, std::move(implicitCte)};

            auto expressionCode = [&](const AstNode& node) -> std::string {
                auto result = generateNode(node);
                selectWarnings.insert(selectWarnings.end(),
                                      std::make_move_iterator(result.warnings.begin()),
                                      std::make_move_iterator(result.warnings.end()));
                selectDecisionPoints.insert(selectDecisionPoints.end(),
                                            std::make_move_iterator(result.decisionPoints.begin()),
                                            std::make_move_iterator(result.decisionPoints.end()));
                return result.code;
            };

            bool isStar = selectNode->columns.size() == 1 && !selectNode->columns.at(0).expression;
            int apiLevelDecisionId = -1;
            if(isStar && !selectNode->fromClause.empty()) {
                apiLevelDecisionId = this->nextDecisionPointId++;
            }
            std::string code;
            if(!isStar) {
                code = "auto rows = storage.select(";
                if(selectNode->distinct) {
                    if(selectNode->columns.size() == 1) {
                        auto colCode = expressionCode(*selectNode->columns.at(0).expression);
                        code += "distinct(" + colCode + ")";
                    } else {
                        code += "distinct(columns(";
                        for(size_t i = 0; i < selectNode->columns.size(); ++i) {
                            if(i > 0) code += ", ";
                            code += expressionCode(*selectNode->columns.at(i).expression);
                        }
                        code += "))";
                    }
                } else if(selectNode->columns.size() == 1) {
                    code += expressionCode(*selectNode->columns.at(0).expression);
                } else {
                    code += "columns(";
                    for(size_t i = 0; i < selectNode->columns.size(); ++i) {
                        if(i > 0) code += ", ";
                        code += expressionCode(*selectNode->columns.at(i).expression);
                    }
                    code += ")";
                }
            }

            std::vector<std::string> selectTrailingClauses;
            auto appendClause = [&](const std::string& clause) { selectTrailingClauses.push_back(clause); };

            for(size_t joinIndex = 1; joinIndex < selectNode->fromClause.size(); ++joinIndex) {
                const auto& joinItem = selectNode->fromClause.at(joinIndex);
                const auto& leftTable = selectNode->fromClause.at(joinIndex - 1).table;
                std::string rightStruct = structForFromTable(joinItem.table.tableName);
                std::string leftStruct = structForFromTable(leftTable.tableName);
                std::string joinCode;
                switch(joinItem.leadingJoin) {
                case JoinKind::crossJoin:
                case JoinKind::naturalInnerJoin:
                    joinCode = std::string(join_sqlite_orm_api_name(joinItem.leadingJoin)) + "<" + rightStruct + ">()";
                    break;
                case JoinKind::naturalLeftJoin:
                    selectWarnings.push_back(
                        "NATURAL LEFT JOIN is not supported in sqlite_orm; generated natural_join does not match SQL "
                        "semantics");
                    joinCode = "natural_join<" + rightStruct + ">()";
                    break;
                default: {
                    std::string api(join_sqlite_orm_api_name(joinItem.leadingJoin));
                    if(!joinItem.usingColumnNames.empty()) {
                        if(joinItem.usingColumnNames.size() == 1) {
                            joinCode = std::string(api) + "<" + rightStruct + ">(using_(&" + rightStruct + "::" +
                                       to_cpp_identifier(joinItem.usingColumnNames.at(0)) + "))";
                        } else {
                            std::string cond;
                            for(size_t ci = 0; ci < joinItem.usingColumnNames.size(); ++ci) {
                                if(ci > 0) {
                                    cond += " and ";
                                }
                                auto col = to_cpp_identifier(joinItem.usingColumnNames.at(ci));
                                cond += "c(&" + leftStruct + "::" + col + ") == c(&" + rightStruct + "::" + col + ")";
                            }
                            joinCode = std::string(api) + "<" + rightStruct + ">(on(" + cond + "))";
                        }
                    } else if(joinItem.onExpression) {
                        joinCode = std::string(api) + "<" + rightStruct + ">(on(" +
                                   expressionCode(*joinItem.onExpression) + "))";
                    } else {
                        joinCode = std::string(api) + "<" + rightStruct + ">(on(true))";
                    }
                    break;
                }
                }
                appendClause(joinCode);
            }

            if(selectNode->whereClause) {
                appendClause("where(" + expressionCode(*selectNode->whereClause) + ")");
            }

            if(selectNode->groupBy) {
                std::string groupCode = "group_by(";
                for(size_t i = 0; i < selectNode->groupBy->expressions.size(); ++i) {
                    if(i > 0) groupCode += ", ";
                    groupCode += expressionCode(*selectNode->groupBy->expressions.at(i));
                }
                groupCode += ")";
                if(selectNode->groupBy->having) {
                    groupCode += ".having(" + expressionCode(*selectNode->groupBy->having) + ")";
                }
                appendClause(groupCode);
            }

            for(const auto& namedWindow : selectNode->namedWindows) {
                if(!namedWindow.definition) {
                    continue;
                }
                std::string windowArgs =
                    codegenOverClause(*namedWindow.definition, selectDecisionPoints, selectWarnings);
                if(!windowArgs.empty()) {
                    appendClause("window(" + identifier_to_cpp_string_literal(namedWindow.name) + ", " + windowArgs +
                                 ")");
                } else {
                    selectWarnings.push_back(
                        "WINDOW `" + namedWindow.name +
                        "`: empty or unmapped window definition omitted in sqlite_orm codegen");
                }
            }

            if(!selectNode->orderBy.empty()) {
                auto formatOrderTerm = [&](const OrderByTerm& term) -> std::string {
                    std::string orderCode = "order_by(" + expressionCode(*term.expression) + ")";
                    if(term.direction == SortDirection::asc) {
                        orderCode += ".asc()";
                    } else if(term.direction == SortDirection::desc) {
                        orderCode += ".desc()";
                    }
                    if(!term.collation.empty()) {
                        std::string collLower = toLowerAscii(term.collation);
                        if(collLower == "nocase") {
                            orderCode += ".collate_nocase()";
                        } else if(collLower == "binary") {
                            orderCode += ".collate_binary()";
                        } else if(collLower == "rtrim") {
                            orderCode += ".collate_rtrim()";
                        } else {
                            orderCode += ".collate(" + identifier_to_cpp_string_literal(term.collation) + ")";
                            selectWarnings.push_back("COLLATE " + term.collation +
                                " in ORDER BY is not a built-in collation; generated .collate(...) uses literal name");
                        }
                    }
                    return orderCode;
                };
                if(selectNode->orderBy.size() == 1) {
                    appendClause(formatOrderTerm(selectNode->orderBy.at(0)));
                } else {
                    std::string multiCode = "multi_order_by(";
                    for(size_t i = 0; i < selectNode->orderBy.size(); ++i) {
                        if(i > 0) multiCode += ", ";
                        multiCode += formatOrderTerm(selectNode->orderBy.at(i));
                    }
                    multiCode += ")";
                    appendClause(multiCode);
                }
            }

            if(selectNode->limitValue >= 0) {
                std::string limitCode = "limit(" + std::to_string(selectNode->limitValue);
                if(selectNode->offsetValue >= 0) {
                    limitCode += ", offset(" + std::to_string(selectNode->offsetValue) + ")";
                }
                limitCode += ")";
                appendClause(limitCode);
            }

            std::string trailingJoined;
            for(size_t ti = 0; ti < selectTrailingClauses.size(); ++ti) {
                if(ti > 0) {
                    trailingJoined += ", ";
                }
                trailingJoined += selectTrailingClauses.at(ti);
            }

            if(isStar) {
                std::string tail = selectTrailingClauses.empty() ? "" : (", " + trailingJoined);
                std::string codeGetAll = "auto rows = storage.get_all<" + this->structName + ">(" + trailingJoined + ");";
                std::string codeSelectObject =
                    "auto rows = storage.select(object<" + this->structName + ">()" + tail + ");";
                std::string codeSelectAsterisk =
                    "auto rows = storage.select(asterisk<" + this->structName + ">()" + tail + ");";
                std::string chosenApi = "get_all";
                code = codeGetAll;
                if(policyEquals(this->codeGenPolicy, "api_level", "select_object")) {
                    chosenApi = "select_object";
                    code = codeSelectObject;
                } else if(policyEquals(this->codeGenPolicy, "api_level", "select_asterisk")) {
                    chosenApi = "select_asterisk";
                    code = codeSelectAsterisk;
                }
                if(apiLevelDecisionId >= 0) {
                    selectDecisionPoints.insert(
                        selectDecisionPoints.begin(),
                        DecisionPoint{
                            apiLevelDecisionId,
                            "api_level",
                            chosenApi,
                            code,
                            {Alternative{"select_object",
                                         codeSelectObject,
                                         "select(object<T>(), ...) returns std::tuple of columns"},
                             Alternative{"select_asterisk",
                                         codeSelectAsterisk,
                                         "select(asterisk<T>(), ...) returns full row objects"}}});
                }
            } else {
                if(!trailingJoined.empty()) {
                    code += ", ";
                    code += trailingJoined;
                }
                code += ");";
            }
            return CodeGenResult{code, std::move(selectDecisionPoints), std::move(selectWarnings)};
        }
        if(auto* pragmaNode = dynamic_cast<const PragmaNode*>(&astNode)) {
            return codegenPragmaStatement(*pragmaNode);
        }
        if(auto* createView = dynamic_cast<const CreateViewNode*>(&astNode)) {
            std::string displayName;
            if(createView->viewSchemaName) {
                displayName = *createView->viewSchemaName;
                displayName += '.';
            }
            displayName += createView->viewName;
            return CodeGenResult{"/* CREATE VIEW " + displayName + " — not supported for sqlite_orm */",
                                 {},
                                 {"CREATE VIEW " + displayName + " is not supported for sqlite_orm code generation"}};
        }
        return CodeGenResult{"/* unsupported node */", {}, {}};
    }

    namespace {

        std::optional<std::string> journalModeSqlTokenToCppEnum(std::string_view token) {
            const std::string lower = toLowerAscii(strip_identifier_quotes(token));
            if(lower == "delete") return std::string{"sqlite_orm::journal_mode::DELETE"};
            if(lower == "truncate") return std::string{"sqlite_orm::journal_mode::TRUNCATE"};
            if(lower == "persist") return std::string{"sqlite_orm::journal_mode::PERSIST"};
            if(lower == "memory") return std::string{"sqlite_orm::journal_mode::MEMORY"};
            if(lower == "wal") return std::string{"sqlite_orm::journal_mode::WAL"};
            if(lower == "off") return std::string{"sqlite_orm::journal_mode::OFF"};
            return std::nullopt;
        }

        std::optional<std::string> lockingModeSqlTokenToCppEnum(std::string_view token) {
            const std::string lower = toLowerAscii(strip_identifier_quotes(token));
            if(lower == "normal") return std::string{"sqlite_orm::locking_mode::NORMAL"};
            if(lower == "exclusive") return std::string{"sqlite_orm::locking_mode::EXCLUSIVE"};
            return std::nullopt;
        }

        std::optional<std::string> pragmaTableNameLiteral(const AstNode& valueNode) {
            if(const auto* s = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
                return sql_string_to_cpp(s->value);
            }
            if(const auto* c = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
                return identifier_to_cpp_string_literal(c->columnName);
            }
            return std::nullopt;
        }

        std::optional<std::string> pragmaJournalOrLockingValueToken(const AstNode& valueNode) {
            if(const auto* s = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
                if(s->value.size() >= 2 && s->value.front() == '\'' && s->value.back() == '\'') {
                    return std::string(s->value.substr(1, s->value.size() - 2));
                }
            }
            if(const auto* c = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
                return std::string(c->columnName);
            }
            return std::nullopt;
        }

        std::optional<bool> pragmaRecursiveTriggersBool(const AstNode& valueNode) {
            if(const auto* i = dynamic_cast<const IntegerLiteralNode*>(&valueNode)) {
                if(i->value == "0") return false;
                if(i->value == "1") return true;
                return std::nullopt;
            }
            if(const auto* b = dynamic_cast<const BoolLiteralNode*>(&valueNode)) {
                return b->value;
            }
            if(const auto* s = dynamic_cast<const StringLiteralNode*>(&valueNode)) {
                if(s->value.size() >= 2) {
                    const std::string inner = toLowerAscii(s->value.substr(1, s->value.size() - 2));
                    if(inner == "on" || inner == "yes" || inner == "true") return true;
                    if(inner == "off" || inner == "no" || inner == "false") return false;
                }
            }
            if(const auto* c = dynamic_cast<const ColumnRefNode*>(&valueNode)) {
                const std::string t = toLowerAscii(c->columnName);
                if(t == "on") return true;
                if(t == "off") return false;
            }
            return std::nullopt;
        }

    }  // namespace

    CodeGenResult CodeGenerator::codegenPragmaStatement(const PragmaNode& node) {
        const std::string name = toLowerAscii(node.pragmaName);
        std::vector<DecisionPoint> dps;
        std::vector<std::string> warnings;

        auto mergeSub = [&](CodeGenResult sub) {
            dps.insert(dps.end(), std::make_move_iterator(sub.decisionPoints.begin()),
                       std::make_move_iterator(sub.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(sub.warnings.begin()),
                            std::make_move_iterator(sub.warnings.end()));
            return std::move(sub.code);
        };

        if(name == "module_list") {
            return CodeGenResult{"storage.pragma.module_list();", {}, {}};
        }
        if(name == "quick_check") {
            return CodeGenResult{"storage.pragma.quick_check();", {}, {}};
        }
        if(name == "table_info") {
            if(!node.value) {
                accumulatedErrors.push_back("PRAGMA table_info requires a table name");
                return CodeGenResult{"/* PRAGMA table_info */"};
            }
            if(auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.table_info(" + *lit + ");", {}, {}};
            }
            accumulatedErrors.push_back("PRAGMA table_info: use a string literal or identifier for the table name");
            return CodeGenResult{"/* PRAGMA table_info */"};
        }
        if(name == "table_xinfo") {
            if(!node.value) {
                accumulatedErrors.push_back("PRAGMA table_xinfo requires a table name");
                return CodeGenResult{"/* PRAGMA table_xinfo */"};
            }
            if(auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.table_xinfo(" + *lit + ");", {}, {}};
            }
            accumulatedErrors.push_back("PRAGMA table_xinfo: use a string literal or identifier for the table name");
            return CodeGenResult{"/* PRAGMA table_xinfo */"};
        }
        if(name == "integrity_check") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.integrity_check();", {}, {}};
            }
            if(const auto* n = dynamic_cast<const IntegerLiteralNode*>(node.value.get())) {
                return CodeGenResult{"storage.pragma.integrity_check(" + std::string(n->value) + ");", {}, {}};
            }
            if(auto lit = pragmaTableNameLiteral(*node.value)) {
                return CodeGenResult{"storage.pragma.integrity_check(" + *lit + ");", {}, {}};
            }
            warnings.push_back(
                "PRAGMA integrity_check argument is emitted via subexpression codegen; ensure it matches "
                "sqlite_orm::pragma_t::integrity_check overloads");
            std::string arg = mergeSub(generateNode(*node.value));
            return CodeGenResult{"storage.pragma.integrity_check(" + std::move(arg) + ");", std::move(dps),
                                 std::move(warnings)};
        }
        if(name == "busy_timeout" || name == "application_id" || name == "synchronous" || name == "user_version" ||
           name == "auto_vacuum" || name == "max_page_count") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma." + name + "();", {}, {}};
            }
            std::string arg = mergeSub(generateNode(*node.value));
            return CodeGenResult{"storage.pragma." + name + "(" + std::move(arg) + ");", std::move(dps),
                                 std::move(warnings)};
        }
        if(name == "recursive_triggers") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.recursive_triggers();", {}, {}};
            }
            if(auto b = pragmaRecursiveTriggersBool(*node.value)) {
                return CodeGenResult{std::string("storage.pragma.recursive_triggers(") + (*b ? "true" : "false") + ");",
                                     {}, {}};
            }
            accumulatedErrors.push_back(
                "PRAGMA recursive_triggers = …: use 0/1, TRUE/FALSE, ON/OFF, or a string literal");
            return CodeGenResult{"/* PRAGMA recursive_triggers */"};
        }
        if(name == "journal_mode") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.journal_mode();", {}, {}};
            }
            if(auto tok = pragmaJournalOrLockingValueToken(*node.value)) {
                if(auto cpp = journalModeSqlTokenToCppEnum(*tok)) {
                    return CodeGenResult{"storage.pragma.journal_mode(" + *cpp + ");", {}, {}};
                }
            }
            accumulatedErrors.push_back("PRAGMA journal_mode: unknown mode (expected delete, wal, memory, …)");
            return CodeGenResult{"/* PRAGMA journal_mode */"};
        }
        if(name == "locking_mode") {
            if(!node.value) {
                return CodeGenResult{"storage.pragma.locking_mode();", {}, {}};
            }
            if(auto tok = pragmaJournalOrLockingValueToken(*node.value)) {
                if(auto cpp = lockingModeSqlTokenToCppEnum(*tok)) {
                    return CodeGenResult{"storage.pragma.locking_mode(" + *cpp + ");", {}, {}};
                }
            }
            accumulatedErrors.push_back("PRAGMA locking_mode: expected NORMAL or EXCLUSIVE");
            return CodeGenResult{"/* PRAGMA locking_mode */"};
        }

        accumulatedErrors.push_back("internal: unsupported PRAGMA reached codegen");
        return CodeGenResult{"/* PRAGMA */"};
    }

    CodeGenResult CodeGenerator::tryCodegenSqliteSelectSubexpression(const SelectNode& selectNode) {
        struct SubselectAliasRestore {
            CodeGenerator* generator;
            std::map<std::string, std::string> savedAliases;
            std::string savedStructName;
            std::optional<std::string> savedImplicitCte;

            SubselectAliasRestore(CodeGenerator* gen)
                : generator(gen), savedAliases(gen->fromTableAliasToStructName), savedStructName(gen->structName),
                  savedImplicitCte(std::move(gen->implicitSingleSourceCteTypedef)) {}

            ~SubselectAliasRestore() {
                generator->fromTableAliasToStructName = std::move(savedAliases);
                generator->structName = std::move(savedStructName);
                generator->implicitSingleSourceCteTypedef = std::move(savedImplicitCte);
            }
        } restore{this};

        std::vector<std::string> subWarnings;
        std::vector<DecisionPoint> subDecisionPoints;

        if(selectNode.groupBy || !selectNode.orderBy.empty() || selectNode.limitValue >= 0 ||
           selectNode.offsetValue >= 0) {
            subWarnings.push_back(
                "GROUP BY / ORDER BY / LIMIT / OFFSET in subquery are not yet mapped to sqlite_orm select(...)");
            return CodeGenResult{{}, {}, std::move(subWarnings)};
        }

        for(const auto& fromItem : selectNode.fromClause) {
            if(fromItem.table.derivedSelect) {
                subWarnings.push_back("subselect in FROM is not supported in sqlite_orm codegen");
                return CodeGenResult{{}, {}, std::move(subWarnings)};
            }
        }

        this->fromTableAliasToStructName.clear();
        auto structForFromTable = [&](std::string_view tableSqlName) -> std::string {
            auto k = normalize_table_key(tableSqlName);
            if(auto cteLookup = this->activeCteTypedefByTableKey.find(k);
               cteLookup != this->activeCteTypedefByTableKey.end()) {
                return cteLookup->second;
            }
            return to_struct_name(tableSqlName);
        };
        if(!selectNode.fromClause.empty()) {
            for(const auto& fromItem : selectNode.fromClause) {
                const auto& ft = fromItem.table;
                if(ft.schemaName) {
                    subWarnings.push_back(
                        "FROM clause schema qualifier '" + *ft.schemaName + "' for table '" + ft.tableName +
                        "' is not represented in sqlite_orm mapping");
                }
                std::string mappedStructName = structForFromTable(ft.tableName);
                this->fromTableAliasToStructName[ft.tableName] = mappedStructName;
                if(ft.alias) {
                    this->fromTableAliasToStructName[*ft.alias] = mappedStructName;
                }
            }
            this->structName = structForFromTable(selectNode.fromClause.at(0).table.tableName);
        }
        if(selectNode.fromClause.size() == 1u) {
            auto k = normalize_table_key(selectNode.fromClause.at(0).table.tableName);
            if(auto it = this->activeCteTypedefByTableKey.find(k);
               it != this->activeCteTypedefByTableKey.end()) {
                this->implicitSingleSourceCteTypedef = it->second;
            }
        }

        auto expressionCode = [&](const AstNode& node) -> std::string {
            auto result = generateNode(node);
            subWarnings.insert(subWarnings.end(),
                               std::make_move_iterator(result.warnings.begin()),
                               std::make_move_iterator(result.warnings.end()));
            subDecisionPoints.insert(subDecisionPoints.end(),
                                     std::make_move_iterator(result.decisionPoints.begin()),
                                     std::make_move_iterator(result.decisionPoints.end()));
            return result.code;
        };

        bool isStar = selectNode.columns.size() == 1 && !selectNode.columns.at(0).expression;
        std::string columnPart;
        if(isStar) {
            if(selectNode.fromClause.empty()) {
                subWarnings.push_back("SELECT * subexpression requires FROM for sqlite_orm asterisk<...>()");
                return CodeGenResult{{}, std::move(subDecisionPoints), std::move(subWarnings)};
            }
            columnPart = "asterisk<" + this->structName + ">()";
        } else {
            if(selectNode.distinct) {
                if(selectNode.columns.size() == 1) {
                    columnPart = "distinct(" + expressionCode(*selectNode.columns.at(0).expression) + ")";
                } else {
                    columnPart = "distinct(columns(";
                    for(size_t i = 0; i < selectNode.columns.size(); ++i) {
                        if(i > 0) columnPart += ", ";
                        columnPart += expressionCode(*selectNode.columns.at(i).expression);
                    }
                    columnPart += "))";
                }
            } else if(selectNode.columns.size() == 1) {
                columnPart = expressionCode(*selectNode.columns.at(0).expression);
            } else {
                columnPart = "columns(";
                for(size_t i = 0; i < selectNode.columns.size(); ++i) {
                    if(i > 0) columnPart += ", ";
                    columnPart += expressionCode(*selectNode.columns.at(i).expression);
                }
                columnPart += ")";
            }
        }

        std::vector<std::string> tailParts;
        for(size_t joinIndex = 1; joinIndex < selectNode.fromClause.size(); ++joinIndex) {
            const auto& joinItem = selectNode.fromClause.at(joinIndex);
            const auto& leftTable = selectNode.fromClause.at(joinIndex - 1).table;
            std::string rightStruct = structForFromTable(joinItem.table.tableName);
            std::string leftStruct = structForFromTable(leftTable.tableName);
            std::string joinCode;
            switch(joinItem.leadingJoin) {
            case JoinKind::crossJoin:
            case JoinKind::naturalInnerJoin:
                joinCode = std::string(join_sqlite_orm_api_name(joinItem.leadingJoin)) + "<" + rightStruct + ">()";
                break;
            case JoinKind::naturalLeftJoin:
                subWarnings.push_back(
                    "NATURAL LEFT JOIN is not supported in sqlite_orm; generated natural_join does not match SQL "
                    "semantics");
                joinCode = "natural_join<" + rightStruct + ">()";
                break;
            default: {
                std::string api(join_sqlite_orm_api_name(joinItem.leadingJoin));
                if(!joinItem.usingColumnNames.empty()) {
                    if(joinItem.usingColumnNames.size() == 1) {
                        joinCode = std::string(api) + "<" + rightStruct + ">(using_(&" + rightStruct + "::" +
                                   to_cpp_identifier(joinItem.usingColumnNames.at(0)) + "))";
                    } else {
                        std::string cond;
                        for(size_t ci = 0; ci < joinItem.usingColumnNames.size(); ++ci) {
                            if(ci > 0) {
                                cond += " and ";
                            }
                            auto col = to_cpp_identifier(joinItem.usingColumnNames.at(ci));
                            cond += "c(&" + leftStruct + "::" + col + ") == c(&" + rightStruct + "::" + col + ")";
                        }
                        joinCode = std::string(api) + "<" + rightStruct + ">(on(" + cond + "))";
                    }
                } else if(joinItem.onExpression) {
                    joinCode =
                        std::string(api) + "<" + rightStruct + ">(on(" + expressionCode(*joinItem.onExpression) + "))";
                } else {
                    joinCode = std::string(api) + "<" + rightStruct + ">(on(true))";
                }
                break;
            }
            }
            tailParts.push_back(std::move(joinCode));
        }

        if(selectNode.whereClause) {
            tailParts.push_back("where(" + expressionCode(*selectNode.whereClause) + ")");
        }

        for(const auto& namedWindow : selectNode.namedWindows) {
            if(!namedWindow.definition) {
                continue;
            }
            std::string windowArgs = codegenOverClause(*namedWindow.definition, subDecisionPoints, subWarnings);
            if(!windowArgs.empty()) {
                tailParts.push_back("window(" + identifier_to_cpp_string_literal(namedWindow.name) + ", " +
                                    windowArgs + ")");
            }
        }

        std::string code = "select(" + columnPart;
        for(const auto& part : tailParts) {
            code += ", ";
            code += part;
        }
        code += ")";
        return CodeGenResult{code, std::move(subDecisionPoints), std::move(subWarnings)};
    }

    CodeGenResult CodeGenerator::tryCodegenCompoundSelectSubexpression(const CompoundSelectNode& compoundNode) {
        if(compoundNode.selects.size() != compoundNode.operators.size() + 1) {
            return CodeGenResult{{}, {}, {"internal: compound SELECT operand count mismatch"}};
        }
        auto* firstSelect = dynamic_cast<const SelectNode*>(compoundNode.selects.at(0).get());
        if(!firstSelect) {
            return CodeGenResult{{}, {}, {"compound SELECT arm is not a SelectNode"}};
        }
        CodeGenResult accumulated = tryCodegenSqliteSelectSubexpression(*firstSelect);
        if(accumulated.code.empty()) {
            return accumulated;
        }
        for(size_t operatorIndex = 0; operatorIndex < compoundNode.operators.size(); ++operatorIndex) {
            auto* nextSelect = dynamic_cast<const SelectNode*>(compoundNode.selects.at(operatorIndex + 1).get());
            if(!nextSelect) {
                return CodeGenResult{{}, {}, {"compound SELECT arm is not a SelectNode"}};
            }
            CodeGenResult nextArm = tryCodegenSqliteSelectSubexpression(*nextSelect);
            accumulated.decisionPoints.insert(accumulated.decisionPoints.end(),
                std::make_move_iterator(nextArm.decisionPoints.begin()),
                std::make_move_iterator(nextArm.decisionPoints.end()));
            accumulated.warnings.insert(accumulated.warnings.end(),
                std::make_move_iterator(nextArm.warnings.begin()),
                std::make_move_iterator(nextArm.warnings.end()));
            if(nextArm.code.empty()) {
                return CodeGenResult{{}, std::move(accumulated.decisionPoints), std::move(accumulated.warnings)};
            }
            accumulated.code = std::string(compound_select_api(compoundNode.operators.at(operatorIndex))) + "(" +
                               accumulated.code + ", " + nextArm.code + ")";
        }
        return accumulated;
    }

    CodeGenResult CodeGenerator::tryCodegenSelectLikeSubquery(const AstNode& node) {
        if(auto* selectNode = dynamic_cast<const SelectNode*>(&node)) {
            return tryCodegenSqliteSelectSubexpression(*selectNode);
        }
        if(auto* compoundNode = dynamic_cast<const CompoundSelectNode*>(&node)) {
            return tryCodegenCompoundSelectSubexpression(*compoundNode);
        }
        if(auto* withQueryNode = dynamic_cast<const WithQueryNode*>(&node)) {
            auto inner = tryCodegenSelectLikeSubquery(*withQueryNode->statement);
            std::vector<std::string> subWarnings = std::move(inner.warnings);
            subWarnings.insert(subWarnings.begin(),
                               "nested WITH in subquery: sqlite_orm select(...) cannot embed CTEs; generated code "
                               "uses the inner SELECT only (WITH clause dropped)");
            return CodeGenResult{std::move(inner.code), std::move(inner.decisionPoints), std::move(subWarnings)};
        }
        return CodeGenResult{{}, {}, {"subquery is not a SELECT or compound SELECT for sqlite_orm codegen"}};
    }

    void CodeGenerator::registerColumn(const std::string& cppName, const std::string& cppType) {
        auto [it, inserted] = this->columnTypes.try_emplace(cppName, cppType);
        if(!inserted && it->second == "int" && cppType != "int") {
            it->second = cppType;
        }
    }

    std::string CodeGenerator::inferTypeFromNode(const AstNode& node) const {
        if(dynamic_cast<const StringLiteralNode*>(&node)) return "std::string";
        if(dynamic_cast<const IntegerLiteralNode*>(&node)) return "int";
        if(dynamic_cast<const RealLiteralNode*>(&node)) return "double";
        if(dynamic_cast<const BoolLiteralNode*>(&node)) return "bool";
        return "int";
    }

    std::string CodeGenerator::generatePrefix() const {
        if(this->columnTypes.empty()) {
            return "";
        }
        std::string result = "struct " + this->structName + " {\n";
        for(const auto& [name, type] : this->columnTypes) {
            result += "    " + type + " " + name + default_initializer(type) + ";\n";
        }
        result += "};";
        return result;
    }

    CodeGenResult CodeGenerator::generateTriggerStep(const AstNode& statement, const std::string& subjectTableStruct) {
        struct TriggerStepScope {
            CodeGenerator* gen;
            std::string savedStruct;
            std::map<std::string, std::string> savedAliases;

            TriggerStepScope(CodeGenerator* g, const std::string& subject)
                : gen(g), savedStruct(g->structName), savedAliases(g->fromTableAliasToStructName) {
                gen->structName = subject;
                gen->fromTableAliasToStructName.clear();
            }
            ~TriggerStepScope() {
                gen->structName = std::move(savedStruct);
                gen->fromTableAliasToStructName = std::move(savedAliases);
            }
        } scope{this, subjectTableStruct};

        if(auto* selectNode = dynamic_cast<const SelectNode*>(&statement)) {
            return tryCodegenSqliteSelectSubexpression(*selectNode);
        }
        if(auto* compoundSelectNode = dynamic_cast<const CompoundSelectNode*>(&statement)) {
            return tryCodegenCompoundSelectSubexpression(*compoundSelectNode);
        }
        auto outer = generateNode(statement);
        std::string c = outer.code;
        static constexpr std::string_view kStoragePrefix = "storage.";
        if(c.size() >= kStoragePrefix.size() && c.compare(0, kStoragePrefix.size(), kStoragePrefix) == 0) {
            c.erase(0, kStoragePrefix.size());
        }
        while(!c.empty() && std::isspace(static_cast<unsigned char>(c.back()))) {
            c.pop_back();
        }
        if(!c.empty() && c.back() == ';') {
            c.pop_back();
        }
        while(!c.empty() && std::isspace(static_cast<unsigned char>(c.back()))) {
            c.pop_back();
        }
        return CodeGenResult{std::move(c), std::move(outer.decisionPoints), std::move(outer.warnings)};
    }

    CreateTableParts CodeGenerator::createTableParts(const CreateTableNode& createTable) {
        const auto sName = to_struct_name(createTable.tableName);
        this->structName = sName;
        const auto rawTableName = strip_identifier_quotes(createTable.tableName);
        std::vector<std::string> warnings;

        std::string structDecl = "struct " + sName + " {\n";
        for(const auto& column : createTable.columns) {
            const auto cppName = to_cpp_identifier(column.name);
            const auto cppType =
                column.typeName.empty() ? "std::vector<char>" : sqlite_type_to_cpp(column.typeName);
            const bool nullable = !column.primaryKey && !column.notNull;
            if(nullable) {
                structDecl += "    std::optional<" + cppType + "> " + cppName + ";\n";
            } else {
                structDecl += "    " + cppType + " " + cppName + default_initializer(cppType) + ";\n";
            }
        }
        structDecl += "};\n";

        std::string makeExpr = "make_table(\"" + rawTableName + "\"";
        for(const auto& column : createTable.columns) {
            const auto cppName = to_cpp_identifier(column.name);
            const auto rawColName = strip_identifier_quotes(column.name);
            makeExpr += ",\n        make_column(\"" + rawColName + "\", &" + sName + "::" + cppName;
            if(column.primaryKey) {
                std::string pk = "primary_key()";
                switch(column.primaryKeyConflict) {
                case ConflictClause::rollback: pk += ".on_conflict_rollback()"; break;
                case ConflictClause::abort: pk += ".on_conflict_abort()"; break;
                case ConflictClause::fail: pk += ".on_conflict_fail()"; break;
                case ConflictClause::ignore: pk += ".on_conflict_ignore()"; break;
                case ConflictClause::replace: pk += ".on_conflict_replace()"; break;
                case ConflictClause::none: break;
                }
                if(column.autoincrement) {
                    pk += ".autoincrement()";
                }
                makeExpr += ", " + pk;
            }
            if(column.defaultValue) {
                const auto defaultCode = generateNode(*column.defaultValue).code;
                makeExpr += ", default_value(" + defaultCode + ")";
            }
            if(column.unique) {
                makeExpr += ", unique()";
                if(column.uniqueConflict != ConflictClause::none) {
                    warnings.push_back("UNIQUE ON CONFLICT clause on column '" + rawColName +
                                       "' is not supported by sqlite_orm::unique()");
                }
            }
            if(column.checkExpression) {
                const auto checkCode = generateNode(*column.checkExpression).code;
                makeExpr += ", check(" + checkCode + ")";
            }
            if(!column.collation.empty()) {
                const auto lower = toLowerAscii(column.collation);
                if(lower == "nocase") {
                    makeExpr += ", collate_nocase()";
                } else if(lower == "binary") {
                    makeExpr += ", collate_binary()";
                } else if(lower == "rtrim") {
                    makeExpr += ", collate_rtrim()";
                } else {
                    warnings.push_back("COLLATE " + column.collation + " on column '" + rawColName +
                                       "' is not a built-in collation in sqlite_orm");
                }
            }
            if(column.generatedExpression) {
                const auto exprCode = generateNode(*column.generatedExpression).code;
                if(column.generatedAlways) {
                    makeExpr += ", generated_always_as(" + exprCode + ")";
                } else {
                    makeExpr += ", as(" + exprCode + ")";
                }
                if(column.generatedStorage == ColumnDef::GeneratedStorage::stored) {
                    makeExpr += ".stored()";
                } else if(column.generatedStorage == ColumnDef::GeneratedStorage::virtual_) {
                    makeExpr += ".virtual_()";
                }
            }
            makeExpr += ")";
        }
        for(const auto& column : createTable.columns) {
            if(!column.foreignKey) {
                continue;
            }
            auto& foreignKey = *column.foreignKey;
            const auto cppName = to_cpp_identifier(column.name);
            const auto refStructName = to_struct_name(foreignKey.table);
            const auto refColName = foreignKey.column.empty() ? cppName : to_cpp_identifier(foreignKey.column);
            makeExpr += ",\n        foreign_key(&" + sName + "::" + cppName + ").references(&" + refStructName +
                        "::" + refColName + ")";
            const auto action_str = [](ForeignKeyAction action) -> std::string {
                switch(action) {
                case ForeignKeyAction::cascade: return ".cascade()";
                case ForeignKeyAction::restrict_: return ".restrict_()";
                case ForeignKeyAction::setNull: return ".set_null()";
                case ForeignKeyAction::setDefault: return ".set_default()";
                case ForeignKeyAction::noAction: return ".no_action()";
                case ForeignKeyAction::none: return "";
                }
                return "";
            };
            if(foreignKey.onDelete != ForeignKeyAction::none) {
                makeExpr += ".on_delete" + action_str(foreignKey.onDelete);
            }
            if(foreignKey.onUpdate != ForeignKeyAction::none) {
                makeExpr += ".on_update" + action_str(foreignKey.onUpdate);
            }
            if(foreignKey.deferrability != Deferrability::none) {
                std::string desc = foreignKey.deferrability == Deferrability::deferrable ? "DEFERRABLE" : "NOT DEFERRABLE";
                if(foreignKey.initially == InitialConstraintMode::deferred) desc += " INITIALLY DEFERRED";
                else if(foreignKey.initially == InitialConstraintMode::immediate) desc += " INITIALLY IMMEDIATE";
                warnings.push_back(desc + " on foreign key for column '" + column.name +
                    "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for(const auto& tableForeignKey : createTable.foreignKeys) {
            const auto cppName = to_cpp_identifier(tableForeignKey.column);
            const auto refStructName = to_struct_name(tableForeignKey.references.table);
            const auto refColName = tableForeignKey.references.column.empty()
                ? cppName : to_cpp_identifier(tableForeignKey.references.column);
            makeExpr += ",\n        foreign_key(&" + sName + "::" + cppName + ").references(&" + refStructName +
                        "::" + refColName + ")";
            const auto action_str = [](ForeignKeyAction action) -> std::string {
                switch(action) {
                case ForeignKeyAction::cascade: return ".cascade()";
                case ForeignKeyAction::restrict_: return ".restrict_()";
                case ForeignKeyAction::setNull: return ".set_null()";
                case ForeignKeyAction::setDefault: return ".set_default()";
                case ForeignKeyAction::noAction: return ".no_action()";
                case ForeignKeyAction::none: return "";
                }
                return "";
            };
            if(tableForeignKey.references.onDelete != ForeignKeyAction::none) {
                makeExpr += ".on_delete" + action_str(tableForeignKey.references.onDelete);
            }
            if(tableForeignKey.references.onUpdate != ForeignKeyAction::none) {
                makeExpr += ".on_update" + action_str(tableForeignKey.references.onUpdate);
            }
            if(tableForeignKey.references.deferrability != Deferrability::none) {
                std::string desc = tableForeignKey.references.deferrability == Deferrability::deferrable
                    ? "DEFERRABLE" : "NOT DEFERRABLE";
                if(tableForeignKey.references.initially == InitialConstraintMode::deferred) desc += " INITIALLY DEFERRED";
                else if(tableForeignKey.references.initially == InitialConstraintMode::immediate) desc += " INITIALLY IMMEDIATE";
                warnings.push_back(desc + " on table-level foreign key for column '" + tableForeignKey.column +
                    "' is not supported in sqlite_orm — ignored in codegen");
            }
        }
        for(const auto& tablePrimaryKey : createTable.primaryKeys) {
            makeExpr += ",\n        primary_key(";
            for(size_t i = 0; i < tablePrimaryKey.columns.size(); ++i) {
                if(i > 0) {
                    makeExpr += ", ";
                }
                makeExpr += "&" + sName + "::" + to_cpp_identifier(tablePrimaryKey.columns.at(i));
            }
            makeExpr += ")";
        }
        for(const auto& tableUnique : createTable.uniques) {
            makeExpr += ",\n        unique(";
            for(size_t i = 0; i < tableUnique.columns.size(); ++i) {
                if(i > 0) {
                    makeExpr += ", ";
                }
                makeExpr += "&" + sName + "::" + to_cpp_identifier(tableUnique.columns.at(i));
            }
            makeExpr += ")";
        }
        for(const auto& tableCheck : createTable.checks) {
            if(tableCheck.expression) {
                const auto checkCode = generateNode(*tableCheck.expression).code;
                makeExpr += ",\n        check(" + checkCode + ")";
            }
        }
        makeExpr += ")";
        if(createTable.withoutRowid) {
            makeExpr += ".without_rowid()";
        }
        if(createTable.strict) {
            warnings.push_back("STRICT tables are not directly supported by sqlite_orm — "
                               "the STRICT qualifier is ignored in codegen");
        }

        return CreateTableParts{std::move(structDecl), std::move(makeExpr), std::move(warnings)};
    }

    std::string CodeGenerator::codegenWindowFrameBound(const WindowFrameBound& bound,
                                                       std::vector<DecisionPoint>& decisionPoints,
                                                       std::vector<std::string>& warnings) {
        switch(bound.kind) {
        case WindowFrameBoundKind::unboundedPreceding:
            return "unbounded_preceding()";
        case WindowFrameBoundKind::currentRow:
            return "current_row()";
        case WindowFrameBoundKind::unboundedFollowing:
            return "unbounded_following()";
        case WindowFrameBoundKind::exprPreceding:
        case WindowFrameBoundKind::exprFollowing: {
            if(!bound.expr) {
                return {};
            }
            auto expressionResult = generateNode(*bound.expr);
            decisionPoints.insert(decisionPoints.end(),
                std::make_move_iterator(expressionResult.decisionPoints.begin()),
                std::make_move_iterator(expressionResult.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(expressionResult.warnings.begin()),
                             std::make_move_iterator(expressionResult.warnings.end()));
            if(bound.kind == WindowFrameBoundKind::exprPreceding) {
                return "preceding(" + expressionResult.code + ")";
            }
            return "following(" + expressionResult.code + ")";
        }
        }
        return {};
    }

    std::string CodeGenerator::codegenWindowFrameSpec(const WindowFrameSpec& frame,
                                                      std::vector<DecisionPoint>& decisionPoints,
                                                      std::vector<std::string>& warnings) {
        std::string_view frameApi;
        switch(frame.unit) {
        case WindowFrameUnit::rows:
            frameApi = "rows";
            break;
        case WindowFrameUnit::range:
            frameApi = "range";
            break;
        case WindowFrameUnit::groups:
            frameApi = "groups";
            break;
        }
        std::string start = codegenWindowFrameBound(frame.start, decisionPoints, warnings);
        std::string end = codegenWindowFrameBound(frame.end, decisionPoints, warnings);
        if(start.empty() || end.empty()) {
            return {};
        }
        std::string core = std::string(frameApi) + "(" + start + ", " + end + ")";
        switch(frame.exclude) {
        case WindowFrameExcludeKind::currentRow:
            core += ".exclude_current_row()";
            break;
        case WindowFrameExcludeKind::group:
            core += ".exclude_group()";
            break;
        case WindowFrameExcludeKind::ties:
            core += ".exclude_ties()";
            break;
        case WindowFrameExcludeKind::none:
            break;
        }
        return core;
    }

    std::string CodeGenerator::codegenOverClause(const OverClause& overClause,
                                                 std::vector<DecisionPoint>& decisionPoints,
                                                 std::vector<std::string>& warnings) {
        if(overClause.namedWindow) {
            return "window_ref(" + identifier_to_cpp_string_literal(*overClause.namedWindow) + ")";
        }
        std::vector<std::string> parts;
        if(!overClause.partitionBy.empty()) {
            std::string inner;
            for(size_t partitionByIndex = 0; partitionByIndex < overClause.partitionBy.size(); ++partitionByIndex) {
                if(partitionByIndex > 0) {
                    inner += ", ";
                }
                auto partitionResult = generateNode(*overClause.partitionBy.at(partitionByIndex));
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(partitionResult.decisionPoints.begin()),
                    std::make_move_iterator(partitionResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(partitionResult.warnings.begin()),
                                std::make_move_iterator(partitionResult.warnings.end()));
                inner += partitionResult.code;
            }
            parts.push_back("partition_by(" + inner + ")");
        }
        if(!overClause.orderBy.empty()) {
            auto formatOrderTerm = [&](const OrderByTerm& term) -> std::string {
                auto expressionResult = generateNode(*term.expression);
                decisionPoints.insert(decisionPoints.end(),
                    std::make_move_iterator(expressionResult.decisionPoints.begin()),
                    std::make_move_iterator(expressionResult.decisionPoints.end()));
                warnings.insert(warnings.end(), std::make_move_iterator(expressionResult.warnings.begin()),
                                std::make_move_iterator(expressionResult.warnings.end()));
                std::string termCode = "order_by(" + expressionResult.code + ")";
                if(term.direction == SortDirection::asc) {
                    termCode += ".asc()";
                } else if(term.direction == SortDirection::desc) {
                    termCode += ".desc()";
                }
                if(!term.collation.empty()) {
                    std::string collLower = toLowerAscii(term.collation);
                    if(collLower == "nocase") {
                        termCode += ".collate_nocase()";
                    } else if(collLower == "binary") {
                        termCode += ".collate_binary()";
                    } else if(collLower == "rtrim") {
                        termCode += ".collate_rtrim()";
                    } else {
                        termCode += ".collate(" + identifier_to_cpp_string_literal(term.collation) + ")";
                        warnings.push_back("COLLATE " + term.collation +
                            " in window ORDER BY is not a built-in collation; generated .collate(...) uses literal name");
                    }
                }
                return termCode;
            };
            if(overClause.orderBy.size() == 1) {
                parts.push_back(formatOrderTerm(overClause.orderBy.at(0)));
            } else {
                std::string multi = "multi_order_by(";
                for(size_t oi = 0; oi < overClause.orderBy.size(); ++oi) {
                    if(oi > 0) {
                        multi += ", ";
                    }
                    multi += formatOrderTerm(overClause.orderBy.at(oi));
                }
                multi += ")";
                parts.push_back(std::move(multi));
            }
        }
        if(overClause.frame) {
            std::string frameCode = codegenWindowFrameSpec(*overClause.frame, decisionPoints, warnings);
            if(!frameCode.empty()) {
                parts.push_back(std::move(frameCode));
            }
        }
        if(parts.empty()) {
            return {};
        }
        std::string joined = parts.at(0);
        for(size_t i = 1; i < parts.size(); ++i) {
            joined += ", ";
            joined += parts.at(i);
        }
        return joined;
    }

    CodeGenResult CodeGenerator::generateWithQuery(const WithQueryNode& withQueryNode) {
        std::vector<std::string> warnings;
        std::vector<DecisionPoint> allDecisionPoints;

        struct ClearActiveCteMap {
            CodeGenerator* generator;
            ~ClearActiveCteMap() { generator->activeCteTypedefByTableKey.clear(); }
        } clearGuard{this};

        this->activeCteTypedefByTableKey.clear();

        const auto& ctes = withQueryNode.clause.tables;
        std::vector<std::string> innerCodes;
        innerCodes.reserve(ctes.size());
        for(const auto& cte : ctes) {
            auto part = tryCodegenSelectLikeSubquery(*cte.query);
            allDecisionPoints.insert(allDecisionPoints.end(),
                std::make_move_iterator(part.decisionPoints.begin()),
                std::make_move_iterator(part.decisionPoints.end()));
            warnings.insert(warnings.end(), std::make_move_iterator(part.warnings.begin()),
                             std::make_move_iterator(part.warnings.end()));
            if(part.code.empty()) {
                warnings.push_back(
                    "WITH: a CTE SELECT is not mapped to a sqlite_orm select(...) subexpression; emitted outer "
                    "statement without storage.with()");
                this->activeCteTypedefByTableKey.clear();
                auto inner = generateNode(*withQueryNode.statement);
                warnings.insert(warnings.end(), std::make_move_iterator(inner.warnings.begin()),
                                 std::make_move_iterator(inner.warnings.end()));
                allDecisionPoints.insert(allDecisionPoints.end(),
                    std::make_move_iterator(inner.decisionPoints.begin()),
                    std::make_move_iterator(inner.decisionPoints.end()));
                return CodeGenResult{inner.code, std::move(allDecisionPoints), std::move(warnings)};
            }
            innerCodes.push_back(std::move(part.code));
        }

        std::vector<std::string> typedefNames;
        typedefNames.reserve(ctes.size());
        for(size_t ci = 0; ci < ctes.size(); ++ci) {
            std::string alias = "cte_" + std::to_string(ci);
            typedefNames.push_back(alias);
            this->activeCteTypedefByTableKey[normalize_table_key(ctes[ci].cteName)] = alias;
        }

        std::string prelude = "using namespace sqlite_orm::literals;\n";
        for(size_t ci = 0; ci < ctes.size(); ++ci) {
            prelude += "using " + typedefNames[ci] + " = decltype(" + std::to_string(ci + 1) + "_ctealias);\n";
        }

        auto buildCteExpression = [&](size_t ci) -> std::string {
            std::string b = "cte<" + typedefNames[ci] + ">";
            if(!ctes[ci].columnNames.empty()) {
                b += "(";
                for(size_t cn = 0; cn < ctes[ci].columnNames.size(); ++cn) {
                    if(cn > 0) {
                        b += ", ";
                    }
                    b += identifier_to_cpp_string_literal(ctes[ci].columnNames[cn]);
                }
                b += ")";
            } else {
                b += "()";
            }
            std::string asMethod = ".as(";
            if(ctes[ci].materialization == CteMaterialization::materialized) {
                asMethod = ".as<sqlite_orm::materialized()>(";
                warnings.push_back(
                    "WITH: AS MATERIALIZED uses sqlite_orm::materialized() — requires C++20 and "
                    "SQLITE_ORM_WITH_CPP20_ALIASES in the consuming project");
            } else if(ctes[ci].materialization == CteMaterialization::notMaterialized) {
                asMethod = ".as<sqlite_orm::not_materialized()>(";
                warnings.push_back(
                    "WITH: AS NOT MATERIALIZED uses sqlite_orm::not_materialized() — requires C++20 and "
                    "SQLITE_ORM_WITH_CPP20_ALIASES in the consuming project");
            }
            b += asMethod + innerCodes[ci] + ")";
            return b;
        };

        std::string cteArgument;
        if(ctes.size() == 1) {
            cteArgument = buildCteExpression(0);
        } else {
            cteArgument = "std::make_tuple(";
            for(size_t ci = 0; ci < ctes.size(); ++ci) {
                if(ci > 0) {
                    cteArgument += ", ";
                }
                cteArgument += buildCteExpression(ci);
            }
            cteArgument += ")";
        }

        const char* withApi = withQueryNode.clause.recursive ? "with_recursive" : "with";

        const auto* outerSelect = dynamic_cast<const SelectNode*>(withQueryNode.statement.get());
        const auto* outerCompound = dynamic_cast<const CompoundSelectNode*>(withQueryNode.statement.get());
        const auto* outerInsert = dynamic_cast<const InsertNode*>(withQueryNode.statement.get());
        const auto* outerUpdate = dynamic_cast<const UpdateNode*>(withQueryNode.statement.get());
        const auto* outerDelete = dynamic_cast<const DeleteNode*>(withQueryNode.statement.get());

        if(outerSelect || outerCompound) {
            auto outerResult = generateNode(*withQueryNode.statement);
            warnings.insert(warnings.end(), std::make_move_iterator(outerResult.warnings.begin()),
                             std::make_move_iterator(outerResult.warnings.end()));
            allDecisionPoints.insert(allDecisionPoints.end(),
                std::make_move_iterator(outerResult.decisionPoints.begin()),
                std::make_move_iterator(outerResult.decisionPoints.end()));

            auto outerArgOpt = extract_storage_select_argument(outerResult.code);
            if(!outerArgOpt) {
                warnings.push_back(
                    "WITH: outer SELECT is not in the expected `auto rows = storage.select(...);` form; emitted as plain "
                    "outer codegen");
                this->activeCteTypedefByTableKey.clear();
                return CodeGenResult{outerResult.code, std::move(allDecisionPoints), std::move(warnings)};
            }

            std::string code = prelude + "auto rows = storage." + std::string(withApi) + "(" + cteArgument + ", " +
                               *outerArgOpt + ");";

            warnings.push_back(
                "WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                "sqlite_orm::literals` scope for `_ctealias`");

            return CodeGenResult{std::move(code), std::move(allDecisionPoints), std::move(warnings)};
        }

        if(outerInsert || outerUpdate || outerDelete) {
            auto outerResult = generateNode(*withQueryNode.statement);
            warnings.insert(warnings.end(), std::make_move_iterator(outerResult.warnings.begin()),
                             std::make_move_iterator(outerResult.warnings.end()));
            allDecisionPoints.insert(allDecisionPoints.end(),
                std::make_move_iterator(outerResult.decisionPoints.begin()),
                std::make_move_iterator(outerResult.decisionPoints.end()));
            std::string stripped = strip_storage_prefix_and_trailing_semicolon(outerResult.code);
            if(stripped.empty()) {
                warnings.push_back(
                    "WITH … DML: outer statement codegen could not be wrapped in storage.with(); emitted plain DML");
                this->activeCteTypedefByTableKey.clear();
                return CodeGenResult{outerResult.code, std::move(allDecisionPoints), std::move(warnings)};
            }
            std::string code = prelude + "storage." + std::string(withApi) + "(" + cteArgument + ", " + stripped + ");";
            warnings.push_back(
                "WITH … DML: second argument omits the `storage.` prefix (sqlite_orm::with / with_recursive)");
            warnings.push_back(
                "WITH: requires SQLite ≥ 3.8.3, sqlite_orm built with SQLITE_ORM_WITH_CTE, and `using namespace "
                "sqlite_orm::literals` scope for `_ctealias`");
            return CodeGenResult{std::move(code), std::move(allDecisionPoints), std::move(warnings)};
        }

        warnings.push_back(
            "WITH …: outer statement kind is not wrapped with storage.with() in sqlite2orm codegen; emitted as plain "
            "statement");
        this->activeCteTypedefByTableKey.clear();
        auto inner = generateNode(*withQueryNode.statement);
        warnings.insert(warnings.end(), std::make_move_iterator(inner.warnings.begin()),
                         std::make_move_iterator(inner.warnings.end()));
        allDecisionPoints.insert(allDecisionPoints.end(),
            std::make_move_iterator(inner.decisionPoints.begin()),
            std::make_move_iterator(inner.decisionPoints.end()));
        return CodeGenResult{inner.code, std::move(allDecisionPoints), std::move(warnings)};
    }

}  // namespace sqlite2orm
