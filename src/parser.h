/// --------------------
/// Parser
/// --------------------

#ifndef PARSER_H
#define PARSER_H

#include <string>
#include <memory>
#include <functional>
#include <map>
#include "node.h"
#include "token.h"

enum class GrammarProduction {
    Statement,
    Expr,
    CompExpr,
    ArithExpr,
    Term,
    Factor,
    Power,
    Call,
    Atom,
    ArrayExpr,
    IfExpr,
    ForExpr,
    WhileExpr,
    RepeatExpr,
    AlgoDef,
    StructDef
};

using Grammar = std::map<GrammarProduction, std::string>;

class Parser {
public:
    Parser(const TokenList& _tokens)
        : tokens(_tokens), tok_index(-1) { advance();}
    std::shared_ptr<Token> advance();
    std::shared_ptr<Token> back();
    std::shared_ptr<Node> factor(int tab_expect);
    std::shared_ptr<Node> term(int tab_expect);
    std::shared_ptr<Node> expr(int tab_expect);
    std::shared_ptr<Node> arith_expr(int tab_expect);
    std::shared_ptr<Node> comp_expr(int tab_expect);
    std::shared_ptr<Node> array_expr(int tab_expect, const std::string& closing_token = TOKEN_RIGHT_BRACE);
    std::shared_ptr<Node> if_expr(int tab_expect);
    std::shared_ptr<Node> for_expr(int tab_expect);
    std::shared_ptr<Node> while_expr(int tab_expect);
    std::shared_ptr<Node> repeat_expr(int tab_expect);
    std::shared_ptr<Node> pow(int tab_expect);
    std::shared_ptr<Node> atom(int tab_expect);
    std::shared_ptr<Node> call(int tab_expect);
    std::shared_ptr<Node> algo_def(int tab_expect);
    std::shared_ptr<Node> struct_def(int tab_expect);
    NodeList statement(int tab_expect);
    bool match_type(const std::string& token_type) const;
    bool match_value(const std::string& token_value) const;
    bool match_keyword(const std::string& keyword) const;
    std::shared_ptr<Node> parse_error(const std::string& message);
    static const Grammar& grammar();
    
    std::shared_ptr<Node> bin_op(
        int tab_expect,
        std::function<std::shared_ptr<Node>(int)>, 
        std::vector<std::string>, std::function<std::shared_ptr<Node>(int)>);
    NodeList parse();
protected:
    TokenList tokens;
    std::shared_ptr<Token> current_tok;
    int64_t tok_index;
};

#endif
