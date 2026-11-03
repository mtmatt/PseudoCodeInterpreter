/// --------------------
/// Lexer
/// --------------------

#include "lexer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "token.h"

namespace {
struct TokenRegex {
    std::regex pattern;
    TokenType type;
};

const std::regex NUMBER_RE(R"(^\d+(?:\.\d*)?)");
const std::regex IDENTIFIER_RE(R"(^[A-Za-z_][A-Za-z0-9_]*)");
const std::regex STRING_RE(R"(^"(?:\\.|[^"\\])*")");
const std::regex WHITESPACE_RE(R"(^[ \t]+)");
const std::regex NEWLINE_INDENT_RE(R"(^\n *)");
const std::regex COMMENT_RE(R"(^//[^\n]*)");

const std::vector<TokenRegex> TOKEN_REGEXES{
    {std::regex(R"(^::)"), TOKEN_SCOPE_RES},   {std::regex(R"(^<-)"), TOKEN_ASSIGN},
    {std::regex(R"(^<=)"), TOKEN_LEQ},         {std::regex(R"(^>=)"), TOKEN_GEQ},
    {std::regex(R"(^!=)"), TOKEN_NEQ},         {std::regex(R"(^\+)"), TOKEN_ADD},
    {std::regex(R"(^-)"), TOKEN_SUB},          {std::regex(R"(^\*)"), TOKEN_MUL},
    {std::regex(R"(^/)"), TOKEN_DIV},          {std::regex(R"(^%)"), TOKEN_MOD},
    {std::regex(R"(^\^)"), TOKEN_POW},         {std::regex(R"(^\()"), TOKEN_LEFT_PAREN},
    {std::regex(R"(^\))"), TOKEN_RIGHT_PAREN}, {std::regex(R"(^=)"), TOKEN_EQUAL},
    {std::regex(R"(^,)"), TOKEN_COMMA},        {std::regex(R"(^:)"), TOKEN_COLON},
    {std::regex(R"(^\{)"), TOKEN_LEFT_BRACE},  {std::regex(R"(^\})"), TOKEN_RIGHT_BRACE},
    {std::regex(R"(^\[)"), TOKEN_LEFT_SQUARE}, {std::regex(R"(^\])"), TOKEN_RIGHT_SQUARE},
    {std::regex(R"(^;)"), TOKEN_SEMICOLON},    {std::regex(R"(^\.)"), TOKEN_DOT},
    {std::regex(R"(^<)"), TOKEN_LESS},         {std::regex(R"(^>)"), TOKEN_GREATER},
};

bool regex_prefix(const std::string& input, const std::regex& regex, std::smatch& match) {
    return std::regex_search(input, match, regex, std::regex_constants::match_continuous);
}
}  // namespace

void Lexer::advance() {
    pos.advance(current_char);
    if (pos.index >= text.size()) {
        current_char = NONE;
        return;
    }
    current_char = text[pos.index];
}

void Lexer::advance_by(const std::string& lexeme) {
    for (char ch : lexeme) {
        current_char = ch;
        pos.advance(current_char);
    }
    current_char = pos.index >= text.size() ? NONE : text[pos.index];
}

std::shared_ptr<Token> Lexer::make_error(const Position& start_pos, const std::string& message) {
    return std::make_shared<ErrorToken>(TOKEN_ERROR, start_pos, message);
}

TokenList Lexer::make_tokens() {
    TokenList tokens;
    pos = Position(-1, 0, -1, file_name);
    current_char = NONE;
    advance();

    while (current_char != NONE) {
        const std::string REST = text.substr(pos.index);
        Position start_pos = pos;
        std::smatch match;

        if (regex_prefix(REST, COMMENT_RE, match)) {
            advance_by(match.str());
            continue;
        }

        if (regex_prefix(REST, NEWLINE_INDENT_RE, match)) {
            const std::string LEXEME = match.str();
            tokens.push_back(std::make_shared<Token>(TOKEN_NEWLINE, start_pos));

            const int SPACE_NUM = static_cast<int>(LEXEME.size()) - 1;
            const std::string LINE_REST = REST.substr(LEXEME.size());
            advance_by(LEXEME);
            if (LINE_REST.empty() || LINE_REST[0] == '\n' || LINE_REST.rfind("//", 0) == 0) {
                continue;
            }
            if (SPACE_NUM % TAB_SIZE != 0) {
                tokens.clear();
                std::string error_msg = "Illegal tab size: ";
                error_msg += std::to_string(SPACE_NUM);
                error_msg += ". Tab Size should be 4n";
                tokens.push_back(make_error(start_pos, error_msg));
                return tokens;
            }
            for (int i{0}; i < (SPACE_NUM / TAB_SIZE); ++i) {
                tokens.push_back(std::make_shared<Token>(TOKEN_TAB, start_pos));
            }
            continue;
        }

        if (regex_prefix(REST, WHITESPACE_RE, match)) {
            advance_by(match.str());
            continue;
        }

        if (current_char == '\"') {
            if (!regex_prefix(REST, STRING_RE, match)) {
                tokens.clear();
                tokens.push_back(make_error(start_pos, "Expected \'\"\'"));
                return tokens;
            }
            std::shared_ptr<Token> new_token = make_string(match.str(), start_pos);
            advance_by(match.str());
            if (new_token->get_type() == TOKEN_ERROR) {
                tokens.clear();
                tokens.push_back(new_token);
                return tokens;
            }
            tokens.push_back(new_token);
            continue;
        }

        if (regex_prefix(REST, NUMBER_RE, match)) {
            tokens.push_back(make_number(match.str(), start_pos));
            advance_by(match.str());
            continue;
        }

        if (regex_prefix(REST, IDENTIFIER_RE, match)) {
            tokens.push_back(make_identifier(match.str(), start_pos));
            advance_by(match.str());
            continue;
        }

        bool matched_operator = false;
        for (const auto& token_regex : TOKEN_REGEXES) {
            if (regex_prefix(REST, token_regex.pattern, match)) {
                tokens.push_back(std::make_shared<Token>(token_regex.type, start_pos));
                advance_by(match.str());
                matched_operator = true;
                break;
            }
        }
        if (matched_operator) continue;

        tokens.clear();
        std::string error_msg = "Illegal char \'";
        error_msg += current_char;
        error_msg += "\'.";
        tokens.push_back(make_error(start_pos, error_msg));
        return tokens;
    }
    return tokens;
}

std::shared_ptr<Token> Lexer::make_number(const std::string& number_str,
                                          const Position& start_pos) {
    if (number_str.find('.') == std::string::npos)
        return std::make_shared<TypedToken<int64_t>>(TOKEN_INT, start_pos, std::stoll(number_str));
    return std::make_shared<TypedToken<double>>(TOKEN_FLOAT, start_pos, std::stod(number_str));
}

std::shared_ptr<Token> Lexer::make_identifier(const std::string& id_str,
                                              const Position& start_pos) {
    TokenType type;
    if (KEYWORDS.count(id_str))
        type = TOKEN_KEYWORD;
    else if (BUILTIN_CONST.count(id_str))
        type = TOKEN_BUILTIN_CONST;
    else if (BUILTIN_ALGO.count(id_str))
        type = TOKEN_BUILTIN_ALGO;
    else
        type = TOKEN_IDENTIFIER;
    return std::make_shared<TypedToken<std::string>>(type, start_pos, id_str);
}

std::shared_ptr<Token> Lexer::make_string(const std::string& string_lexeme,
                                          const Position& start_pos) {
    std::string ret;
    for (size_t i = 1; i + 1 < string_lexeme.size(); ++i) {
        char ch = string_lexeme[i];
        if (ch != '\\') {
            ret += ch;
            continue;
        }

        if (i + 1 >= string_lexeme.size() - 1)
            return make_error(start_pos, "Expected escape character after \'\\\'");

        char escaped = string_lexeme[++i];
        if (!ESCAPE_CHAR.count(escaped)) return make_error(start_pos, "Unknown char after \'\\\'");
        ret += ESCAPE_CHAR.at(escaped);
    }
    return std::make_shared<TypedToken<std::string>>(TOKEN_STRING, start_pos, ret);
}
