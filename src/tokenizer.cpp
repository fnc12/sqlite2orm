#include <sqlite2orm/tokenizer.h>

#include <algorithm>
#include <cctype>
#include <unordered_map>

namespace sqlite2orm {

    namespace {
        const std::unordered_map<std::string_view, TokenType>& keywordMap() {
            static const std::unordered_map<std::string_view, TokenType> map = {
                {"abort", TokenType::kwAbort},
                {"action", TokenType::kwAction},
                {"add", TokenType::kwAdd},
                {"after", TokenType::kwAfter},
                {"all", TokenType::kwAll},
                {"alter", TokenType::kwAlter},
                {"always", TokenType::kwAlways},
                {"analyze", TokenType::kwAnalyze},
                {"and", TokenType::kwAnd},
                {"as", TokenType::kwAs},
                {"asc", TokenType::kwAsc},
                {"attach", TokenType::kwAttach},
                {"autoincrement", TokenType::kwAutoincrement},
                {"before", TokenType::kwBefore},
                {"begin", TokenType::kwBegin},
                {"between", TokenType::kwBetween},
                {"by", TokenType::kwBy},
                {"cascade", TokenType::kwCascade},
                {"case", TokenType::kwCase},
                {"cast", TokenType::kwCast},
                {"check", TokenType::kwCheck},
                {"collate", TokenType::kwCollate},
                {"column", TokenType::kwColumn},
                {"commit", TokenType::kwCommit},
                {"conflict", TokenType::kwConflict},
                {"constraint", TokenType::kwConstraint},
                {"create", TokenType::kwCreate},
                {"cross", TokenType::kwCross},
                {"current", TokenType::kwCurrent},
                {"current_date", TokenType::kwCurrentDate},
                {"current_time", TokenType::kwCurrentTime},
                {"current_timestamp", TokenType::kwCurrentTimestamp},
                {"database", TokenType::kwDatabase},
                {"default", TokenType::kwDefault},
                {"deferrable", TokenType::kwDeferrable},
                {"deferred", TokenType::kwDeferred},
                {"delete", TokenType::kwDelete},
                {"desc", TokenType::kwDesc},
                {"detach", TokenType::kwDetach},
                {"distinct", TokenType::kwDistinct},
                {"do", TokenType::kwDo},
                {"drop", TokenType::kwDrop},
                {"each", TokenType::kwEach},
                {"else", TokenType::kwElse},
                {"end", TokenType::kwEnd},
                {"escape", TokenType::kwEscape},
                {"except", TokenType::kwExcept},
                {"exclude", TokenType::kwExclude},
                {"exclusive", TokenType::kwExclusive},
                {"excluded", TokenType::kwExcluded},
                {"exists", TokenType::kwExists},
                {"explain", TokenType::kwExplain},
                {"fail", TokenType::kwFail},
                {"false", TokenType::kwFalse},
                {"filter", TokenType::kwFilter},
                {"first", TokenType::kwFirst},
                {"following", TokenType::kwFollowing},
                {"for", TokenType::kwFor},
                {"foreign", TokenType::kwForeign},
                {"from", TokenType::kwFrom},
                {"full", TokenType::kwFull},
                {"generated", TokenType::kwGenerated},
                {"glob", TokenType::kwGlob},
                {"group", TokenType::kwGroup},
                {"groups", TokenType::kwGroups},
                {"having", TokenType::kwHaving},
                {"if", TokenType::kwIf},
                {"ignore", TokenType::kwIgnore},
                {"immediate", TokenType::kwImmediate},
                {"in", TokenType::kwIn},
                {"index", TokenType::kwIndex},
                {"indexed", TokenType::kwIndexed},
                {"initially", TokenType::kwInitially},
                {"inner", TokenType::kwInner},
                {"insert", TokenType::kwInsert},
                {"instead", TokenType::kwInstead},
                {"intersect", TokenType::kwIntersect},
                {"into", TokenType::kwInto},
                {"is", TokenType::kwIs},
                {"isnull", TokenType::kwIsnull},
                {"join", TokenType::kwJoin},
                {"key", TokenType::kwKey},
                {"last", TokenType::kwLast},
                {"left", TokenType::kwLeft},
                {"like", TokenType::kwLike},
                {"limit", TokenType::kwLimit},
                {"match", TokenType::kwMatch},
                {"materialized", TokenType::kwMaterialized},
                {"natural", TokenType::kwNatural},
                {"no", TokenType::kwNo},
                {"not", TokenType::kwNot},
                {"nothing", TokenType::kwNothing},
                {"notnull", TokenType::kwNotnull},
                {"null", TokenType::kwNull},
                {"nulls", TokenType::kwNulls},
                {"of", TokenType::kwOf},
                {"offset", TokenType::kwOffset},
                {"on", TokenType::kwOn},
                {"or", TokenType::kwOr},
                {"order", TokenType::kwOrder},
                {"others", TokenType::kwOthers},
                {"outer", TokenType::kwOuter},
                {"over", TokenType::kwOver},
                {"partition", TokenType::kwPartition},
                {"plan", TokenType::kwPlan},
                {"pragma", TokenType::kwPragma},
                {"preceding", TokenType::kwPreceding},
                {"primary", TokenType::kwPrimary},
                {"query", TokenType::kwQuery},
                {"raise", TokenType::kwRaise},
                {"range", TokenType::kwRange},
                {"recursive", TokenType::kwRecursive},
                {"references", TokenType::kwReferences},
                {"regexp", TokenType::kwRegexp},
                {"reindex", TokenType::kwReindex},
                {"release", TokenType::kwRelease},
                {"rename", TokenType::kwRename},
                {"replace", TokenType::kwReplace},
                {"restrict", TokenType::kwRestrict},
                {"returning", TokenType::kwReturning},
                {"right", TokenType::kwRight},
                {"rollback", TokenType::kwRollback},
                {"row", TokenType::kwRow},
                {"rows", TokenType::kwRows},
                {"savepoint", TokenType::kwSavepoint},
                {"select", TokenType::kwSelect},
                {"set", TokenType::kwSet},
                {"stored", TokenType::kwStored},
                {"strict", TokenType::kwStrict},
                {"table", TokenType::kwTable},
                {"temp", TokenType::kwTemp},
                {"temporary", TokenType::kwTemporary},
                {"then", TokenType::kwThen},
                {"ties", TokenType::kwTies},
                {"to", TokenType::kwTo},
                {"transaction", TokenType::kwTransaction},
                {"trigger", TokenType::kwTrigger},
                {"true", TokenType::kwTrue},
                {"unbounded", TokenType::kwUnbounded},
                {"union", TokenType::kwUnion},
                {"unique", TokenType::kwUnique},
                {"update", TokenType::kwUpdate},
                {"using", TokenType::kwUsing},
                {"vacuum", TokenType::kwVacuum},
                {"values", TokenType::kwValues},
                {"view", TokenType::kwView},
                {"virtual", TokenType::kwVirtual},
                {"when", TokenType::kwWhen},
                {"where", TokenType::kwWhere},
                {"window", TokenType::kwWindow},
                {"with", TokenType::kwWith},
                {"without", TokenType::kwWithout},
            };
            return map;
        }

        bool isIdentifierStart(char c) {
            return std::isalpha(static_cast<unsigned char>(c)) || c == '_';
        }

        bool isIdentifierChar(char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        }

        std::string toLower(std::string_view sv) {
            std::string result(sv);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            return result;
        }
    }  // namespace

    std::optional<TokenType> keywordFromIdentifier(std::string_view word) {
        auto lower = toLower(word);
        auto& map = keywordMap();
        if(auto it = map.find(std::string_view{lower}); it != map.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::string_view tokenTypeName(TokenType type) {
        switch(type) {
            case TokenType::integerLiteral: return "IntegerLiteral";
            case TokenType::realLiteral: return "RealLiteral";
            case TokenType::stringLiteral: return "StringLiteral";
            case TokenType::blobLiteral: return "BlobLiteral";
            case TokenType::identifier: return "Identifier";
            case TokenType::bindParameter: return "BindParameter";
            case TokenType::plus: return "Plus";
            case TokenType::minus: return "Minus";
            case TokenType::star: return "Star";
            case TokenType::slash: return "Slash";
            case TokenType::percent: return "Percent";
            case TokenType::pipe2: return "Pipe2";
            case TokenType::eq: return "Eq";
            case TokenType::eq2: return "Eq2";
            case TokenType::ne: return "Ne";
            case TokenType::ltGt: return "LtGt";
            case TokenType::lt: return "Lt";
            case TokenType::le: return "Le";
            case TokenType::gt: return "Gt";
            case TokenType::ge: return "Ge";
            case TokenType::ampersand: return "Ampersand";
            case TokenType::pipe: return "Pipe";
            case TokenType::tilde: return "Tilde";
            case TokenType::shiftLeft: return "ShiftLeft";
            case TokenType::shiftRight: return "ShiftRight";
            case TokenType::arrow: return "Arrow";
            case TokenType::arrow2: return "Arrow2";
            case TokenType::leftParen: return "LeftParen";
            case TokenType::rightParen: return "RightParen";
            case TokenType::comma: return "Comma";
            case TokenType::dot: return "Dot";
            case TokenType::semicolon: return "Semicolon";
            case TokenType::eof: return "Eof";
            default:
                if(type >= TokenType::kwAbort && type <= TokenType::kwWithout) {
                    return "Keyword";
                }
                return "Unknown";
        }
    }

    bool Tokenizer::atEnd() const {
        return this->position >= this->sql.size();
    }

    char Tokenizer::peek() const {
        if(atEnd()) return '\0';
        return this->sql[this->position];
    }

    char Tokenizer::peekAhead(size_t offset) const {
        if(this->position + offset >= this->sql.size()) return '\0';
        return this->sql[this->position + offset];
    }

    char Tokenizer::advance() {
        char c = this->sql[this->position++];
        if(c == '\n') {
            ++this->line;
            this->column = 1;
            if(!atEnd() && peek() == '\r') {
                ++this->position;
            }
        } else if(c == '\r') {
            ++this->line;
            this->column = 1;
            if(!atEnd() && peek() == '\n') {
                ++this->position;
            }
        } else {
            ++this->column;
        }
        return c;
    }

    void Tokenizer::skipWhitespaceAndComments() {
        while(!atEnd()) {
            char c = peek();
            if(std::isspace(static_cast<unsigned char>(c))) {
                advance();
            } else if(c == '-' && peekAhead(1) == '-') {
                skipLineComment();
            } else if(c == '/' && peekAhead(1) == '*') {
                skipBlockComment();
            } else {
                break;
            }
        }
    }

    void Tokenizer::skipLineComment() {
        advance();  // -
        advance();  // -
        while(!atEnd() && peek() != '\n') {
            advance();
        }
    }

    void Tokenizer::skipBlockComment() {
        auto location = SourceLocation{this->line, this->column};
        advance();  // /
        advance();  // *
        while(!atEnd()) {
            if(peek() == '*' && peekAhead(1) == '/') {
                advance();  // *
                advance();  // /
                return;
            }
            advance();
        }
        throw TokenizeError("unterminated block comment", location);
    }

    Token Tokenizer::makeToken(TokenType type, size_t start, SourceLocation location) const {
        return Token{type, this->sql.substr(start, this->position - start), location};
    }

    Token Tokenizer::readIdentifierOrKeyword(SourceLocation location) {
        size_t start = this->position;
        while(!atEnd() && isIdentifierChar(peek())) {
            advance();
        }
        auto text = this->sql.substr(start, this->position - start);
        if(auto keyword = keywordFromIdentifier(text)) {
            return Token{*keyword, text, location};
        }
        return Token{TokenType::identifier, text, location};
    }

    Token Tokenizer::readQuotedIdentifier(char quote, SourceLocation location) {
        size_t start = this->position;
        advance();  // opening quote
        char close = (quote == '[') ? ']' : quote;
        while(!atEnd()) {
            char c = advance();
            if(c == close) {
                if(quote != '[' && peek() == close) {
                    advance();  // escaped quote (doubled)
                } else {
                    return Token{TokenType::identifier, this->sql.substr(start, this->position - start), location};
                }
            }
        }
        throw TokenizeError("unterminated quoted identifier", location);
    }

    Token Tokenizer::readStringLiteral(SourceLocation location) {
        size_t start = this->position;
        advance();  // opening '
        while(!atEnd()) {
            char c = advance();
            if(c == '\'') {
                if(peek() == '\'') {
                    advance();  // escaped '' → continue
                } else {
                    return Token{TokenType::stringLiteral, this->sql.substr(start, this->position - start), location};
                }
            }
        }
        throw TokenizeError("unterminated string literal", location);
    }

    Token Tokenizer::readNumericLiteral(SourceLocation location) {
        size_t start = this->position;
        bool isReal = false;

        if(peek() == '0' && (peekAhead(1) == 'x' || peekAhead(1) == 'X')) {
            advance();  // 0
            advance();  // x
            while(!atEnd() && std::isxdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
            return Token{TokenType::integerLiteral, this->sql.substr(start, this->position - start), location};
        }

        while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }

        if(peek() == '.' && std::isdigit(static_cast<unsigned char>(peekAhead(1)))) {
            isReal = true;
            advance();  // .
            while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        } else if(peek() == '.' && !isIdentifierStart(peekAhead(1)) && peekAhead(1) != '.') {
            isReal = true;
            advance();  // .
        }

        if(peek() == 'e' || peek() == 'E') {
            isReal = true;
            advance();
            if(peek() == '+' || peek() == '-') {
                advance();
            }
            if(!std::isdigit(static_cast<unsigned char>(peek()))) {
                throw TokenizeError("invalid numeric literal: expected digit after exponent", location);
            }
            while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }

        auto type = isReal ? TokenType::realLiteral : TokenType::integerLiteral;
        return Token{type, this->sql.substr(start, this->position - start), location};
    }

    Token Tokenizer::readBlobLiteral(SourceLocation location) {
        size_t start = this->position;
        advance();  // X or x
        if(peek() != '\'') {
            this->position = start + 1;
            while(!atEnd() && isIdentifierChar(peek())) {
                advance();
            }
            auto text = this->sql.substr(start, this->position - start);
            if(auto keyword = keywordFromIdentifier(text)) {
                return Token{*keyword, text, location};
            }
            return Token{TokenType::identifier, text, location};
        }
        advance();  // opening '
        while(!atEnd() && peek() != '\'') {
            char c = peek();
            if(!std::isxdigit(static_cast<unsigned char>(c))) {
                throw TokenizeError("invalid hex digit in blob literal", location);
            }
            advance();
        }
        if(atEnd()) {
            throw TokenizeError("unterminated blob literal", location);
        }
        advance();  // closing '
        return Token{TokenType::blobLiteral, this->sql.substr(start, this->position - start), location};
    }

    Token Tokenizer::readBindParameter(SourceLocation location) {
        size_t start = this->position;
        char c = advance();
        if(c == '?') {
            while(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        } else {
            // :name, @name, $name
            if(!atEnd() && isIdentifierStart(peek())) {
                while(!atEnd() && isIdentifierChar(peek())) {
                    advance();
                }
            }
        }
        return Token{TokenType::bindParameter, this->sql.substr(start, this->position - start), location};
    }

    Token Tokenizer::readOperatorOrPunctuation(SourceLocation location) {
        size_t start = this->position;
        char c = advance();
        switch(c) {
            case '(': return makeToken(TokenType::leftParen, start, location);
            case ')': return makeToken(TokenType::rightParen, start, location);
            case ',': return makeToken(TokenType::comma, start, location);
            case ';': return makeToken(TokenType::semicolon, start, location);
            case '~': return makeToken(TokenType::tilde, start, location);
            case '+': return makeToken(TokenType::plus, start, location);
            case '*': return makeToken(TokenType::star, start, location);
            case '/': return makeToken(TokenType::slash, start, location);
            case '%': return makeToken(TokenType::percent, start, location);
            case '&': return makeToken(TokenType::ampersand, start, location);
            case '.': {
                if(!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    this->position = start;
                    return readNumericLiteral(location);
                }
                return makeToken(TokenType::dot, start, location);
            }
            case '|': {
                if(peek() == '|') {
                    advance();
                    return makeToken(TokenType::pipe2, start, location);
                }
                return makeToken(TokenType::pipe, start, location);
            }
            case '=': {
                if(peek() == '=') {
                    advance();
                    return makeToken(TokenType::eq2, start, location);
                }
                return makeToken(TokenType::eq, start, location);
            }
            case '!': {
                if(peek() == '=') {
                    advance();
                    return makeToken(TokenType::ne, start, location);
                }
                throw TokenizeError("unexpected character '!'", location);
            }
            case '<': {
                if(peek() == '=') { advance(); return makeToken(TokenType::le, start, location); }
                if(peek() == '>') { advance(); return makeToken(TokenType::ltGt, start, location); }
                if(peek() == '<') { advance(); return makeToken(TokenType::shiftLeft, start, location); }
                return makeToken(TokenType::lt, start, location);
            }
            case '>': {
                if(peek() == '=') { advance(); return makeToken(TokenType::ge, start, location); }
                if(peek() == '>') { advance(); return makeToken(TokenType::shiftRight, start, location); }
                return makeToken(TokenType::gt, start, location);
            }
            case '-': {
                if(peek() == '>') {
                    advance();
                    if(peek() == '>') { advance(); return makeToken(TokenType::arrow2, start, location); }
                    return makeToken(TokenType::arrow, start, location);
                }
                return makeToken(TokenType::minus, start, location);
            }
            default:
                throw TokenizeError(std::string("unexpected character '") + c + "'", location);
        }
    }

    std::vector<Token> Tokenizer::tokenize(std::string_view sql) {
        this->sql = sql;
        this->position = 0;
        this->line = 1;
        this->column = 1;

        if(this->sql.size() >= 3 && static_cast<unsigned char>(this->sql[0]) == 0xEF &&
           static_cast<unsigned char>(this->sql[1]) == 0xBB && static_cast<unsigned char>(this->sql[2]) == 0xBF) {
            this->position = 3;
        }

        std::vector<Token> tokens;
        tokens.reserve(sql.size() / 4);

        while(true) {
            skipWhitespaceAndComments();
            if(atEnd()) {
                tokens.push_back(Token{TokenType::eof, {}, SourceLocation{this->line, this->column}});
                break;
            }

            auto location = SourceLocation{this->line, this->column};
            char c = peek();

            if(isIdentifierStart(c)) {
                if((c == 'x' || c == 'X') && peekAhead(1) == '\'') {
                    tokens.push_back(readBlobLiteral(location));
                } else {
                    tokens.push_back(readIdentifierOrKeyword(location));
                }
            } else if(c == '"' || c == '`' || c == '[') {
                tokens.push_back(readQuotedIdentifier(c, location));
            } else if(c == '\'') {
                tokens.push_back(readStringLiteral(location));
            } else if(std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(readNumericLiteral(location));
            } else if(c == '?' || c == ':' || c == '@' || c == '$') {
                tokens.push_back(readBindParameter(location));
            } else {
                tokens.push_back(readOperatorOrPunctuation(location));
            }
        }

        return tokens;
    }

}  // namespace sqlite2orm
