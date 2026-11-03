/// --------------------
/// Token
/// --------------------

#ifndef TOKEN_H
#define TOKEN_H

#include <memory>
#include <string>
#include <vector>

#include "position.h"

enum TokenType {
    // Builtin
    TOKEN_KEYWORD,
    TOKEN_IDENTIFIER,
    TOKEN_ASSIGN,
    TOKEN_BUILTIN_CONST,
    TOKEN_BUILTIN_ALGO,

    TOKEN_INT,
    TOKEN_FLOAT,
    // Arithmetic
    TOKEN_ADD,
    TOKEN_SUB,
    TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_MOD,
    TOKEN_POW,
    TOKEN_LEFT_PAREN,
    TOKEN_RIGHT_PAREN,
    // Comparison
    TOKEN_EQUAL,
    TOKEN_NEQ,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LEQ,
    TOKEN_GEQ,
    // function
    TOKEN_COMMA,
    TOKEN_COLON,
    TOKEN_ARGS,
    // string
    TOKEN_STRING,
    // Array
    TOKEN_LEFT_BRACE,
    TOKEN_RIGHT_BRACE,
    TOKEN_LEFT_SQUARE,
    TOKEN_RIGHT_SQUARE,
    TOKEN_DOT,
    TOKEN_SCOPE_RES,
    // Error
    TOKEN_ERROR,
    // Multiline
    TOKEN_NEWLINE,
    TOKEN_SEMICOLON,
    TOKEN_TAB,
    TOKEN_NONE,
};

const char* token_type_name(TokenType type);

class Token {
   public:
    Token(TokenType _type = TOKEN_NONE, Position _pos = Position()) : type(_type), pos(_pos) {}
    virtual std::string get_tok();
    virtual TokenType get_type();
    virtual std::string get_value() { return ""; }
    virtual Position get_pos() { return pos; }
    virtual inline bool isnumber() { return false; }
    friend std::ostream& operator<<(std::ostream& out, Token& token);

   protected:
    TokenType type;
    Position pos;
};

template <typename T>
class TypedToken : public Token {
   public:
    TypedToken(TokenType _type, const Position& _pos, const T& _value)
        : Token(_type, _pos), value(_value) {}
    virtual std::string get_tok();
    virtual std::string get_value();
    virtual inline bool isnumber() { return type == TOKEN_INT || type == TOKEN_FLOAT; }

   protected:
    T value;
};

using TokenList = std::vector<std::shared_ptr<Token>>;
using ErrorToken = TypedToken<std::string>;
using ArgsToken = TypedToken<TokenList>;
std::ostream& operator<<(std::ostream& out, TokenList& tokens);

template class TypedToken<double>;
template class TypedToken<int64_t>;
template class TypedToken<std::string>;

#endif