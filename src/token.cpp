/// --------------------
/// Token
/// --------------------

#include "token.h"

#include <iostream>
#include <sstream>
#include <string>

#include "color.h"

// template class TypedToken<double>;
// template class TypedToken<int64_t>;
// template class TypedToken<std::string>;

const char* token_type_name(TokenType type) {
    switch (type) {
        case TOKEN_KEYWORD:
            return "KEYWORD";
        case TOKEN_IDENTIFIER:
            return "IDENTIFIER";
        case TOKEN_ASSIGN:
            return "ASSIGN";
        case TOKEN_BUILTIN_CONST:
            return "BICONST";
        case TOKEN_BUILTIN_ALGO:
            return "BIALGO";
        case TOKEN_INT:
            return "INT";
        case TOKEN_FLOAT:
            return "FLOAT";
        case TOKEN_ADD:
            return "ADD";
        case TOKEN_SUB:
            return "SUB";
        case TOKEN_MUL:
            return "MUL";
        case TOKEN_DIV:
            return "DIV";
        case TOKEN_MOD:
            return "MOD";
        case TOKEN_POW:
            return "POW";
        case TOKEN_LEFT_PAREN:
            return "LPAREN";
        case TOKEN_RIGHT_PAREN:
            return "RPAREN";
        case TOKEN_EQUAL:
            return "EQUAL";
        case TOKEN_NEQ:
            return "NEQ";
        case TOKEN_LESS:
            return "LESS";
        case TOKEN_GREATER:
            return "GREATER";
        case TOKEN_LEQ:
            return "LEQ";
        case TOKEN_GEQ:
            return "GEQ";
        case TOKEN_COMMA:
            return "COMMA";
        case TOKEN_COLON:
            return "COLON";
        case TOKEN_ARGS:
            return "ARGS";
        case TOKEN_STRING:
            return "STR";
        case TOKEN_LEFT_BRACE:
            return "LBRACE";
        case TOKEN_RIGHT_BRACE:
            return "RBRACE";
        case TOKEN_LEFT_SQUARE:
            return "LSQUARE";
        case TOKEN_RIGHT_SQUARE:
            return "RSQUARE";
        case TOKEN_DOT:
            return "DOT";
        case TOKEN_SCOPE_RES:
            return "SCOPERES";
        case TOKEN_ERROR:
            return "ERROR";
        case TOKEN_NEWLINE:
            return "NEWL";
        case TOKEN_SEMICOLON:
            return "SEMIC";
        case TOKEN_TAB:
            return "TAB";
        case TOKEN_NONE:
            return "NONE";
    }
    return "UNKNOWN";
}

std::string Token::get_tok() { return token_type_name(type); }

TokenType Token::get_type() { return type; }

std::ostream& operator<<(std::ostream& out, Token& token) {
    out << token.get_tok();
    return out;
}

template <typename T>
std::string TypedToken<T>::get_tok() {
    std::stringstream ss;
    if (type == TOKEN_ERROR) ss << Color(0xFF, 0x39, 0x6E);
    ss << token_type_name(type) << ": " << value << RESET;
    if (type == TOKEN_ERROR) ss << Color(0xDB, 0x80, 0xFF) << ". At " << pos << RESET "\n";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

std::ostream& operator<<(std::ostream& out, TokenList& tokens) {
    if (!tokens.empty() && tokens.front()->get_type() == TOKEN_ERROR) {
        out << (*tokens.front());
        return out;
    }

    out << "{";
    for (auto tok : tokens) {
        out << (*tok);
        if (tok != tokens.back()) out << ", ";
    }
    out << "}";
    return out;
}

template <typename T>
std::string TypedToken<T>::get_value() {
    std::stringstream ss;
    ss << value;
    std::string ret;
    std::getline(ss, ret);
    return ret;
}