/// --------------------
/// Lexer
/// --------------------

#ifndef LEXER_H
#define LEXER_H

#include <map>
#include <set>
#include "token.h"

#define NONE 0
#define TAB_SIZE 4

const std::map<char, std::string> TO_TOKEN_TYPE {
    {'+', TOKEN_ADD}, {'-', TOKEN_SUB}, 
    {'*', TOKEN_MUL}, {'/', TOKEN_DIV},
    {'%', TOKEN_MOD}, {'(', TOKEN_LEFT_PAREN},
    {')', TOKEN_RIGHT_PAREN}, {'^', TOKEN_POW},
    {'=', TOKEN_EQUAL}, {',', TOKEN_COMMA},
    {':', TOKEN_COLON}, {'{', TOKEN_LEFT_BRACE},
    {'}', TOKEN_RIGHT_BRACE}, {'[', TOKEN_LEFT_SQUARE},
    {']', TOKEN_RIGHT_SQUARE}, {'\n', TOKEN_NEWLINE},
    {';', TOKEN_SEMICOLON}, {'.', TOKEN_DOT}
};

const std::set<std::string> KEYWORDS{
    "and", "or", "not",
    "for", "to", "step", "while", "do",
    "repeat", "until",
    "if", "then", "else", 
    "Algorithm", "continue", "break",
    "Struct", "self", "operator"
};

const std::map<std::string, int64_t> BUILTIN_CONST{
    {"true", 1}, {"false", 0}, {"none", 0}
};

const std::set<std::string> BUILTIN_ALGO{
    "print", "read", "read_line",
    "open", "clear", "quit",
    "int", "float", "string"
};

const std::map<char, char> ESCAPE_CHAR {
    {'n', '\n'}, {'r', '\r'},
    {'b', '\b'}, {'\"', '\"'},
    {'\'', '\''}, {'\\', '\\'},
    {'t', '\t'}, {'b', '\b'}
};

class Lexer {
public:
    Lexer(const std::string& _file_name, const std::string& _text)
        : file_name(_file_name), text(_text)
        , pos(-1, 0, -1, _file_name), current_char(NONE) {}
    void advance();
    TokenList make_tokens();
    std::shared_ptr<Token> make_number();
    std::shared_ptr<Token> make_identifier();
    std::shared_ptr<Token> make_string();
protected:
    std::string file_name, text;
    Position pos;
    char current_char;
};

#endif