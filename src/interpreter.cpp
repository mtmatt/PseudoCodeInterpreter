/// --------------------
/// Interpreter
/// --------------------

#include "interpreter.h"
#include "node.h"
#include "value.h"
#include <iostream>
#include <memory>
#include <functional>

std::shared_ptr<Value> Interpreter::visit(std::shared_ptr<Node> node) {
    if(node->get_type() == NODE_VALUE) {
        return visit_number(node);
    }
    if(node->get_type() == NODE_VARACCESS) {
        return visit_var_access(node);
    }
    if(node->get_type() == NODE_VARASSIGN) {
        return visit_var_assign(node);
    }
    if(node->get_type() == NODE_BINOP) {
        return visit_bin_op(node);
    }
    if(node->get_type() == NODE_UNARYOP) {
        return visit_unary_op(node);
    }
    if(node->get_type() == NODE_IF) {
        return visit_if(node);
    }
    if(node->get_type() == NODE_FOR) {
        return visit_for(node);
    }
    if(node->get_type() == NODE_WHILE) {
        return visit_while(node);
    }
    if(node->get_type() == NODE_REPEAT) {
        return visit_repeat(node);
    }
    if(node->get_type() == NODE_ALGODEF) {
        return visit_algo_def(node);
    }
    if(node->get_type() == NODE_ALGOCALL) {
        return visit_algo_call(node);
    }
    if(node->get_type() == NODE_ARRAY) {
        return visit_array(node);
    }
    if(node->get_type() == NODE_ARRACCESS) {
        return visit_array_access(node);
    }
    if(node->get_type() == NODE_ARRASSIGN) {
        return visit_array_assign(node);
    }
    if(node->get_type() == NODE_MEMACCESS) {
        return visit_member_access(node);
    }
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Fail to get result\n");
}

std::shared_ptr<Value> Interpreter::visit_number(std::shared_ptr<Node> node) {
    if(node->get_tok()->get_type() == TOKEN_INT) 
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, std::stoll(node->get_tok()->get_value()));
    else if(node->get_tok()->get_type() == TOKEN_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, std::stod(node->get_tok()->get_value()));
    else if(node->get_tok()->get_type() == TOKEN_STRING)
        return std::make_shared<TypedValue<std::string>>(VALUE_STRING, node->get_tok()->get_value());
    else
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Not a value type\n");
}

std::shared_ptr<Value> Interpreter::visit_var_access(std::shared_ptr<Node> node) {
    std::string var_name = node->get_name();
    return symbol_table.get(var_name);
}

std::shared_ptr<Value> Interpreter::visit_var_assign(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    std::string var_name = node->get_name();
    std::shared_ptr<Value> value = visit(child[0]);
    if(value->get_type() == VALUE_ERROR)
        return value;
    symbol_table.set(var_name, value);
    return symbol_table.get(var_name);
}

std::shared_ptr<Value> Interpreter::visit_bin_op(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    std::shared_ptr<Value> a, b;
    a = visit(child[0]);
    b = visit(child[1]);
    if(a->get_type() == VALUE_ERROR || b->get_type() == VALUE_ERROR)
        return a->get_type() == VALUE_ERROR ? a : b;
    return bin_op(a, b, node->get_tok());
}

std::shared_ptr<Value> Interpreter::visit_unary_op(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    std::shared_ptr<Value> a = visit(child[0]);
    if(a->get_type() == VALUE_ERROR)
        return a;
    return unary_op(a, node->get_tok());
}

std::shared_ptr<Value> Interpreter::visit_array(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    ValueList array_value;
    for(int i{0}; i < child.size(); ++i) {
        array_value.push_back(visit(child[i]));
    }
    return std::make_shared<ArrayValue>(array_value);
}

std::shared_ptr<Value>& Interpreter::visit_array_access(std::shared_ptr<Node> node) {
    NodeList child{node->get_child()};
    std::shared_ptr<Value> arr{visit(child[0])}, index{visit(child[1])};
    if(arr->get_type() != VALUE_ARRAY) {
        error = std::make_shared<ErrorValue>(VALUE_ERROR, "Access can only apply on array, find " + 
            arr->get_type() + "\n");
        return error;
    }
    algo_call_temp = arr;
    return dynamic_cast<ArrayValue*>(arr.get())->operator[](std::stoll(index->get_num()));
}

std::shared_ptr<Value> Interpreter::visit_array_assign(std::shared_ptr<Node> node) {
    NodeList child{node->get_child()};
    if(child[0]->get_type() != NODE_ARRACCESS) {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Access can only apply on array\n");
    }
    std::shared_ptr<Value> &arr{visit_array_access(child[0])}, value{visit(child[1])};
    return arr = value;
}

std::shared_ptr<Value> Interpreter::visit_member_access(std::shared_ptr<Node> node) {
    NodeList child{node->get_child()};
    std::shared_ptr<Value> obj{visit(child[0])};
    std::shared_ptr<Node> &member{child[1]};

    if(obj->get_type() == VALUE_ARRAY) {
        std::string member_name = member->get_name();
        return std::make_shared<BoundMethodValue>(obj, member_name);
    }
    error = std::make_shared<ErrorValue>(VALUE_ERROR, obj->get_num() + " has no member " + member->get_name() + "\n");
    return error;
} 

std::shared_ptr<Value> Interpreter::visit_if(std::shared_ptr<Node> node) {
    IfNode* if_node = dynamic_cast<IfNode*>(node.get());
    std::shared_ptr<Value> cond = visit(if_node->get_condition());
    if(cond->get_type() == VALUE_ERROR)
        return cond;
    if(std::stoll(cond->get_num()) == 1) {
        std::shared_ptr<Value> ret;
        for(auto expr : if_node->get_expr()) {
            ret = visit(expr);
            if(ret->get_type() == VALUE_ERROR) {
                return ret;
            }
        }
        return ret;
    } else if(!if_node->get_else().empty()) {
        std::shared_ptr<Value> ret;
        for(auto expr : if_node->get_else()) {
            ret = visit(expr);
            if(ret->get_type() == VALUE_ERROR) {
                return ret;
            }
        }
        return ret;
    }
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT, 0);
}

std::shared_ptr<Value> Interpreter::visit_for(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    std::shared_ptr<Value> i = visit(child[0]);
    if (i->get_type() == VALUE_ERROR) return i;
    std::shared_ptr<Value> step;
    if(child[2] != nullptr) {
        step = visit(child[2]);
        if (step->get_type() == VALUE_ERROR) return step;
    } else {
        step = std::make_shared<TypedValue<int64_t>>(VALUE_INT, 1);
    }
    std::shared_ptr<Value> end_value = visit(child[1]);
    if(end_value->get_type() == VALUE_ERROR) return end_value;
    std::function<bool(std::shared_ptr<Value>, std::shared_ptr<Value>)> condition;
    if(stod(step->get_num()) > 0) {
        condition = [](std::shared_ptr<Value> i, std::shared_ptr<Value> end) -> bool {
            return std::stoll((i <= end)->get_num()) == 1;
        };
    } else if(std::stod(step->get_num()) < 0) {
        condition = [](std::shared_ptr<Value> i, std::shared_ptr<Value> end) -> bool {
            return std::stoll((i >= end)->get_num()) == 1;
        };
    } else {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Infinite for loop\n");
    }

    ValueList ret;
    while(condition(i, end_value)) {
        if(child.size() == 4) {
            ret.push_back(visit(child[3]));
            if(ret.back()->get_type() == VALUE_ERROR) 
                return ret.back();
        } else {
            for(int index{3}; index < child.size(); ++index) {
                std::shared_ptr<Value> ret{visit(child[index])};
                if(ret->get_type() == VALUE_ERROR) 
                    return ret;
            }
        }
        symbol_table.set(child[0]->get_name(), i + step);
        i = symbol_table.get(child[0]->get_name());
    }
    if(child.size() != 4)
        ret.push_back(std::make_shared<Value>());
    return std::make_shared<ArrayValue>(ret);
}

std::shared_ptr<Value> Interpreter::visit_while(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    ValueList ret;
    while(std::stoll(visit(child[0])->get_num()) == 1) {
        if(child.size() == 2) {
            ret.push_back(visit(child[1]));
            if(ret.back()->get_type() == VALUE_ERROR) 
                return ret.back();
        } else {
            for(int index{1}; index < child.size(); ++index) {
                std::shared_ptr<Value> ret{visit(child[index])};
                if(ret->get_type() == VALUE_ERROR)
                    return ret;
            }
        }
    }
    return std::make_shared<ArrayValue>(ret);
}

std::shared_ptr<Value> Interpreter::visit_repeat(std::shared_ptr<Node> node) {
    NodeList child = node->get_child();
    ValueList ret;
    do {
        if(child.size() == 2) {
            ret.push_back(visit(child[1]));
            if(ret.back()->get_type() == VALUE_ERROR) 
                return ret.back();
        } else {
            for(int index{1}; index < child.size(); ++index) {
                std::shared_ptr<Value> ret{visit(child[index])};
                if(ret->get_type() == VALUE_ERROR)
                    return ret;
            }
        }
    } while(std::stoll(visit(child[0])->get_num()) == 0);
    return std::make_shared<ArrayValue>(ret);
}

std::shared_ptr<Value> Interpreter::visit_algo_def(std::shared_ptr<Node> node) {
    std::string algo_name = node->get_name();
    std::shared_ptr<Value> value = std::make_shared<AlgoValue>(algo_name, node);
    symbol_table.set(algo_name, value);
    return symbol_table.get(algo_name);
}

std::shared_ptr<Value> Interpreter::visit_algo_call(std::shared_ptr<Node> node) {
    AlgorithmCallNode *algo_call_node = dynamic_cast<AlgorithmCallNode*>(node.get());
    NodeList child = node->get_child();
    std::shared_ptr<Node> algo_node = algo_call_node->get_call();
    std::shared_ptr<Value> algo;
    if(algo_node->get_type() == NODE_VARACCESS)
        algo = symbol_table.get(algo_call_node->get_name());
    else
        algo = visit(algo_node);
    return algo->execute(child, &symbol_table);
}

std::shared_ptr<Value> Interpreter::bin_op(
    std::shared_ptr<Value> a, std::shared_ptr<Value> b, std::shared_ptr<Token> op
) {
    if(op->get_type() == TOKEN_ADD)
        return a + b;
    else if(op->get_type() == TOKEN_SUB)
        return a - b;
    else if(op->get_type() == TOKEN_MUL)
        return a * b;
    else if(op->get_type() == TOKEN_DIV)
        return a / b;
    else if(op->get_type() == TOKEN_MOD)
        return a % b;
    else if(op->get_type() == TOKEN_POW)
        return pow(a, b);
    else if(op->get_type() == TOKEN_EQUAL)
        return a == b;
    else if(op->get_type() == TOKEN_NEQ)
        return a != b;
    else if(op->get_type() == TOKEN_LESS)
        return a < b;
    else if(op->get_type() == TOKEN_GREATER)
        return a > b;
    else if(op->get_type() == TOKEN_LEQ)
        return a <= b;
    else if(op->get_type() == TOKEN_GEQ)
        return a >= b;
    else if(op->get_type() == TOKEN_KEYWORD && op->get_value() == "and")
        return a && b;
    else if(op->get_type() == TOKEN_KEYWORD && op->get_value() == "or")
        return a || b;
    
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Not a binary op\n");
}

std::shared_ptr<Value> Interpreter::unary_op(std::shared_ptr<Value> a, std::shared_ptr<Token> op) {
    if(op->get_type() == TOKEN_ADD)
        return a;
    else if(op->get_type() == TOKEN_SUB)
        return -a;
    else if(op->get_type() == TOKEN_KEYWORD && op->get_value() == "not")
        return !a;
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Not an unary op\n");
}