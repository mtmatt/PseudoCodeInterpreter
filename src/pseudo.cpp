#include "value.h"
#include "pseudo.h"
#include "node.h"
#include "color.h"
#include "interpreter.h"
#include <iostream>
#include <sstream>
#include <string>
#include <cmath>

/// --------------------
/// Value
/// --------------------

std::ostream& operator<<(std::ostream &out, Value &number) {
    out << number.get_num();
    return out;
}

template<typename T>
std::string TypedValue<T>::get_num() {
    std::stringstream ss;
    std::string ret;
    if(type == VALUE_FLOAT || type == VALUE_INT) {
        ss << value;
        std::getline(ss, ret);
    } else if(type == VALUE_STRING || type == VALUE_ERROR) {
        ret = value;
    }
    return ret;
}

template<typename T>
std::string TypedValue<T>::repr() {
    std::stringstream ss;
    std::string ret;
    if(type == VALUE_FLOAT || type == VALUE_INT) {
        ret = get_num();
    } else if(type == VALUE_STRING) {
        ss << value;

        char ch{char(ss.get())};
        ret += '\"';
        while(!ss.eof()) {
            if(REVERSE_ESCAPE_CHAR.count(ch)) {
                ret += '\\';
                ret += REVERSE_ESCAPE_CHAR.at(ch);
            } else {
                ret += ch;
            }
            ch = ss.get();
        }
        ret += '\"';
    }
    return ret;
}

std::string ArrayValue::get_num() {
    std::stringstream ss;
    ss << "{";
    if(!value.empty()) ss << value[0]->repr();
    for(int i{1}; i < value.size(); ++i) {
        ss << ", " << value[i]->repr();
    }
    ss << "}";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

void ArrayValue::push_back(std::shared_ptr<Value> new_value) {
    value.push_back(new_value);
}

std::shared_ptr<Value> ArrayValue::pop_back() {
    if(value.empty())
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Pop a empty array");
    std::shared_ptr<Value> ret = value.back();
    value.pop_back();
    return ret;
}

std::shared_ptr<Value>& ArrayValue::operator[](int p) {
    if(1 <= p && p <= value.size())
        return value[p - 1];
    error = std::make_shared<ErrorValue>(
        VALUE_ERROR, "Index out of range, size: " + std::to_string(value.size()) + ", position: " + std::to_string(p));
    return error;
}

std::shared_ptr<Value> BaseAlgoValue::set_args(NodeList &args, SymbolTable &sym, Interpreter &interpreter) {
    if(args.size() < value->get_toks().size()) {
        return std::make_shared<ErrorValue>(VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too few arguments" RESET);
    } else if(args.size() > value->get_toks().size()) {
        return std::make_shared<ErrorValue>(VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too many arguments" RESET);
    }

    TokenList args_name = value->get_toks();
    for(int i = 0; i < args.size(); ++i) {
        std::shared_ptr<Value> v = interpreter.visit(args[i]);
        sym.set(args_name[i]->get_value(), v);
    }
    return std::make_shared<Value>();
}

std::shared_ptr<Value> AlgoValue::execute(NodeList args, SymbolTable *parent) {
    SymbolTable sym(parent);
    Interpreter interpreter(sym);
    std::shared_ptr<Value> ret{set_args(args, sym, interpreter)};
    if(ret->get_type() == VALUE_ERROR)
        return ret;

    NodeList algo_body = value->get_child();
    
    for(int i = 0; i < algo_body.size(); ++i) {
        ret = interpreter.visit(algo_body[i]);
    }
    return ret;
}

std::shared_ptr<Value> BuiltinAlgoValue::execute(NodeList args, SymbolTable *parent) {
    SymbolTable sym(parent);
    Interpreter interpreter(sym);
    std::shared_ptr<Value> ret{set_args(args, sym, interpreter)};
    if(ret->get_type() == VALUE_ERROR)
        return ret;
    TokenList args_name = value->get_toks();
    if(algo_name == "print") {
        return execute_print(sym.get(args_name[0]->get_value())->get_num());
    } else if(algo_name == "read") {
        return execute_read();
    } else if(algo_name == "read_line") {
        return execute_read_line();
    } else if(algo_name == "open") {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Not found!");
    } else if(algo_name == "clear") {
        return execute_clear();
    } else if(algo_name == "quit") {
        exit(0);
    } else if(algo_name == "int") {
        return execute_int(sym.get(args_name[0]->get_value())->get_num());
    } else if(algo_name == "float") {
        return execute_float(sym.get(args_name[0]->get_value())->get_num());
    } else if(algo_name == "string") {
        return execute_string(sym.get(args_name[0]->get_value())->get_num());
    }
    return ret;
}

std::shared_ptr<Value> BoundMethodValue::execute(NodeList args, SymbolTable *parent) {
    if (obj->get_type() == VALUE_ARRAY) {
        ArrayValue *arr_obj = dynamic_cast<ArrayValue*>(obj.get());
        SymbolTable sym(parent);
        Interpreter interpreter(sym);

        if (method_name == "push" || method_name == "push_back") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect one argument for " + method_name + "\n");
            }
            std::shared_ptr<Value> arg = interpreter.visit(args[0]);
            if (arg->get_type() == VALUE_ERROR) return arg;
            arr_obj->push_back(arg);
            return arr_obj->back();
        } else if (method_name == "pop" || method_name == "pop_back") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero argument for " + method_name + "\n");
            }
            if (arr_obj->size()->get_num() == "0") {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot " + method_name + " from an empty array\n");
            }
            return arr_obj->pop_back();
        } else if (method_name == "resize") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect one argument for resize\n");
            }
            std::shared_ptr<Value> new_size_val = interpreter.visit(args[0]);
            if (new_size_val->get_type() == VALUE_ERROR) return new_size_val;
            if (new_size_val->get_type() != VALUE_INT) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Argument for resize must be an integer\n");
            }
            long long new_size;
            try {
                new_size = std::stoll(new_size_val->get_num());
            } catch (const std::out_of_range& oor) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Resize argument out of range\n");
            }
            if (new_size < 0) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Resize argument cannot be negative\n");
            }
            arr_obj->resize(static_cast<int>(new_size));
            return obj;
        } else if (method_name == "size") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero argument for size\n");
            }
            return arr_obj->size();
        } else if (method_name == "back") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero arguments for back\n");
            }
            if (arr_obj->size()->get_num() == "0") {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot call back on an empty array\n");
            }
            return arr_obj->back();
        }
    }
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Unknown member or invalid object for " + method_name + "\n");
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_print(const std::string &str) {
    std::cout << str << "\n";
    return std::make_shared<Value>();
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_read() {
    std::string ret;
    std::cin >> ret;
    std::cin.ignore();
    return std::make_shared<TypedValue<std::string>>(VALUE_STRING, ret);
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_read_line() {
    std::string ret;
    std::getline(std::cin, ret);
    return std::make_shared<TypedValue<std::string>>(VALUE_STRING, ret);
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_clear() {
    std::system("clear");
    return std::make_shared<Value>();
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_int(const std::string &str) {
    if(str[0] != '-' && !std::isdigit(str[0])) {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot convert \"" + str + "\" to an int");
    }
    for(int i{1}; i < str.size(); ++i) 
        if(!std::isdigit(str[0])) 
            return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot convert \"" + str + "\" to an int");
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT, std::stoll(str));
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_float(const std::string &str) {
    int point{0};
    if(str[0] != '-' && !std::isdigit(str[0])) {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot convert \"" + str + "\" to an int");
    }
    for(int i{1}; i < str.size(); ++i) {
        if(!std::isdigit(str[0]) && (str[0] != '.' || point == 1)) {
            return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot convert \"" + str + "\" to an int");
        }
        if(str[0] == '.') point++;
    }
    return std::make_shared<TypedValue<double>>(VALUE_FLOAT, std::stod(str));
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_string(const std::string &str) {
    return std::make_shared<TypedValue<std::string>>(VALUE_STRING, str);
}

std::shared_ptr<Value> operator+(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(
            VALUE_FLOAT, std::stod(a->get_num()) + std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) + std::stoll(b->get_num()));
    else if(a->get_type() == VALUE_STRING && b->get_type() == VALUE_STRING)
        return std::make_shared<TypedValue<std::string>>(
            VALUE_STRING, a->get_num() + b->get_num());
    else
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: ADD operation can only apply on number or two string\n" RESET);
}

std::shared_ptr<Value> operator-(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(
            VALUE_FLOAT, std::stod(a->get_num()) - std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) - std::stoll(b->get_num()));
    else
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: SUB operation can only apply on number\n" RESET);
}

std::shared_ptr<Value> operator*(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(
            VALUE_FLOAT, std::stod(a->get_num()) * std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) * std::stoll(b->get_num()));
    else if(a->get_type() == VALUE_STRING && b->get_type() == VALUE_INT) {
        std::string ret, str_a{a->get_num()};
        int64_t times{stoll(b->get_num())};
        for(int i{0}; i < times; ++i)
            ret += str_a;
        return std::make_shared<TypedValue<std::string>>(
            VALUE_STRING, ret);
    } else
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: MUL operation can only apply on number or string and int\n" RESET);
}

std::shared_ptr<Value> operator/(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(std::stod(b->get_num()) == 0.0) 
        return std::make_shared<ErrorValue>(VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: DIV by 0\n" RESET);
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(
            VALUE_FLOAT, std::stod(a->get_num()) / std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) / std::stoll(b->get_num()));
    else
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: DIV operation can only apply on number\n" RESET);
}

std::shared_ptr<Value> operator%(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() != VALUE_INT || b->get_type() != VALUE_INT) 
        return std::make_shared<ErrorValue>(VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Cannot apply \"%\" operation on float\n" RESET);
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) % std::stoll(b->get_num()));
}

std::shared_ptr<Value> operator==(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) == std::stod(b->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, a->get_num() == b->get_num());
}

std::shared_ptr<Value> operator!=(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) != std::stod(b->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, a->get_num() != b->get_num());
}

std::shared_ptr<Value> operator<(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) < std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) < std::stoll(b->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, a->get_num() < b->get_num());
}

std::shared_ptr<Value> operator>(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) > std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) > std::stoll(b->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, a->get_num() > b->get_num());
}

std::shared_ptr<Value> operator<=(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) <= std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) <= std::stoll(b->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, a->get_num() <= b->get_num());
}

std::shared_ptr<Value> operator>=(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) >= std::stod(b->get_num()));
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) >= std::stoll(b->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, a->get_num() >= b->get_num());
}

std::shared_ptr<Value> operator&&(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) != 0 && std::stod(b->get_num()) != 0);
    else if(a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) != 0 && std::stoll(b->get_num()) != 0);
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) && std::stoll(b->get_num()));
}

std::shared_ptr<Value> operator||(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stod(a->get_num()) != 0 || std::stod(b->get_num()) != 0);
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::stoll(a->get_num()) || std::stoll(b->get_num()));
}

std::shared_ptr<Value> operator-(std::shared_ptr<Value> a) {
    if(a->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, 0 - stod(a->get_num()));
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, 0 - stoll(a->get_num()));
}

std::shared_ptr<Value> operator!(std::shared_ptr<Value> a) {
    if(a->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, stod(a->get_num()) == 0);
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, stoll(a->get_num()) == 0);
}

std::shared_ptr<Value> pow(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if(std::stod(a->get_num()) == 0.0 && std::stod(b->get_num()) == 0.0) 
        return std::make_shared<ErrorValue>(VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: 0 to the 0\n" RESET);
    if(a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(
            VALUE_FLOAT, std::pow(std::stod(a->get_num()), std::stod(b->get_num())));
    else
        return std::make_shared<TypedValue<int64_t>>(
            VALUE_INT, std::pow(std::stoll(a->get_num()), std::stoll(b->get_num())));
}

/// --------------------
/// Run
/// --------------------

std::string Run(std::string file_name, std::string text, SymbolTable &global_symbol_table) {
    Lexer lexer(file_name, text);
    TokenList tokens = lexer.make_tokens();
    if(tokens.empty()) return "";
    if(tokens[0]->get_type() == TOKEN_ERROR)
        std::cout << "Tokens: " << tokens << "\n";

    Parser parser(tokens);
    NodeList ast = parser.parse();
    
    for(auto node : ast) {
        // if(node->get_type() == NODE_ERROR)
            std::cout << "Nodes: " << node->get_node() << "\n";
        if(node->get_type() == NODE_ERROR) return "ABORT";
    }

    Interpreter interpreter(global_symbol_table);
    ArrayValue *ret{new ArrayValue(ValueList(0))};
    for(auto node : ast) {
        ret->push_back(interpreter.visit(node));
        if(ret->back()->get_type() == VALUE_ERROR) {
            std::cout << ret->back()->get_num() << "\n";
            return "ABORT";
        }
    }

    while(ret->get_type() == VALUE_ARRAY && ret->back()->get_type() == VALUE_ARRAY) {
        ret = dynamic_cast<ArrayValue*>(ret->back().get());
    }
    
    if(file_name == "stdin" && ret->operator[](0)->get_type() != VALUE_NONE) {
        std::cout << ret->get_num() << "\n";
    }
    return "";
}
