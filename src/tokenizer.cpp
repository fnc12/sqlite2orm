#include <sqlite2orm/tokenizer.h>
#include <sqlite2orm/utils.h>

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

        // SQLite treats any byte >= 0x80 as an identifier character (it never decodes UTF-8),
        // so non-ASCII identifiers like `café` or `привет` are accepted unquoted. Match that;
        // the codegen sanitises such names into a valid C++ identifier separately.
        bool isIdentifierStart(char c) {
            const unsigned char uc = static_cast<unsigned char>(c);
            return std::isalpha(uc) || c == '_' || uc >= 0x80;
        }

        bool isIdentifierChar(char c) {
            const unsigned char uc = static_cast<unsigned char>(c);
            return std::isalnum(uc) || c == '_' || uc >= 0x80;
        }

        bool isNumericDigit(char character, bool hexadecimal) {
            const unsigned char byte = static_cast<unsigned char>(character);
            return hexadecimal ? std::isxdigit(byte) != 0 : std::isdigit(byte) != 0;
        }

        // SQLite accepts a `_` digit separator only between two digits (hex digits inside a hex
        // literal). It scans the literal greedily and checks the separators afterwards, so a
        // misplaced one rejects the whole literal instead of ending it.
        bool hasMisplacedDigitSeparator(std::string_view text, bool hexadecimal) {
            for (size_t index = 0; index < text.size(); ++index) {
                if (text[index] != '_') {
                    continue;
                }
                if (index == 0 || index + 1 == text.size() || !isNumericDigit(text[index - 1], hexadecimal) ||
                    !isNumericDigit(text[index + 1], hexadecimal)) {
                    return true;
                }
            }
            return false;
        }

        TokenizeError unrecognizedToken(std::string_view text, SourceLocation location) {
            return TokenizeError("unrecognized token '" + std::string(text) + "'", location);
        }

        std::string toLower(std::string_view sv) {
            std::string result(sv);
            std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) {
                return std::tolower(c);
            });
            return result;
        }
    }  // namespace

    std::optional<TokenType> keywordFromIdentifier(std::string_view word) {
        auto lower = toLower(word);
        auto& map = keywordMap();
        if (auto it = map.find(std::string_view{lower}); it != map.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    bool isKeywordUsableAsName(TokenType type) {
        if (type < TokenType::kwAbort || type > TokenType::kwWithout) {
            return false;
        }
        switch (type) {
            // SQLite's reserved words, the keywords its parser never falls back to an identifier
            // for. Everything else it knows is a name wherever the grammar asks for one, which is
            // why `CREATE TABLE t(row INTEGER)` is a table and `CREATE TABLE t(set INTEGER)` is a
            // syntax error. Checked against sqlite3 3.51 over every keyword in the map above.
            case TokenType::kwAdd:
            case TokenType::kwAll:
            case TokenType::kwAlter:
            case TokenType::kwAnd:
            case TokenType::kwAs:
            case TokenType::kwAutoincrement:
            case TokenType::kwBetween:
            case TokenType::kwCase:
            case TokenType::kwCheck:
            case TokenType::kwCollate:
            case TokenType::kwCommit:
            case TokenType::kwConstraint:
            case TokenType::kwCreate:
            case TokenType::kwDefault:
            case TokenType::kwDeferrable:
            case TokenType::kwDelete:
            case TokenType::kwDistinct:
            case TokenType::kwDrop:
            case TokenType::kwElse:
            case TokenType::kwEscape:
            case TokenType::kwExcept:
            case TokenType::kwExists:
            case TokenType::kwForeign:
            case TokenType::kwFrom:
            case TokenType::kwGroup:
            case TokenType::kwHaving:
            case TokenType::kwIn:
            case TokenType::kwIndex:
            case TokenType::kwInsert:
            case TokenType::kwIntersect:
            case TokenType::kwInto:
            case TokenType::kwIs:
            case TokenType::kwIsnull:
            case TokenType::kwJoin:
            case TokenType::kwLimit:
            case TokenType::kwNot:
            case TokenType::kwNothing:
            case TokenType::kwNotnull:
            case TokenType::kwNull:
            case TokenType::kwOn:
            case TokenType::kwOr:
            case TokenType::kwOrder:
            case TokenType::kwPrimary:
            case TokenType::kwReferences:
            case TokenType::kwReturning:
            case TokenType::kwSelect:
            case TokenType::kwSet:
            case TokenType::kwTable:
            case TokenType::kwThen:
            case TokenType::kwTo:
            case TokenType::kwTransaction:
            case TokenType::kwUnion:
            case TokenType::kwUnique:
            case TokenType::kwUpdate:
            case TokenType::kwUsing:
            case TokenType::kwValues:
            case TokenType::kwWhen:
            case TokenType::kwWhere:
                return false;
            default:
                return true;
        }
    }

    std::string_view tokenTypeName(TokenType type) {
        switch (type) {
            case TokenType::integerLiteral:
                return "IntegerLiteral";
            case TokenType::realLiteral:
                return "RealLiteral";
            case TokenType::stringLiteral:
                return "StringLiteral";
            case TokenType::blobLiteral:
                return "BlobLiteral";
            case TokenType::identifier:
                return "Identifier";
            case TokenType::bindParameter:
                return "BindParameter";
            case TokenType::plus:
                return "Plus";
            case TokenType::minus:
                return "Minus";
            case TokenType::star:
                return "Star";
            case TokenType::slash:
                return "Slash";
            case TokenType::percent:
                return "Percent";
            case TokenType::pipe2:
                return "Pipe2";
            case TokenType::eq:
                return "Eq";
            case TokenType::eq2:
                return "Eq2";
            case TokenType::ne:
                return "Ne";
            case TokenType::ltGt:
                return "LtGt";
            case TokenType::lt:
                return "Lt";
            case TokenType::le:
                return "Le";
            case TokenType::gt:
                return "Gt";
            case TokenType::ge:
                return "Ge";
            case TokenType::ampersand:
                return "Ampersand";
            case TokenType::pipe:
                return "Pipe";
            case TokenType::tilde:
                return "Tilde";
            case TokenType::shiftLeft:
                return "ShiftLeft";
            case TokenType::shiftRight:
                return "ShiftRight";
            case TokenType::arrow:
                return "Arrow";
            case TokenType::arrow2:
                return "Arrow2";
            case TokenType::leftParen:
                return "LeftParen";
            case TokenType::rightParen:
                return "RightParen";
            case TokenType::comma:
                return "Comma";
            case TokenType::dot:
                return "Dot";
            case TokenType::semicolon:
                return "Semicolon";
            case TokenType::eof:
                return "Eof";
            default:
                if (type >= TokenType::kwAbort && type <= TokenType::kwWithout) {
                    return "Keyword";
                }
                return "Unknown";
        }
    }

    bool Tokenizer::atEnd() const {
        return this->position >= this->sql.size();
    }

    char Tokenizer::peek() const {
        if (atEnd())
            return '\0';
        return this->sql[this->position];
    }

    char Tokenizer::peekAhead(size_t offset) const {
        if (this->position + offset >= this->sql.size())
            return '\0';
        return this->sql[this->position + offset];
    }

    char Tokenizer::advance() {
        char c = this->sql[this->position++];
        if (c == '\n') {
            ++this->line;
            this->column = 1;
            if (!atEnd() && peek() == '\r') {
                ++this->position;
            }
        } else if (c == '\r') {
            ++this->line;
            this->column = 1;
            if (!atEnd() && peek() == '\n') {
                ++this->position;
            }
        } else if (!isUtf8ContinuationByte(c)) {
            // `column` counts characters, which is what a location a diagnostic carries promises:
            // the bytes that continue a multi-byte UTF-8 character belong to the character its
            // leading byte has already counted. SQLite takes such characters in bare identifiers
            // (`CREATE VIEW ü`) as well as inside quoted names and string literals.
            ++this->column;
        }
        return c;
    }

    void Tokenizer::skipWhitespaceAndComments() {
        while (!atEnd()) {
            char c = peek();
            if (std::isspace(static_cast<unsigned char>(c))) {
                advance();
            } else if (c == '-' && peekAhead(1) == '-') {
                skipLineComment();
            } else if (c == '/' && peekAhead(1) == '*') {
                skipBlockComment();
            } else {
                break;
            }
        }
    }

    void Tokenizer::skipLineComment() {
        advance();  // -
        advance();  // -
        while (!atEnd() && peek() != '\n') {
            advance();
        }
    }

    void Tokenizer::skipBlockComment() {
        auto location = SourceLocation{this->line, this->column};
        advance();  // /
        advance();  // *
        while (!atEnd()) {
            if (peek() == '*' && peekAhead(1) == '/') {
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
        while (!atEnd() && isIdentifierChar(peek())) {
            advance();
        }
        auto text = this->sql.substr(start, this->position - start);
        if (auto keyword = keywordFromIdentifier(text)) {
            return Token{*keyword, text, location};
        }
        return Token{TokenType::identifier, text, location};
    }

    Token Tokenizer::readQuotedIdentifier(char quote, SourceLocation location) {
        size_t start = this->position;
        advance();  // opening quote
        char close = (quote == '[') ? ']' : quote;
        while (!atEnd()) {
            char c = advance();
            if (c == close) {
                if (quote != '[' && peek() == close) {
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
        while (!atEnd()) {
            char c = advance();
            if (c == '\'') {
                if (peek() == '\'') {
                    advance();  // escaped '' → continue
                } else {
                    return Token{TokenType::stringLiteral, this->sql.substr(start, this->position - start), location};
                }
            }
        }
        throw TokenizeError("unterminated string literal", location);
    }

    // Consumes a run of digits together with any `_` digit separators in it. Like SQLite, the run
    // is taken greedily and separator placement is checked afterwards, so the whole literal is
    // reported as one unrecognized token instead of being split up.
    void Tokenizer::readDigitsWithSeparators(bool hexadecimal) {
        while (!atEnd() && (isNumericDigit(peek(), hexadecimal) || peek() == '_')) {
            advance();
        }
    }

    Token Tokenizer::readNumericLiteral(SourceLocation location) {
        size_t start = this->position;
        auto type = TokenType::integerLiteral;
        bool hexadecimal = false;

        // `0x` only starts a hex literal when a hex digit follows it; otherwise SQLite reads the
        // leading `0` as a decimal literal and the rest as trailing identifier characters, which
        // is what makes `0x`, `0xg` and `0x_1f` unrecognized tokens.
        if (peek() == '0' && (peekAhead(1) == 'x' || peekAhead(1) == 'X') && isNumericDigit(peekAhead(2), true)) {
            hexadecimal = true;
            advance();  // 0
            advance();  // x
            readDigitsWithSeparators(true);
        } else {
            readDigitsWithSeparators(false);

            // A `.` makes the literal a float whatever follows it, so `1.` and `1.e5` are floats
            // while `1.x` and `1._2` are rejected below.
            if (peek() == '.') {
                type = TokenType::realLiteral;
                advance();  // .
                readDigitsWithSeparators(false);
            }

            // An exponent needs at least one digit after the optional sign, otherwise the `e` is
            // not part of the number at all and `1e`, `1e+` and `1e_1` stay unrecognized tokens.
            const bool exponentHasDigits = std::isdigit(static_cast<unsigned char>(peekAhead(1))) ||
                                           ((peekAhead(1) == '+' || peekAhead(1) == '-') &&
                                            std::isdigit(static_cast<unsigned char>(peekAhead(2))));
            if ((peek() == 'e' || peek() == 'E') && exponentHasDigits) {
                type = TokenType::realLiteral;
                advance();  // e
                if (peek() == '+' || peek() == '-') {
                    advance();
                }
                readDigitsWithSeparators(false);
            }
        }

        // SQLite never splits a numeric literal followed directly by identifier characters into a
        // number plus an identifier: the whole run becomes one unrecognized token, which is how
        // `1a`, `0x_1f` and `100_abc` are rejected.
        if (!atEnd() && isIdentifierChar(peek())) {
            while (!atEnd() && isIdentifierChar(peek())) {
                advance();
            }
            throw unrecognizedToken(this->sql.substr(start, this->position - start), location);
        }

        auto text = this->sql.substr(start, this->position - start);
        if (hasMisplacedDigitSeparator(text, hexadecimal)) {
            throw unrecognizedToken(text, location);
        }

        // A hex literal a signed 64-bit integer cannot hold is not the lexer's business: SQLite
        // raises `hex literal too big` in codeInteger(), when it compiles an expression, so
        // `CREATE TABLE t(x DEFAULT 0x10000000000000000)` and `PRAGMA user_version =
        // 0x10000000000000000` — neither of which ever compiles the value — are accepted whole.
        // Codegen refuses the literal where SQLite does; see `hexLiteralExceedsInt64`.
        return Token{type, text, location};
    }

    Token Tokenizer::readBlobLiteral(SourceLocation location) {
        size_t start = this->position;
        advance();  // X or x
        if (peek() != '\'') {
            this->position = start + 1;
            while (!atEnd() && isIdentifierChar(peek())) {
                advance();
            }
            auto text = this->sql.substr(start, this->position - start);
            if (auto keyword = keywordFromIdentifier(text)) {
                return Token{*keyword, text, location};
            }
            return Token{TokenType::identifier, text, location};
        }
        advance();  // opening '
        while (!atEnd() && peek() != '\'') {
            char c = peek();
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                throw TokenizeError("invalid hex digit in blob literal", location);
            }
            advance();
        }
        if (atEnd()) {
            throw TokenizeError("unterminated blob literal", location);
        }
        advance();  // closing '
        return Token{TokenType::blobLiteral, this->sql.substr(start, this->position - start), location};
    }

    Token Tokenizer::readBindParameter(SourceLocation location) {
        size_t start = this->position;
        char c = advance();
        if (c == '?') {
            while (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        } else {
            // :name, @name, $name
            if (!atEnd() && isIdentifierStart(peek())) {
                while (!atEnd() && isIdentifierChar(peek())) {
                    advance();
                }
            }
        }
        return Token{TokenType::bindParameter, this->sql.substr(start, this->position - start), location};
    }

    Token Tokenizer::readOperatorOrPunctuation(SourceLocation location) {
        size_t start = this->position;
        char c = advance();
        switch (c) {
            case '(':
                return makeToken(TokenType::leftParen, start, location);
            case ')':
                return makeToken(TokenType::rightParen, start, location);
            case ',':
                return makeToken(TokenType::comma, start, location);
            case ';':
                return makeToken(TokenType::semicolon, start, location);
            case '~':
                return makeToken(TokenType::tilde, start, location);
            case '+':
                return makeToken(TokenType::plus, start, location);
            case '*':
                return makeToken(TokenType::star, start, location);
            case '/':
                return makeToken(TokenType::slash, start, location);
            case '%':
                return makeToken(TokenType::percent, start, location);
            case '&':
                return makeToken(TokenType::ampersand, start, location);
            case '.': {
                if (!atEnd() && std::isdigit(static_cast<unsigned char>(peek()))) {
                    this->position = start;
                    return readNumericLiteral(location);
                }
                return makeToken(TokenType::dot, start, location);
            }
            case '|': {
                if (peek() == '|') {
                    advance();
                    return makeToken(TokenType::pipe2, start, location);
                }
                return makeToken(TokenType::pipe, start, location);
            }
            case '=': {
                if (peek() == '=') {
                    advance();
                    return makeToken(TokenType::eq2, start, location);
                }
                return makeToken(TokenType::eq, start, location);
            }
            case '!': {
                if (peek() == '=') {
                    advance();
                    return makeToken(TokenType::ne, start, location);
                }
                throw TokenizeError("unexpected character '!'", location);
            }
            case '<': {
                if (peek() == '=') {
                    advance();
                    return makeToken(TokenType::le, start, location);
                }
                if (peek() == '>') {
                    advance();
                    return makeToken(TokenType::ltGt, start, location);
                }
                if (peek() == '<') {
                    advance();
                    return makeToken(TokenType::shiftLeft, start, location);
                }
                return makeToken(TokenType::lt, start, location);
            }
            case '>': {
                if (peek() == '=') {
                    advance();
                    return makeToken(TokenType::ge, start, location);
                }
                if (peek() == '>') {
                    advance();
                    return makeToken(TokenType::shiftRight, start, location);
                }
                return makeToken(TokenType::gt, start, location);
            }
            case '-': {
                if (peek() == '>') {
                    advance();
                    if (peek() == '>') {
                        advance();
                        return makeToken(TokenType::arrow2, start, location);
                    }
                    return makeToken(TokenType::arrow, start, location);
                }
                return makeToken(TokenType::minus, start, location);
            }
            default:
                // Only ASCII reaches here: bytes >= 0x80 are identifier characters (see
                // isIdentifierStart), so `c` is always a single valid-UTF-8 byte.
                throw TokenizeError(std::string("unexpected character '") + c + "'", location);
        }
    }

    std::vector<Token> Tokenizer::tokenize(std::string_view sql) {
        this->sql = sql;
        this->position = 0;
        this->line = 1;
        this->column = 1;

        if (this->sql.size() >= 3 && static_cast<unsigned char>(this->sql[0]) == 0xEF &&
            static_cast<unsigned char>(this->sql[1]) == 0xBB && static_cast<unsigned char>(this->sql[2]) == 0xBF) {
            this->position = 3;
        }

        std::vector<Token> tokens;
        tokens.reserve(sql.size() / 4);

        while (true) {
            skipWhitespaceAndComments();
            if (atEnd()) {
                tokens.push_back(Token{TokenType::eof, {}, SourceLocation{this->line, this->column}});
                break;
            }

            auto location = SourceLocation{this->line, this->column};
            char c = peek();

            if (isIdentifierStart(c)) {
                if ((c == 'x' || c == 'X') && peekAhead(1) == '\'') {
                    tokens.push_back(readBlobLiteral(location));
                } else {
                    tokens.push_back(readIdentifierOrKeyword(location));
                }
            } else if (c == '"' || c == '`' || c == '[') {
                tokens.push_back(readQuotedIdentifier(c, location));
            } else if (c == '\'') {
                tokens.push_back(readStringLiteral(location));
            } else if (std::isdigit(static_cast<unsigned char>(c))) {
                tokens.push_back(readNumericLiteral(location));
            } else if (c == '?' || c == ':' || c == '@' || c == '$') {
                tokens.push_back(readBindParameter(location));
            } else {
                tokens.push_back(readOperatorOrPunctuation(location));
            }
        }

        return tokens;
    }

}  // namespace sqlite2orm
