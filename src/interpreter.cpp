/// --------------------
/// Interpreter
/// --------------------

#include "interpreter.h"
#include "node.h"
#include "value.h"
#include <iostream>
#include <memory>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace {
constexpr int JIT_HOT_THRESHOLD = 8;

bool is_jit_root(const std::shared_ptr<Node>& node) {
    if (!node) return false;
    return node->get_type() == NODE_BINOP || node->get_type() == NODE_UNARYOP;
}
} // namespace

std::shared_ptr<Value> Interpreter::visit(std::shared_ptr<Node> node) {
    if (std::optional<std::shared_ptr<Value>> jit_result = try_visit_jit(node)) {
        return *jit_result;
    }

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
    if(node->get_type() == NODE_STRUCTDEF) {
        return visit_struct_def(node);
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
    if(node->get_type() == NODE_RETURN) {
        return visit_return(node);
    }
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Fail to get result\n");
}

std::optional<std::shared_ptr<Value>> Interpreter::try_visit_jit(const std::shared_ptr<Node>& node) {
    static std::unordered_map<std::size_t, JitCacheEntry> jit_cache;

    if (!is_jit_root(node)) {
        return std::nullopt;
    }

    JitCacheEntry& entry = jit_cache[node->get_id()];
    if (entry.disabled) {
        return std::nullopt;
    }

    if (entry.program) {
        return entry.program->execute(symbol_table);
    }

    ++entry.hits;
    if (entry.hits < JIT_HOT_THRESHOLD) {
        return std::nullopt;
    }

    entry.program = ExpressionJit::compile(node);
    if (!entry.program) {
        entry.disabled = true;
        return std::nullopt;
    }

    return entry.program->execute(symbol_table);
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
    return dynamic_cast<ArrayValue*>(arr.get())->operator[](index->as_int());
}

std::shared_ptr<Value> Interpreter::visit_array_assign(std::shared_ptr<Node> node) {
    NodeList child{node->get_child()};
    if(child[0]->get_type() == NODE_MEMACCESS) {
        // Handle member assignment: obj.member <- val
        // visit_member_access currently returns a value (possibly a BoundMethodValue or just a value).
        // But for assignment we need to set the member.
        // We need to unpack NODE_MEMACCESS to get obj and member name.
        std::shared_ptr<Node> obj_node = child[0]->get_child()[0];
        std::shared_ptr<Node> member_node = child[0]->get_child()[1];

        std::shared_ptr<Value> obj = visit(obj_node);
        if (obj->get_type() == VALUE_INSTANCE) {
            InstanceValue* inst = dynamic_cast<InstanceValue*>(obj.get());
            std::shared_ptr<Value> val = visit(child[1]);
            if (val->get_type() == VALUE_ERROR) return val;
            inst->set_member(member_node->get_name(), val);
            return val;
        } else {
             return std::make_shared<ErrorValue>(VALUE_ERROR, "Assignment to member only supported for Struct Instances\n");
        }
    } else if(child[0]->get_type() != NODE_ARRACCESS) {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Access can only apply on array or object member\n");
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
    } else if (obj->get_type() == VALUE_INSTANCE) {
        std::string member_name = member->get_name();
        InstanceValue* inst = dynamic_cast<InstanceValue*>(obj.get());
        return inst->get_member(member_name, obj);
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
            if(ret->get_type() == VALUE_ERROR || ret->get_type() == VALUE_RETURN) {
                return ret;
            }
        }
        return ret;
    } else if(!if_node->get_else().empty()) {
        std::shared_ptr<Value> ret;
        for(auto expr : if_node->get_else()) {
            ret = visit(expr);
            if(ret->get_type() == VALUE_ERROR || ret->get_type() == VALUE_RETURN) {
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
    if(step->as_double() > 0) {
        condition = [](std::shared_ptr<Value> i, std::shared_ptr<Value> end) -> bool {
            if (i->get_type() == VALUE_FLOAT || end->get_type() == VALUE_FLOAT)
                return i->as_double() <= end->as_double();
            return i->as_int() <= end->as_int();
        };
    } else if(step->as_double() < 0) {
        condition = [](std::shared_ptr<Value> i, std::shared_ptr<Value> end) -> bool {
            if (i->get_type() == VALUE_FLOAT || end->get_type() == VALUE_FLOAT)
                return i->as_double() >= end->as_double();
            return i->as_int() >= end->as_int();
        };
    } else {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Infinite for loop\n");
    }

    ValueList ret;
    while(condition(i, end_value)) {
        if(child.size() == 4) {
            std::shared_ptr<Value> val = visit(child[3]);
            if(val->get_type() == VALUE_ERROR || val->get_type() == VALUE_RETURN) 
                return val;
            ret.push_back(val);
        } else {
            for(int index{3}; index < child.size(); ++index) {
                std::shared_ptr<Value> val{visit(child[index])};
                if(val->get_type() == VALUE_ERROR || val->get_type() == VALUE_RETURN) 
                    return val;
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
    while(visit(child[0])->as_int() == 1) {
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
    } while(visit(child[0])->as_int() == 0);
    return std::make_shared<ArrayValue>(ret);
}

std::shared_ptr<Value> Interpreter::visit_algo_def(std::shared_ptr<Node> node) {
    std::string algo_name = node->get_name();
    std::shared_ptr<Value> value = std::make_shared<AlgoValue>(algo_name, node);
    
    size_t pos = algo_name.find("::");
    if (pos != std::string::npos) {
        std::string struct_name = algo_name.substr(0, pos);
        std::string method_name = algo_name.substr(pos + 2);
        
        std::shared_ptr<Value> struct_val = symbol_table.get(struct_name);
        if (struct_val->get_type() == VALUE_STRUCT) {
            StructValue* s = dynamic_cast<StructValue*>(struct_val.get());
            // We create a new AlgoValue with the short name for the method map
            // so that when it executes, it might have the correct name context if needed.
            // But actually, the AlgoValue holds the AST node.
            // The AST node has the full name.
            // It should be fine.
            s->methods[method_name] = value;
        }
    }

    symbol_table.set(algo_name, value);
    return symbol_table.get(algo_name);
}

std::shared_ptr<Value> Interpreter::visit_algo_call(std::shared_ptr<Node> node) {
    if (std::optional<std::shared_ptr<Value>> array_result = try_visit_array_method_call(node)) {
        return *array_result;
    }

    AlgorithmCallNode *algo_call_node = dynamic_cast<AlgorithmCallNode*>(node.get());
    std::shared_ptr<Node> algo_node = algo_call_node->get_call();
    std::shared_ptr<Value> algo;
    if(algo_node->get_type() == NODE_VARACCESS)
        algo = symbol_table.get(algo_call_node->get_name());
    else
        algo = visit(algo_node);
    return algo->execute(algo_call_node->get_args(), &symbol_table);
}

std::optional<std::shared_ptr<Value>> Interpreter::try_visit_array_method_call(const std::shared_ptr<Node>& node) {
    AlgorithmCallNode *algo_call_node = dynamic_cast<AlgorithmCallNode*>(node.get());
    if (!algo_call_node) {
        return std::nullopt;
    }

    std::shared_ptr<Node> call_node = algo_call_node->get_call();
    if (call_node->get_type() != NODE_MEMACCESS) {
        return std::nullopt;
    }

    NodeList member_child = call_node->get_child();
    std::shared_ptr<Value> obj = visit(member_child[0]);
    if (obj->get_type() != VALUE_ARRAY) {
        return std::nullopt;
    }

    ArrayValue *arr_obj = dynamic_cast<ArrayValue *>(obj.get());
    const std::string method_name = member_child[1]->get_name();
    const NodeList& args = algo_call_node->get_args();

    if (method_name == "push" || method_name == "push_back") {
        if (args.size() != 1) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, "Expect one argument for " + method_name + "\n");
        }
        std::shared_ptr<Value> arg = visit(args[0]);
        if (arg->get_type() == VALUE_ERROR) {
            return arg;
        }
        arr_obj->push_back(arg);
        return arr_obj->back();
    }

    if (method_name == "pop" || method_name == "pop_back") {
        if (!args.empty()) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, "Expect zero argument for " + method_name + "\n");
        }
        if (arr_obj->empty()) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, "Cannot " + method_name + " from an empty array\n");
        }
        return arr_obj->pop_back();
    }

    if (method_name == "resize") {
        if (args.size() != 1) {
            return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                "Expect one argument for resize\n");
        }
        std::shared_ptr<Value> new_size_val = visit(args[0]);
        if (new_size_val->get_type() == VALUE_ERROR) {
            return new_size_val;
        }
        if (new_size_val->get_type() != VALUE_INT) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, "Argument for resize must be an integer\n");
        }
        long long new_size;
        try {
            new_size = new_size_val->as_int();
        } catch (const std::out_of_range&) {
            return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                "Resize argument out of range\n");
        }
        if (new_size < 0) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, "Resize argument cannot be negative\n");
        }
        arr_obj->resize(static_cast<int>(new_size));
        return obj;
    }

    if (method_name == "size") {
        if (!args.empty()) {
            return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                "Expect zero argument for size\n");
        }
        return arr_obj->size();
    }

    if (method_name == "back") {
        if (!args.empty()) {
            return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                "Expect zero arguments for back\n");
        }
        if (arr_obj->empty()) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, "Cannot call back on an empty array\n");
        }
        return arr_obj->back();
    }

    return std::nullopt;
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
}std::shared_ptr<Value> Interpreter::visit_struct_def(std::shared_ptr<Node> node) {
    auto struct_node = dynamic_cast<StructDefNode*>(node.get());
    std::string name = struct_node->get_name();
    std::vector<std::string> members;
    for (const auto& tok : struct_node->get_toks()) {
        members.push_back(tok->get_value());
    }

    std::map<std::string, std::shared_ptr<Value>> methods;
    for (const auto& method_node : struct_node->get_child()) {
        std::string method_name = method_node->get_name(); // This is the simple name "constructor", "add", etc.
        // We need to visit it to create an AlgoValue
        // But visit_algo_def puts it in symbol table. We don't want that for struct methods,
        // we want them inside the struct.
        // However, visit_algo_def creates an AlgoValue and returns it.
        // But it also sets it in symbol_table.
        // We can temporarily use a dummy symbol table or just remove it after.
        // Or better, manually create AlgoValue.
        std::shared_ptr<Value> method_val = std::make_shared<AlgoValue>(method_name, method_node);
        methods[method_name] = method_val;
    }

    // Now we also need to handle "Algorithm Struct::Method".
    // This is handled by visit_algo_def but with a name "Struct::Method".
    // When we interpret "Struct Definition", we create the struct.
    // If later we see "Algorithm Struct::Method", we need to attach it to the struct.
    // But StructValue is in symbol table.
    // So visit_algo_def needs to check if name contains "::".

    std::shared_ptr<Value> struct_val = std::make_shared<StructValue>(name, members, methods);
    symbol_table.set(name, struct_val);

    // Also create a constructor wrapper in global scope if "constructor" exists?
    // The user usage: `Algorithm List constructor(_head, _tail)` inside struct.
    // Call: `l <- List(1, 2)` ? Or `l <- new List(1, 2)`?
    // The example says: `Algorithm List constructor...` inside struct.
    // Usually construction is `List(...)`.
    // So `List` in symbol table should be callable.
    // StructValue needs to be callable (implement execute).
    return struct_val;
}

std::shared_ptr<Value> Interpreter::visit_return(std::shared_ptr<Node> node) {
    ReturnNode* ret_node = dynamic_cast<ReturnNode*>(node.get());
    std::shared_ptr<Value> val = visit(ret_node->get_child()[0]);
    if (val->get_type() == VALUE_ERROR) return val;
    return std::make_shared<ReturnValue>(val);
}
