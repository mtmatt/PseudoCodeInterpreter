/// --------------------
/// Parser
/// --------------------

#include "parser.h"
#include "color.h"
#include "lexer.h"
#include "node.h"
#include "token.h"
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>

std::shared_ptr<Token> Parser::advance() {
    tok_index++;
    if(tok_index >= 0 && tok_index < tokens.size())
        current_tok = tokens[tok_index];
    else
        current_tok = std::make_shared<Token>();
    return current_tok;
}

std::shared_ptr<Token> Parser::back() {
    tok_index--;
    if(tok_index >= 0 && tok_index < tokens.size())
        current_tok = tokens[tok_index];
    else
        current_tok = std::make_shared<Token>();
    return current_tok;
}

std::shared_ptr<Node> Parser::atom(int tab_expect) {
    std::shared_ptr<Token> tok = current_tok;
    std::string error_msg{"Not an atom, found \""};
    if(tok->isnumber()) {
        advance();
        return std::make_shared<ValueNode>(tok);
    } else if(tok->get_type() == TOKEN_STRING) {
        advance();
        return std::make_shared<ValueNode>(tok);
    } else if(tok->get_type() == TOKEN_BUILTIN_CONST) {
        advance();
        std::shared_ptr<Token> ret{std::make_shared<TypedToken<int64_t>>(TOKEN_INT, tok->get_pos(), BUILTIN_CONST.at(tok->get_value()))};
        return std::make_shared<ValueNode>(ret);
    }  else if(tok->get_type() == TOKEN_BUILTIN_ALGO) {
        advance();
        return std::make_shared<VarAccessNode>(tok);
    } else if(tok->get_type() == TOKEN_LEFT_PAREN) {
        advance();
        std::shared_ptr<Node> e{expr(tab_expect)};
        if(current_tok->get_type() == TOKEN_RIGHT_PAREN) {
            advance();
            return e;
        }
        error_msg += "Expected \')\'" RESET "\n";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    } else if(tok->get_type() == TOKEN_IDENTIFIER) {
        advance();
        if(current_tok->get_type() == TOKEN_ASSIGN) {
            advance();
            std::shared_ptr<Node> ret = expr(tab_expect);
            if(ret->get_type() == NODE_ERROR) return ret;
            return std::make_shared<VarAssignNode>(tok->get_value(), ret);
        }
        return std::make_shared<VarAccessNode>(tok);
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "if") {
        advance();
        return if_expr(tab_expect);
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "for") {
        advance();
        return for_expr(tab_expect);
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "while") {
        advance();
        return while_expr(tab_expect);
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "repeat") {
        advance();
        return repeat_expr(tab_expect);
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "Algorithm") {
        advance();
        return algo_def(tab_expect);
    } else if(tok->get_type() == TOKEN_LEFT_BRACE) {
        advance();
        return array_expr(tab_expect);
    }

    error_msg += "\"" RESET "\n" ;
    std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
    return std::make_shared<ErrorNode>(error_token);
}

std::shared_ptr<Node> Parser::factor(int tab_expect) {
    std::shared_ptr<Token> tok = current_tok;
    if(tok->get_type() == TOKEN_ADD || tok->get_type() == TOKEN_SUB) {
        advance();
        return std::make_shared<UnaryOpNode>(factor(tab_expect), tok);
    }
    return pow(tab_expect);
}

std::shared_ptr<Node> Parser::term(int tab_expect) {
    return bin_op(
        tab_expect,
        std::bind(&Parser::factor, this, tab_expect), 
        {TOKEN_MUL, TOKEN_DIV, TOKEN_MOD}, 
        std::bind(&Parser::factor, this, tab_expect));
}

std::shared_ptr<Node> Parser::expr(int tab_expect) {
    return bin_op(
        tab_expect,
        std::bind(&Parser::comp_expr, this, tab_expect), 
        {"and", "or"}, 
        std::bind(&Parser::comp_expr, this, tab_expect));
}

std::shared_ptr<Node> Parser::comp_expr(int tab_expect) {
    std::shared_ptr<Token> tok = current_tok;
    if(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "not") {
        advance();
        std::shared_ptr<Node> ret = comp_expr(tab_expect);
        if(ret->get_type() == NODE_ERROR) return ret;
        return std::make_shared<UnaryOpNode>(ret, tok);
    }
    return bin_op(
        tab_expect,
        std::bind(&Parser::arith_expr, this, tab_expect), 
        {TOKEN_EQUAL, TOKEN_NEQ, TOKEN_LESS, TOKEN_GREATER, TOKEN_LEQ, TOKEN_GEQ}, 
        std::bind(&Parser::arith_expr, this, tab_expect));
}

std::shared_ptr<Node> Parser::arith_expr(int tab_expect) {
    return bin_op(
        tab_expect,
        std::bind(&Parser::term, this, tab_expect), 
        {TOKEN_ADD, TOKEN_SUB}, 
        std::bind(&Parser::term, this, tab_expect));
}

std::shared_ptr<Node> Parser::array_expr(int tab_expect) {
    NodeList ret;
    if(current_tok->get_type() == TOKEN_RIGHT_BRACE) {
        advance();
        return std::make_shared<ArrayNode>(ret);
    }
    ret.push_back(expr(tab_expect));
    if(ret.back()->get_type() == NODE_ERROR) return ret.back();
    while(current_tok->get_type() == TOKEN_COMMA) {
        advance();
        ret.push_back(expr(tab_expect));
        if(ret.back()->get_type() == NODE_ERROR) return ret.back();
    }
    if(current_tok->get_type() != TOKEN_RIGHT_BRACE) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected a \"}\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    return std::make_shared<ArrayNode>(ret);
}

std::shared_ptr<Node> Parser::if_expr(int tab_expect) {
    std::shared_ptr<Node> condition = expr(tab_expect);
    if(condition->get_type() == NODE_ERROR) return condition;
    if(!(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "then")) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"then\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    NodeList exp;
    if(current_tok->get_type() == TOKEN_NEWLINE)
        exp = statement(tab_expect + 1);
    else
        exp = NodeList{expr(tab_expect)};
    for(auto node : exp)
        if(node->get_type() == NODE_ERROR) return node;
    NodeList els;
    bool has_newline = false;
    if(current_tok->get_type() == TOKEN_NEWLINE) {
        advance();
        for(int i{0}; i < tab_expect; ++i) {
            advance();
        }
        has_newline = true;
    }
    if(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "else") {
        advance();
        if(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "if") {
            advance();
            els = NodeList{if_expr(tab_expect)};
        } else {
            els = statement(tab_expect + 1);
        }
    } else if(has_newline) {
        std::cout << "Before checking" << "\n";
        while(current_tok->get_type() != TOKEN_NEWLINE) {
            std::cout << current_tok->get_type() << "\n";
            back();
        }
    }
    return std::make_shared<IfNode>(condition, exp, els);
}

std::shared_ptr<Node> Parser::for_expr(int tab_expect) {
    std::shared_ptr<Token> var_name = current_tok;
    if(current_tok->get_type() != TOKEN_IDENTIFIER) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"an identifier\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    if(current_tok->get_type() != TOKEN_ASSIGN) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"<-\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    std::shared_ptr<Node> start_value = expr(tab_expect);
    if(start_value->get_type() == NODE_ERROR) return start_value;
    std::shared_ptr<Node> var_assign = std::make_shared<VarAssignNode>(var_name->get_value(), start_value);
    
    if(!(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "to")) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"to\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    std::shared_ptr<Node> end_value = expr(tab_expect);
    if(end_value->get_type() == NODE_ERROR) return end_value;

    std::shared_ptr<Node> step_value = nullptr;
    if(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "step") {
        advance();
        step_value = expr(tab_expect);
        if(step_value->get_type() == NODE_ERROR) return step_value;
    }
    if(!(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "do")) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"do\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    NodeList body_node = statement(tab_expect + 1);
    for(auto node : body_node)
        if(node->get_type() == NODE_ERROR) return node;
    return std::make_shared<ForNode>(var_assign, end_value, step_value, body_node);
}

std::shared_ptr<Node> Parser::while_expr(int tab_expect) {
    std::shared_ptr<Node> condition = expr(tab_expect);
    if(condition->get_type() == NODE_ERROR) return condition;
    if(!(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "do")) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"do\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    NodeList body_node = statement(tab_expect + 1);
    for(auto node : body_node)
        if(node->get_type() == NODE_ERROR) return node;
    return std::make_shared<WhileNode>(condition, body_node);
}

std::shared_ptr<Node> Parser::repeat_expr(int tab_expect) {
    NodeList body_node = statement(tab_expect + 1);
    for(auto node : body_node)
        if(node->get_type() == NODE_ERROR) return node;
    if(!(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "until")) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected \"until\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    std::shared_ptr<Node> condition = expr(tab_expect);
    if(condition->get_type() == NODE_ERROR) return condition;
    return std::make_shared<RepeatNode>(body_node, condition);
}

std::shared_ptr<Node> Parser::algo_def(int tab_expect) {
    std::shared_ptr<Token> algo_name = current_tok;
    if(current_tok->get_type() == TOKEN_IDENTIFIER) {
        advance();
        if(current_tok->get_type() != TOKEN_LEFT_PAREN) {
            std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected a \"(\"\n" RESET;
            std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
            return std::make_shared<ErrorNode>(error_token);
        }
    } else if(current_tok->get_type() != TOKEN_LEFT_PAREN) {
        algo_name = std::make_shared<TypedToken<std::string>>(TOKEN_IDENTIFIER, current_tok->get_pos(), "Anonymous");
    } else {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected an \"identifier\" or \"(\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    TokenList args_name;
    if(current_tok->get_type() == TOKEN_IDENTIFIER) {
        args_name.push_back(current_tok);
        advance();
        while(current_tok->get_type() == TOKEN_COMMA) {
            advance();
            if(current_tok->get_type() != TOKEN_IDENTIFIER) {
                std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected an \"identifier\" or a \"(\"\n" RESET;
                std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
                return std::make_shared<ErrorNode>(error_token);
            }
            args_name.push_back(current_tok);
            advance();
        }
    }

    if(current_tok->get_type() != TOKEN_RIGHT_PAREN) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected a \")\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    if(current_tok->get_type() != TOKEN_COLON) {
        std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected a \":\"\n" RESET;
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    NodeList body_node = statement(tab_expect + 1);
    for(auto node : body_node)
        if(node->get_type() == NODE_ERROR) return node;
    return std::make_shared<AlgorithmDefNode>(algo_name, args_name, body_node);
}

std::shared_ptr<Node> Parser::pow(int tab_expect) {
    return bin_op(
        tab_expect,
        std::bind(&Parser::call, this, tab_expect), 
        {TOKEN_POW}, std::bind(&Parser::factor, this, tab_expect));
}

std::shared_ptr<Node> Parser::call(int tab_expect) {
    std::shared_ptr<Node> at{atom(tab_expect)};
    if(at->get_type() == NODE_ERROR) return at;
    if(current_tok->get_type() == TOKEN_DOT) {
        advance();
        std::shared_ptr<Node> member{atom(tab_expect)};
        at = std::make_shared<MemberAccessNode>(at, member);
    }
    if(current_tok->get_type() != TOKEN_LEFT_PAREN && current_tok->get_type() != TOKEN_LEFT_SQUARE) {
        return at;
    }
    std::shared_ptr<Node> ret;
    while(current_tok->get_type() == TOKEN_LEFT_PAREN || current_tok->get_type() == TOKEN_LEFT_SQUARE) {
        while(current_tok->get_type() == TOKEN_LEFT_PAREN) {
            advance();
            NodeList args;
            if(current_tok->get_type() != TOKEN_RIGHT_PAREN)  {
                args.push_back(expr(tab_expect));
                if(args.back()->get_type() == NODE_ERROR) return args.back();
                while(current_tok->get_type() == TOKEN_COMMA) {
                    advance();
                    args.push_back(expr(tab_expect));
                    if(args.back()->get_type() == NODE_ERROR) return args.back();
                }
                if(current_tok->get_type() != TOKEN_RIGHT_PAREN) {
                    std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected a \")\"\n" RESET;
                    std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
                    return std::make_shared<ErrorNode>(error_token);
                }
            }
            advance();
            ret = std::make_shared<AlgorithmCallNode>(at, args);
            at = ret;
        }
        while(current_tok->get_type() == TOKEN_LEFT_SQUARE) {
            advance();
            std::shared_ptr<Node> index{expr(tab_expect)};
            if(index->get_type() == NODE_ERROR) return index;
            if(current_tok->get_type() != TOKEN_RIGHT_SQUARE) {
                std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected a \"]\"" RESET "\n";
                std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
                return std::make_shared<ErrorNode>(error_token);
            }
            advance();
            ret = std::make_shared<ArrayAccessNode>(at, index);
            at = ret;
        }
    }
    if(current_tok->get_type() == TOKEN_ASSIGN) {
        advance();
        std::shared_ptr<Node> val = expr(tab_expect);
        if(val->get_type() == NODE_ERROR) return val;
        return std::make_shared<ArrayAssignNode>(ret, val);
    }
    return ret;
}

std::shared_ptr<Node> Parser::bin_op(
    int tab_expect,
    std::function<std::shared_ptr<Node>(int tab_expect)> lfunc, 
    std::vector<std::string> allowed_types, 
    std::function<std::shared_ptr<Node>(int tab_expect)> rfunc
) {
    std::shared_ptr<Node> left = lfunc(tab_expect);
    if(left->get_type() == NODE_ERROR) 
        return left;
    while(
        std::find(allowed_types.begin(), allowed_types.end(), current_tok->get_type()) != allowed_types.end() ||
        std::find(allowed_types.begin(), allowed_types.end(), current_tok->get_value()) != allowed_types.end()
    ) {
        std::shared_ptr<Token> op_tok = current_tok;
        advance();
        std::shared_ptr<Node> right = rfunc(tab_expect);
        if(right->get_type() == NODE_ERROR) 
            return right;
        left = std::make_shared<BinOpNode>(left, right, op_tok);
    }
    return left;
}

NodeList Parser::statement(int tab_expect) {
    NodeList ret;
    do {
        while(current_tok->get_type() == TOKEN_NEWLINE) {
            std::shared_ptr<Token> tok = current_tok;
            advance();
            for(int i{0}; i < tab_expect; ++i) {
                if(current_tok->get_type() != TOKEN_TAB) {
                    while(current_tok->get_type() != TOKEN_NEWLINE) {
                        back();
                    }
                    return ret;
                }
                advance();
            }
                
            if(current_tok->get_type() == TOKEN_TAB) {
                std::string error_msg = Color(0xFF, 0x39, 0x6E).get() + "Expected " + std::to_string(tab_expect) + " tabs" RESET "\n";
                std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, tok->get_pos(), error_msg);
                ret.clear();
                ret.push_back(std::make_shared<ErrorNode>(error_token));
                return ret;
            }
        }
        while(current_tok->get_type() == TOKEN_SEMICOLON)
            advance();
        if(current_tok->get_type() != TOKEN_NONE)
            ret.push_back(expr(tab_expect));
    } while(current_tok->get_type() == TOKEN_NEWLINE || current_tok->get_type() == TOKEN_SEMICOLON);
    return ret;
}

NodeList Parser::parse() {
    NodeList ret = statement(0);
    return ret;
}