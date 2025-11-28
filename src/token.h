/// --------------------
/// Token
/// --------------------

#ifndef TOKEN_H
#define TOKEN_H

#include <string>
#include <vector>
#include <memory>
#include "position.h"

// Builtin
const std::string TOKEN_KEYWORD{"KEYWORD"};
const std::string TOKEN_IDENTIFIER{"IDENTIFIER"};
const std::string TOKEN_ASSIGN{"ASSIGN"};
const std::string TOKEN_BUILTIN_CONST{"BICONST"};
const std::string TOKEN_BUILTIN_ALGO{"BIALGO"};

const std::string TOKEN_INT{"INT"};
const std::string TOKEN_FLOAT{"FLOAT"};
// Arithmetic
const std::string TOKEN_ADD{"ADD"};
const std::string TOKEN_SUB{"SUB"};
const std::string TOKEN_MUL{"MUL"};
const std::string TOKEN_DIV{"DIV"};
const std::string TOKEN_MOD{"MOD"};
const std::string TOKEN_POW{"POW"};
const std::string TOKEN_LEFT_PAREN{"LPAREN"};
const std::string TOKEN_RIGHT_PAREN{"RPAREN"};
// Comparison
const std::string TOKEN_EQUAL{"EQUAL"};
const std::string TOKEN_NEQ{"NEQ"};
const std::string TOKEN_LESS{"LESS"};
const std::string TOKEN_GREATER{"GREATER"};
const std::string TOKEN_LEQ{"LEQ"};
const std::string TOKEN_GEQ{"GEQ"};
// function
const std::string TOKEN_COMMA{"COMMA"};
const std::string TOKEN_COLON{"COLON"};
const std::string TOKEN_ARGS{"ARGS"};
// string
const std::string TOKEN_STRING{"STR"};
// Array
const std::string TOKEN_LEFT_BRACE{"LBRACE"};
const std::string TOKEN_RIGHT_BRACE{"RBRACE"};
const std::string TOKEN_LEFT_SQUARE{"LSQUARE"};
const std::string TOKEN_RIGHT_SQUARE{"RSQUARE"};
const std::string TOKEN_DOT{"DOT"};
const std::string TOKEN_SCOPE_RES{"SCOPERES"};
// Error
const std::string TOKEN_ERROR{"ERROR"};
// Multiline
const std::string TOKEN_NEWLINE{"NEWL"};
const std::string TOKEN_SEMICOLON{"SEMIC"};
const std::string TOKEN_TAB{"TAB"};
const std::string TOKEN_NONE{"NONE"};

class Token {
public:
    Token(const std::string& _type = TOKEN_NONE, Position _pos = Position())
        : type(_type), pos(_pos) {}
    virtual std::string get_tok();
    virtual std::string get_type();
    virtual std::string get_value() { return "";}
    virtual Position get_pos() { return pos;}
    virtual inline bool isnumber() { return false;}
    friend std::ostream& operator<<(std::ostream &out, Token &token);
protected:
    std::string type;
    Position pos;
};

template<typename T>
class TypedToken: public Token {
public:
    TypedToken(const std::string& _type, const Position &_pos, const T &_value) 
        : Token(_type, _pos), value(_value) {}
    virtual std::string get_tok();
    virtual std::string get_value();
    virtual inline bool isnumber() { return type == TOKEN_INT || type == TOKEN_FLOAT;}
protected:
    T value;
};

using TokenList = std::vector<std::shared_ptr<Token>>;
using ErrorToken = TypedToken<std::string>;
using ArgsToken = TypedToken<TokenList>;
std::ostream& operator<<(std::ostream &out, TokenList &tokens);

template class TypedToken<double>;
template class TypedToken<int64_t>;
template class TypedToken<std::string>;

#endif