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
        error_msg += "Expected \')\'";
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
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "self") {
        advance();
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
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "Struct") {
        advance();
        return struct_def(tab_expect);
    } else if(tok->get_type() == TOKEN_LEFT_BRACE) {
        advance();
        return array_expr(tab_expect);
    } else if(tok->get_type() == TOKEN_KEYWORD && tok->get_value() == "return") {
        advance();
        if (current_tok->get_type() == TOKEN_NEWLINE || current_tok->get_type() == TOKEN_SEMICOLON) {
             return std::make_shared<ReturnNode>(std::make_shared<ValueNode>(std::make_shared<Token>(TOKEN_BUILTIN_CONST, tok->get_pos()))); // Return NONE
        }
        std::shared_ptr<Node> ret_val = expr(tab_expect);
        if(ret_val->get_type() == NODE_ERROR) return ret_val;
        return std::make_shared<ReturnNode>(ret_val);
    }

    error_msg += "\"";
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
        std::string error_msg = "Expected a \"}\"";
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
        std::string error_msg = "Expected \"then\"";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    NodeList exp;
    if(current_tok->get_type() == TOKEN_NEWLINE) {
        // Fix indentation check in statement()
        // statement expects tokens to be exactly tab_expect + 1 indented if they start with newline
        // but expr() consumes newline? No.
        // statement() logic:
        // while newline:
        //    check tabs
        //    parse expr
        exp = statement(tab_expect + 1);
    }
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
        while(current_tok->get_type() != TOKEN_NEWLINE) {
            back();
        }
    }
    return std::make_shared<IfNode>(condition, exp, els);
}

std::shared_ptr<Node> Parser::for_expr(int tab_expect) {
    std::shared_ptr<Token> var_name = current_tok;
    if(current_tok->get_type() != TOKEN_IDENTIFIER) {
        std::string error_msg = "Expected \"an identifier\"";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    if(current_tok->get_type() != TOKEN_ASSIGN) {
        std::string error_msg = "Expected \"<-\"";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    std::shared_ptr<Node> start_value = expr(tab_expect);
    if(start_value->get_type() == NODE_ERROR) return start_value;
    std::shared_ptr<Node> var_assign = std::make_shared<VarAssignNode>(var_name->get_value(), start_value);
    
    if(!(current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "to")) {
        std::string error_msg = "Expected \"to\"";
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
        std::string error_msg = "Expected \"do\"";
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
        std::string error_msg = "Expected \"do\"";
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
        std::string error_msg = "Expected \"until\"";
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
        if(current_tok->get_type() == TOKEN_SCOPE_RES) {
             std::shared_ptr<Token> struct_name = algo_name;
             advance();
             if (current_tok->get_type() != TOKEN_IDENTIFIER && current_tok->get_type() != TOKEN_KEYWORD) {
                  std::string error_msg = "Expected method name after ::";
                  std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
                  return std::make_shared<ErrorNode>(error_token);
             }
             // For now, construct a name like "StructName::MethodName" to handle it easily in symbol table?
             // Or better, handle it in interpreter.
             // Let's modify AlgorithmDefNode to optionally support struct scope.
             // But for now, let's just mangle the name or use the existing structure.
             // The parser creates an AlgorithmDefNode which takes a Token for name.
             // We can create a new Token with the mangled name, or update AlgorithmDefNode.
             // Let's just create a new Token with name "Struct::Method".
             std::string full_name = struct_name->get_value() + "::" + current_tok->get_value();
             algo_name = std::make_shared<TypedToken<std::string>>(TOKEN_IDENTIFIER, struct_name->get_pos(), full_name);
             advance();
        } else if (current_tok->get_type() == TOKEN_IDENTIFIER) {
             // Handle "Algorithm List constructor" where List is struct name and constructor is method name
             // Or generally "Algorithm StructName MethodName"
             // But wait, "constructor" is just identifier.
             // If we are inside struct definition, "Algorithm constructor" is handled by "Algorithm" keyword check in struct_def loop.
             // But inside struct_def, we call algo_def.
             // If we have `Algorithm List constructor...` inside struct?
             // struct_def consumes "Algorithm". Calls algo_def.
             // algo_def sees "List".
             // It consumes "List". Checks for SCOPE_RES. No.
             // It checks for LPAREN. No.
             // It checks for "operator". No.
             // It falls here.
             // If next token is identifier "constructor", then "List" was struct name prefix without "::".
             // Allow "Identifier Identifier" as name?
             // "List constructor" -> method name "constructor"?
             // The prompt syntax: `Algorithm List constructor(_head, _tail):`
             // So yes, `List` (struct name) `constructor` (method name).
             // And global: `Algorithm List::additional_member_function`
             // So `struct_name :: method_name` OR `struct_name method_name` (space separated)?
             // Or just `method_name` inside struct?
             // The example inside struct: `Algorithm List constructor...`
             // Also `Algorithm push_element...` (no List prefix).
             // So if prefix matches struct name, ignore it?
             // Let's allow consuming an extra identifier if it's not LPAREN.

             // Current token is "List". Next is "constructor".
             // We are at `if (current_tok->get_type() != TOKEN_LEFT_PAREN)` check.
             // `current_tok` is LPAREN? No, it is "constructor" (from example).
             // Wait, `algo_name` holds "List". `current_tok` is "constructor".
             // So we are in the `else if` block.

             // We can check if `current_tok` is identifier.
             // If so, update `algo_name` to `current_tok` (method name), and treat `algo_name` (struct name) as context?
             // But `algo_def` creates `AlgorithmDefNode` with a name.
             // If we are inside struct, the name should be just method name?
             // `List constructor` -> name is `constructor`.
             // So we update `algo_name` to `current_tok` and advance.
             algo_name = current_tok;
             advance();
        } else if (current_tok->get_type() != TOKEN_LEFT_PAREN) {
            std::string error_msg = "Expected a \"(\"";
            std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
            return std::make_shared<ErrorNode>(error_token);
        }
    } else if (current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "operator") {
        // Operator overloading
        advance();
        std::string op_name = "operator ";
        if (KEYWORDS.count(current_tok->get_value()) || TO_TOKEN_TYPE.count(current_tok->get_value()[0])) {
             op_name += current_tok->get_value();
        } else {
             op_name += current_tok->get_type(); // fallback
        }
        // Need to handle operators properly. For now, assume simple operators or identifiers.
        // Actually, for "operator add", "add" is likely TOKEN_ADD (if '+') or identifier/keyword.
        // In the example: `Algorithm operator add(other)`
        // `add` is not a keyword. `+` is TOKEN_ADD.
        // But the user example says `operator add`.
        // If `add` is not a keyword, it is an identifier.
        // If it is `+`, lexer produces TOKEN_ADD.
        // Check what `add` is tokenized as.
        // "add" is not in KEYWORDS, BUILTIN_ALGO, BUILTIN_CONST. So it is IDENTIFIER.
        op_name = "operator " + current_tok->get_value();
        algo_name = std::make_shared<TypedToken<std::string>>(TOKEN_IDENTIFIER, current_tok->get_pos(), op_name);
        advance();

    } else if(current_tok->get_type() != TOKEN_LEFT_PAREN) {
        algo_name = std::make_shared<TypedToken<std::string>>(TOKEN_IDENTIFIER, current_tok->get_pos(), "Anonymous");
    } else {
        std::string error_msg = "Expected an \"identifier\" or \"(\"";
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
                std::string error_msg = "Expected an \"identifier\" or a \"(\"";
                std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
                return std::make_shared<ErrorNode>(error_token);
            }
            args_name.push_back(current_tok);
            advance();
        }
    }

    if(current_tok->get_type() != TOKEN_RIGHT_PAREN) {
        std::string error_msg = "Expected a \")\"";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();
    if(current_tok->get_type() != TOKEN_COLON) {
        std::string error_msg = "Expected a \":\"";
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
    while(current_tok->get_type() == TOKEN_DOT) {
        advance();
        if(current_tok->get_type() != TOKEN_IDENTIFIER) {
             std::string error_msg = "Expected an identifier after \".\"";
             std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
             return std::make_shared<ErrorNode>(error_token);
        }
        std::shared_ptr<Node> member = std::make_shared<VarAccessNode>(current_tok);
        advance();
        at = std::make_shared<MemberAccessNode>(at, member);
    }
    if(current_tok->get_type() != TOKEN_LEFT_PAREN && current_tok->get_type() != TOKEN_LEFT_SQUARE && current_tok->get_type() != TOKEN_ASSIGN) {
        return at;
    }

    // Check for assignment to at directly (e.g. member assignment)
    if (current_tok->get_type() == TOKEN_ASSIGN) {
        advance();
        std::shared_ptr<Node> val = expr(tab_expect);
        if(val->get_type() == NODE_ERROR) return val;
        // Reuse ArrayAssignNode for general assignment to lvalue expression
        return std::make_shared<ArrayAssignNode>(at, val);
    }

    std::shared_ptr<Node> ret = at;
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
                    std::string error_msg = "Expected a \")\"";
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
                std::string error_msg = "Expected a \"]\"";
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
            std::shared_ptr<Token> next_tok;
            advance(); // consume NEWLINE

            // Check indentation
            int tab_count = 0;
            while(current_tok->get_type() == TOKEN_TAB) {
                tab_count++;
                advance();
            }

            if (current_tok->get_type() == TOKEN_NEWLINE) {
                continue;
            }

            if (tab_count < tab_expect) {
                // Dedent, end of block
                // Push back current non-tab token? No, we need to rewind to the start of this line (after newline)
                // But wait, the parser state is now at the first non-tab token of the line.
                // We should rewind to the NEWLINE token we just consumed?
                // Actually, if indentation is less, it means this line belongs to outer block.
                // So we should return.
                // But we must push back the tokens we consumed (including NEWLINE and tabs) so outer block can consume them?
                // Or just push back the non-tab token, and let outer block handle tabs?
                // The outer block logic:
                // It called statement(tab_expect + 1).
                // If we return, it continues.
                
                // Let's look at how it was implemented.
                // It was rewinding to NEWLINE.

                // Rewind logic:
                // We advanced past NEWLINE, and past 'tab_count' tabs.
                // We need to rewind 'tab_count' + 1 times.
                for (int i=0; i < tab_count + 1; ++i) back();
                return ret;
            } else if (tab_count > tab_expect) {
                 std::string error_msg = "Expected " + std::to_string(tab_expect) + " tabs";
                 std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, tok->get_pos(), error_msg);
                 ret.clear();
                 ret.push_back(std::make_shared<ErrorNode>(error_token));
                 return ret;
            }
            // Exact indentation matches tab_expect
            // if(current_tok->get_type() == TOKEN_NEWLINE) {
            //     // Empty line with just tabs? or just multiple newlines?
            //     // Loop again.
            //     back(); // go back to NEWLINE to let loop handle it
            //     break; // break inner loop, continue outer do-while
            // }
        }

        while(current_tok->get_type() == TOKEN_SEMICOLON)
            advance();

        if(current_tok->get_type() != TOKEN_NONE && current_tok->get_type() != TOKEN_NEWLINE) {
             std::shared_ptr<Node> val = expr(tab_expect);
             if (val->get_type() == NODE_ERROR) {
                 ret.clear();
                 ret.push_back(val);
                 return ret;
             }
             ret.push_back(val);
        }

    } while(current_tok->get_type() == TOKEN_NEWLINE || current_tok->get_type() == TOKEN_SEMICOLON);
    return ret;
}

NodeList Parser::parse() {
    NodeList ret = statement(0);
    return ret;
}std::shared_ptr<Node> Parser::struct_def(int tab_expect) {
    std::shared_ptr<Token> struct_name = current_tok;
    if (current_tok->get_type() != TOKEN_IDENTIFIER) {
        std::string error_msg = "Expected an identifier";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();

    if (current_tok->get_type() != TOKEN_COLON) {
        std::string error_msg = "Expected a \":\"";
        std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
        return std::make_shared<ErrorNode>(error_token);
    }
    advance();

    TokenList members;
    NodeList methods;

    while (current_tok->get_type() == TOKEN_NEWLINE) {
        std::shared_ptr<Token> tok_newline = current_tok;
        advance();
        
        int tab_count = 0;
        while(current_tok->get_type() == TOKEN_TAB) {
            tab_count++;
            advance();
        }

        if (current_tok->get_type() == TOKEN_NEWLINE) {
            continue;
        }

        if (tab_count < tab_expect + 1) {
             // Indentation finished, end of struct definition
             while(current_tok != tok_newline) back(); // Go back to newline
             return std::make_shared<StructDefNode>(struct_name, members, methods);
        }

        if (tab_count > tab_expect + 1) {
             std::string error_msg = "Expected " + std::to_string(tab_expect + 1) + " tabs";
             std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
             return std::make_shared<ErrorNode>(error_token);
        }

        // Inside struct block
        if (current_tok->get_type() == TOKEN_IDENTIFIER) {
            // Member variable or generic algorithm?
            // Actually, Algorithm keyword starts an algorithm definition.
            // Identifiers are member variables.
            // But wait, what if it's "Algorithm"?
            // If it is an identifier, it is a member.
            members.push_back(current_tok);
            advance();
        } else if (current_tok->get_type() == TOKEN_KEYWORD && current_tok->get_value() == "Algorithm") {
             advance();
             methods.push_back(algo_def(tab_expect + 1));
             if (methods.back()->get_type() == NODE_ERROR) return methods.back();
        } else {
             // Unexpected token or end of block?
             // If it's a newline, loop continues.
             // If it's something else, might be error.
             if (current_tok->get_type() != TOKEN_NEWLINE && current_tok->get_type() != TOKEN_NONE) {
                 std::string error_msg = "Expected identifier or Algorithm inside struct";
                 std::shared_ptr<Token> error_token = std::make_shared<ErrorToken>(TOKEN_ERROR, current_tok->get_pos(), error_msg);
                 return std::make_shared<ErrorNode>(error_token);
             }
        }
    }
    return std::make_shared<StructDefNode>(struct_name, members, methods);
}
