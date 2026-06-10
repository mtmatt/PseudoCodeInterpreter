#include "pseudo.h"

#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "analysis.h"
#include "color.h"
#include "error.h"
#include "imports.h"
#include "interpreter.h"
#include "jit.h"
#include "lexer.h"
#include "node.h"
#include "parser.h"
#include "token.h"
#include "value.h"

/// --------------------
/// Value
/// --------------------

std::ostream& operator<<(std::ostream& out, Value& number) {
    out << number.get_num();
    return out;
}

int64_t Value::as_int() { return std::stoll(get_num()); }

double Value::as_double() { return std::stod(get_num()); }

std::string Value::as_string() { return get_num(); }

template <typename T>
std::string TypedValue<T>::get_num() {
    std::stringstream ss;
    std::string ret;
    if (type == VALUE_FLOAT || type == VALUE_INT) {
        ss << value;
        std::getline(ss, ret);
    } else if (type == VALUE_STRING || type == VALUE_ERROR) {
        ret = value;
    }
    return ret;
}

template <typename T>
int64_t TypedValue<T>::as_int() {
    if constexpr (std::is_same_v<T, int64_t>) {
        return value;
    } else if constexpr (std::is_same_v<T, double>) {
        return static_cast<int64_t>(value);
    } else {
        return std::stoll(value);
    }
}

template <typename T>
double TypedValue<T>::as_double() {
    if constexpr (std::is_same_v<T, double>) {
        return value;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return static_cast<double>(value);
    } else {
        return std::stod(value);
    }
}

template <typename T>
std::string TypedValue<T>::as_string() {
    if constexpr (std::is_same_v<T, std::string>) {
        return value;
    } else {
        return get_num();
    }
}

template <typename T>
bool TypedValue<T>::append_string(const std::string& suffix) {
    if constexpr (std::is_same_v<T, std::string>) {
        value += suffix;
        return true;
    }
    return false;
}

template <typename T>
std::string TypedValue<T>::repr() {
    std::stringstream ss;
    std::string ret;
    if (type == VALUE_FLOAT || type == VALUE_INT) {
        ret = get_num();
    } else if (type == VALUE_STRING) {
        ss << value;

        char ch{char(ss.get())};
        ret += '\"';
        while (!ss.eof()) {
            if (REVERSE_ESCAPE_CHAR.count(ch)) {
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
    if (!value.empty()) ss << value[0]->repr();
    for (int i{1}; i < value.size(); ++i) {
        ss << ", " << value[i]->repr();
    }
    ss << "}";
    std::string ret;
    std::getline(ss, ret);
    return ret;
}

void ArrayValue::push_back(std::shared_ptr<Value> new_value) { value.push_back(new_value); }

std::shared_ptr<Value> ArrayValue::insert(int p, std::shared_ptr<Value> new_value) {
    if (p < 1 || p > static_cast<int>(value.size()) + 1) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Index out of range, size: " + std::to_string(value.size()) +
                             ", position: " + std::to_string(p));
    }
    value.insert(value.begin() + (p - 1), new_value);
    return new_value;
}

std::shared_ptr<Value> ArrayValue::remove(int p) {
    if (p < 1 || p > static_cast<int>(value.size())) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Index out of range, size: " + std::to_string(value.size()) +
                             ", position: " + std::to_string(p));
    }
    std::shared_ptr<Value> ret = value[p - 1];
    value.erase(value.begin() + (p - 1));
    return ret;
}

std::shared_ptr<Value> ArrayValue::pop_back() {
    if (value.empty()) return std::make_shared<ErrorValue>(VALUE_ERROR, "Pop an empty array");
    std::shared_ptr<Value> ret = value.back();
    value.pop_back();
    return ret;
}

std::shared_ptr<Value>& ArrayValue::operator[](int p) {
    if (1 <= p && p <= value.size()) return value[p - 1];
    error = std::make_shared<ErrorValue>(
        VALUE_ERROR, "Index out of range, size: " + std::to_string(value.size()) +
                         ", position: " + std::to_string(p));
    return error;
}

std::string HashTableValue::key_id(std::shared_ptr<Value> key) const {
    if (key->get_type() == VALUE_ARRAY || key->get_type() == VALUE_INSTANCE ||
        key->get_type() == VALUE_STRUCT || key->get_type() == VALUE_HASH_TABLE ||
        key->get_type() == VALUE_ALGO) {
        return key->get_type() +
               ":ptr:" + std::to_string(reinterpret_cast<std::uintptr_t>(key.get()));
    }
    return key->get_type() + ":" + key->repr();
}

std::string HashTableValue::get_num() {
    std::stringstream ss;
    ss << "{";
    if (!entries.empty()) {
        ss << entries[0].key->repr() << ": " << entries[0].value->repr();
    }
    for (size_t i = 1; i < entries.size(); ++i) {
        ss << ", " << entries[i].key->repr() << ": " << entries[i].value->repr();
    }
    ss << "}";
    return ss.str();
}

std::shared_ptr<Value> HashTableValue::get(std::shared_ptr<Value> key) {
    auto found = index.find(key_id(key));
    if (found == index.end()) {
        return std::make_shared<Value>();
    }
    return entries[found->second].value;
}

std::shared_ptr<Value> HashTableValue::set(std::shared_ptr<Value> key,
                                           std::shared_ptr<Value> value) {
    std::string id = key_id(key);
    auto found = index.find(id);
    if (found != index.end()) {
        entries[found->second].value = value;
        return value;
    }
    index[id] = entries.size();
    entries.push_back({key, value});
    return value;
}

std::shared_ptr<Value> HashTableValue::remove(std::shared_ptr<Value> key) {
    std::string id = key_id(key);
    auto found = index.find(id);
    if (found == index.end()) {
        return std::make_shared<Value>();
    }

    size_t removed = found->second;
    std::shared_ptr<Value> value = entries[removed].value;
    index.erase(found);
    if (removed != entries.size() - 1) {
        entries[removed] = entries.back();
        index[key_id(entries[removed].key)] = removed;
    }
    entries.pop_back();
    return value;
}

bool HashTableValue::contains(std::shared_ptr<Value> key) const {
    return index.count(key_id(key)) != 0;
}

std::shared_ptr<Value> HashTableValue::size() const {
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT, entries.size());
}

std::shared_ptr<Value> HashTableValue::keys() const {
    ValueList keys;
    keys.reserve(entries.size());
    for (const Entry& entry : entries) {
        keys.push_back(entry.key);
    }
    return std::make_shared<ArrayValue>(keys);
}

std::shared_ptr<Value> HashTableValue::values() const {
    ValueList values;
    values.reserve(entries.size());
    for (const Entry& entry : entries) {
        values.push_back(entry.value);
    }
    return std::make_shared<ArrayValue>(values);
}

void HashTableValue::clear() {
    entries.clear();
    index.clear();
}

std::shared_ptr<Value> BaseAlgoValue::set_args(const NodeList& args, SymbolTable& sym,
                                               Interpreter& interpreter) {
    if (args.size() < arg_names.size()) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too few arguments" RESET);
    } else if (args.size() > arg_names.size()) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too many arguments" RESET);
    }

    for (int i = 0; i < args.size(); ++i) {
        std::shared_ptr<Value> v = interpreter.visit(args[i]);
        if (v->get_type() == VALUE_ERROR) return v;
        sym.set(arg_names[i], v);
    }
    static std::shared_ptr<Value> none = std::make_shared<Value>();
    return none;
}

class ScopeCleaner {
   public:
    ScopeCleaner(SymbolTable& _sym) : sym(_sym) {}
    ~ScopeCleaner() {
        if (!sym.has_instances()) {
            return;
        }
        for (auto const& [name, val] : sym.get_symbols()) {
            if (val->get_type() == VALUE_INSTANCE) {
                if (val.use_count() == 1) {
                    InstanceValue* inst = dynamic_cast<InstanceValue*>(val.get());
                    std::shared_ptr<Value> self_ptr = val;
                    std::shared_ptr<Value> dtor = inst->get_member("destructor", self_ptr);
                    if (dtor->get_type() == VALUE_ALGO) {
                        dtor->execute({}, sym.get_parent());
                    }
                }
            }
        }
    }

   private:
    SymbolTable& sym;
};

namespace {

bool is_pure_numeric_node(const std::shared_ptr<Node>& node, const std::string& algo_name,
                          const std::unordered_set<std::string>& arg_names) {
    if (!node) return false;

    std::string type = node->get_type();
    if (type == NODE_VALUE) {
        std::string token_type = node->get_tok()->get_type();
        return token_type == TOKEN_INT || token_type == TOKEN_FLOAT;
    }
    if (type == NODE_VARACCESS) {
        return arg_names.count(node->get_name()) != 0;
    }
    if (type == NODE_BINOP) {
        NodeList child = node->get_child();
        return child.size() == 2 && is_pure_numeric_node(child[0], algo_name, arg_names) &&
               is_pure_numeric_node(child[1], algo_name, arg_names);
    }
    if (type == NODE_UNARYOP) {
        NodeList child = node->get_child();
        return child.size() == 1 && is_pure_numeric_node(child[0], algo_name, arg_names);
    }
    if (type == NODE_RETURN) {
        NodeList child = node->get_child();
        return child.size() == 1 && is_pure_numeric_node(child[0], algo_name, arg_names);
    }
    if (type == NODE_IF) {
        IfNode* if_node = dynamic_cast<IfNode*>(node.get());
        if (!is_pure_numeric_node(if_node->get_condition(), algo_name, arg_names)) return false;
        for (const auto& expr : if_node->get_expr()) {
            if (!is_pure_numeric_node(expr, algo_name, arg_names)) return false;
        }
        for (const auto& expr : if_node->get_else()) {
            if (!is_pure_numeric_node(expr, algo_name, arg_names)) return false;
        }
        return true;
    }
    if (type == NODE_ALGOCALL) {
        AlgorithmCallNode* call_node = dynamic_cast<AlgorithmCallNode*>(node.get());
        if (call_node->get_call()->get_type() != NODE_VARACCESS ||
            call_node->get_name() != algo_name) {
            return false;
        }
        for (const auto& arg : call_node->get_args()) {
            if (!is_pure_numeric_node(arg, algo_name, arg_names)) return false;
        }
        return true;
    }

    return false;
}

bool has_self_call(const std::shared_ptr<Node>& node, const std::string& algo_name) {
    if (!node) return false;

    if (node->get_type() == NODE_ALGOCALL) {
        AlgorithmCallNode* call_node = dynamic_cast<AlgorithmCallNode*>(node.get());
        if (call_node->get_call()->get_type() == NODE_VARACCESS &&
            call_node->get_name() == algo_name) {
            return true;
        }
        for (const auto& arg : call_node->get_args()) {
            if (has_self_call(arg, algo_name)) return true;
        }
        return false;
    }

    if (node->get_type() == NODE_IF) {
        IfNode* if_node = dynamic_cast<IfNode*>(node.get());
        if (has_self_call(if_node->get_condition(), algo_name)) return true;
        for (const auto& expr : if_node->get_expr()) {
            if (has_self_call(expr, algo_name)) return true;
        }
        for (const auto& expr : if_node->get_else()) {
            if (has_self_call(expr, algo_name)) return true;
        }
        return false;
    }

    for (const auto& child : node->get_child()) {
        if (has_self_call(child, algo_name)) return true;
    }
    return false;
}

}  // namespace

bool is_memoizable_numeric_algo(const std::shared_ptr<Node>& node, const std::string& algo_name,
                                const std::vector<std::string>& args) {
    AlgorithmDefNode* algo_node = dynamic_cast<AlgorithmDefNode*>(node.get());
    if (!algo_node || args.empty()) return false;

    bool recursive = false;
    std::unordered_set<std::string> arg_set(args.begin(), args.end());
    for (const auto& expr : algo_node->get_body()) {
        if (!is_pure_numeric_node(expr, algo_name, arg_set)) return false;
        recursive = recursive || has_self_call(expr, algo_name);
    }
    return recursive;
}

namespace {

std::shared_ptr<Node> single_return_numeric_expr(const std::shared_ptr<Node>& node,
                                                 const std::string& algo_name,
                                                 const std::vector<std::string>& args) {
    AlgorithmDefNode* algo_node = dynamic_cast<AlgorithmDefNode*>(node.get());
    if (!algo_node || algo_node->get_body().size() != 1) return nullptr;

    std::shared_ptr<Node> ret = algo_node->get_body()[0];
    if (ret->get_type() != NODE_RETURN || has_self_call(ret, algo_name)) return nullptr;

    NodeList child = ret->get_child();
    if (child.size() != 1) return nullptr;

    std::unordered_set<std::string> arg_set(args.begin(), args.end());
    if (!is_pure_numeric_node(child[0], algo_name, arg_set)) return nullptr;
    return child[0];
}

std::string numeric_cache_key(const ValueList& values) {
    std::string key;
    for (const auto& value : values) {
        if (value->get_type() != VALUE_INT && value->get_type() != VALUE_FLOAT) return "";
        key += value->get_type();
        key += ':';
        key += value->get_num();
        key += '|';
    }
    return key;
}

}  // namespace

std::shared_ptr<Value> AlgoValue::execute(const NodeList& args, SymbolTable* parent) {
    static std::unordered_map<std::size_t, bool> memoizable_by_node;
    static std::unordered_map<std::size_t, std::unordered_map<std::string, std::shared_ptr<Value>>>
        memoized_results;
    static std::unordered_map<std::size_t, JitProgram> single_return_jit;
    static std::unordered_set<std::size_t> single_return_jit_disabled;

    SymbolTable sym(parent);
    ScopeCleaner cleaner(sym);
    Interpreter interpreter(sym);
    std::size_t node_id = value->get_id();
    auto memoizable_found = memoizable_by_node.find(node_id);
    if (memoizable_found == memoizable_by_node.end()) {
        memoizable_found =
            memoizable_by_node
                .emplace(node_id, is_memoizable_numeric_algo(value, algo_name, arg_names))
                .first;
    }

    if (memoizable_found->second) {
        if (args.size() < arg_names.size()) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too few arguments" RESET);
        } else if (args.size() > arg_names.size()) {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too many arguments" RESET);
        }

        ValueList evaluated_args;
        evaluated_args.reserve(args.size());
        for (int i = 0; i < args.size(); ++i) {
            std::shared_ptr<Value> arg = interpreter.visit(args[i]);
            if (arg->get_type() == VALUE_ERROR) return arg;
            evaluated_args.push_back(arg);
        }

        std::string cache_key = numeric_cache_key(evaluated_args);
        if (!cache_key.empty()) {
            auto& cache = memoized_results[node_id];
            auto cached = cache.find(cache_key);
            if (cached != cache.end()) {
                return cached->second;
            }

            for (int i = 0; i < evaluated_args.size(); ++i) {
                sym.set(arg_names[i], evaluated_args[i]);
            }

            AlgorithmDefNode* algo_node = dynamic_cast<AlgorithmDefNode*>(value.get());
            const NodeList& algo_body = algo_node->get_body();
            std::shared_ptr<Value> ret = std::make_shared<Value>();
            for (int i = 0; i < algo_body.size(); ++i) {
                ret = interpreter.visit(algo_body[i]);
                if (ret->get_type() == VALUE_RETURN) {
                    ret = dynamic_cast<ReturnValue*>(ret.get())->get_value();
                    break;
                }
                if (ret->get_type() == VALUE_ERROR) {
                    return ret;
                }
            }
            if (ret->get_type() == VALUE_INT || ret->get_type() == VALUE_FLOAT) {
                cache[cache_key] = ret;
            }
            return ret;
        }
    }

    std::shared_ptr<Value> ret{set_args(args, sym, interpreter)};
    if (ret->get_type() == VALUE_ERROR) return ret;

    if (!single_return_jit_disabled.count(node_id)) {
        auto compiled = single_return_jit.find(node_id);
        if (compiled == single_return_jit.end()) {
            std::shared_ptr<Node> return_expr =
                single_return_numeric_expr(value, algo_name, arg_names);
            if (return_expr) {
                std::optional<JitProgram> program = ExpressionJit::compile(return_expr);
                if (program) {
                    compiled = single_return_jit.emplace(node_id, std::move(*program)).first;
                } else {
                    single_return_jit_disabled.insert(node_id);
                }
            } else {
                single_return_jit_disabled.insert(node_id);
            }
        }
        if (compiled != single_return_jit.end()) {
            std::optional<std::shared_ptr<Value>> jit_result = compiled->second.execute(sym);
            if (jit_result) {
                return *jit_result;
            }
        }
    }

    AlgorithmDefNode* algo_node = dynamic_cast<AlgorithmDefNode*>(value.get());
    const NodeList& algo_body = algo_node->get_body();

    for (int i = 0; i < algo_body.size(); ++i) {
        ret = interpreter.visit(algo_body[i]);
        if (ret->get_type() == VALUE_RETURN) {
            return dynamic_cast<ReturnValue*>(ret.get())->get_value();
        }
    }
    return ret;
}

std::shared_ptr<Value> BuiltinAlgoValue::execute(const NodeList& args, SymbolTable* parent) {
    SymbolTable sym(parent);
    ScopeCleaner cleaner(sym);
    Interpreter interpreter(sym);
    if (algo_name == "print") {
        std::string output;
        for (int i = 0; i < args.size(); ++i) {
            std::shared_ptr<Value> arg = interpreter.visit(args[i]);
            if (arg->get_type() == VALUE_ERROR) return arg;
            if (i > 0) output += " ";
            output += arg->get_num();
        }
        return execute_print(output);
    }

    std::shared_ptr<Value> ret{set_args(args, sym, interpreter)};
    if (ret->get_type() == VALUE_ERROR) return ret;
    if (algo_name == "read") {
        return execute_read();
    } else if (algo_name == "read_line") {
        return execute_read_line();
    } else if (algo_name == "open") {
        return std::make_shared<ErrorValue>(VALUE_ERROR, "Not found!");
    } else if (algo_name == "clear") {
        return execute_clear();
    } else if (algo_name == "quit") {
        exit(0);
    } else if (algo_name == "int") {
        return execute_int(sym.get(arg_names[0])->get_num());
    } else if (algo_name == "float") {
        return execute_float(sym.get(arg_names[0])->get_num());
    } else if (algo_name == "string") {
        return execute_string(sym.get(arg_names[0])->get_num());
    } else if (algo_name == "HashTable") {
        return std::make_shared<HashTableValue>();
    }
    return ret;
}

std::shared_ptr<Value> BoundMethodValue::execute(const NodeList& args, SymbolTable* parent) {
    if (obj->get_type() == VALUE_ARRAY) {
        ArrayValue* arr_obj = dynamic_cast<ArrayValue*>(obj.get());
        SymbolTable sym(parent);
        Interpreter interpreter(sym);

        if (method_name == "push" || method_name == "push_back") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(
                    VALUE_ERROR, "Expect one argument for " + method_name + "\n");
            }
            std::shared_ptr<Value> arg = interpreter.visit(args[0]);
            if (arg->get_type() == VALUE_ERROR) return arg;
            arr_obj->push_back(arg);
            return arr_obj->back();
        } else if (method_name == "pop" || method_name == "pop_back") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(
                    VALUE_ERROR, "Expect zero argument for " + method_name + "\n");
            }
            if (arr_obj->size()->get_num() == "0") {
                std::cout << "Cannot " << method_name << " from an empty array\n";
                return std::make_shared<Value>();
            }
            return arr_obj->pop_back();
        } else if (method_name == "resize") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect one argument for resize\n");
            }
            std::shared_ptr<Value> new_size_val = interpreter.visit(args[0]);
            if (new_size_val->get_type() == VALUE_ERROR) return new_size_val;
            if (new_size_val->get_type() != VALUE_INT) {
                std::cout << "Argument for resize must be an integer\n";
                return std::make_shared<Value>();
            }
            long long new_size;
            try {
                new_size = new_size_val->as_int();
            } catch (const std::out_of_range& oor) {
                std::cout << "Resize argument out of range\n";
                return std::make_shared<Value>();
            }
            if (new_size < 0) {
                std::cout << "Resize argument cannot be negative\n";
                return std::make_shared<Value>();
            }
            arr_obj->resize(static_cast<int>(new_size));
            return obj;
        } else if (method_name == "insert") {
            if (args.size() != 2) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect two arguments for insert\n");
            }
            std::shared_ptr<Value> index = interpreter.visit(args[0]);
            if (index->get_type() == VALUE_ERROR) return index;
            std::shared_ptr<Value> value = interpreter.visit(args[1]);
            if (value->get_type() == VALUE_ERROR) return value;
            return arr_obj->insert(index->as_int(), value);
        } else if (method_name == "remove") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect one argument for remove\n");
            }
            std::shared_ptr<Value> index = interpreter.visit(args[0]);
            if (index->get_type() == VALUE_ERROR) return index;
            return arr_obj->remove(index->as_int());
        } else if (method_name == "size") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero argument for size\n");
            }
            return arr_obj->size();
        } else if (method_name == "back") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect zero arguments for back\n");
            }
            if (arr_obj->size()->get_num() == "0") {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Cannot call back on an empty array\n");
            }
            return arr_obj->back();
        }
    } else if (obj->get_type() == VALUE_STRING) {
        if (method_name == "size") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero argument for size\n");
            }
            return std::make_shared<TypedValue<int64_t>>(
                VALUE_INT, static_cast<int64_t>(obj->as_string().size()));
        }
    } else if (obj->get_type() == VALUE_HASH_TABLE) {
        HashTableValue* table_obj = dynamic_cast<HashTableValue*>(obj.get());
        SymbolTable sym(parent);
        Interpreter interpreter(sym);

        if (method_name == "set") {
            if (args.size() != 2) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect two arguments for set\n");
            }
            std::shared_ptr<Value> key = interpreter.visit(args[0]);
            if (key->get_type() == VALUE_ERROR) return key;
            std::shared_ptr<Value> value = interpreter.visit(args[1]);
            if (value->get_type() == VALUE_ERROR) return value;
            return table_obj->set(key, value);
        } else if (method_name == "get") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect one argument for get\n");
            }
            std::shared_ptr<Value> key = interpreter.visit(args[0]);
            if (key->get_type() == VALUE_ERROR) return key;
            return table_obj->get(key);
        } else if (method_name == "contains") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect one argument for contains\n");
            }
            std::shared_ptr<Value> key = interpreter.visit(args[0]);
            if (key->get_type() == VALUE_ERROR) return key;
            return std::make_shared<TypedValue<int64_t>>(VALUE_INT, table_obj->contains(key));
        } else if (method_name == "remove") {
            if (args.size() != 1) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect one argument for remove\n");
            }
            std::shared_ptr<Value> key = interpreter.visit(args[0]);
            if (key->get_type() == VALUE_ERROR) return key;
            return table_obj->remove(key);
        } else if (method_name == "size") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero argument for size\n");
            }
            return table_obj->size();
        } else if (method_name == "is_empty") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect zero argument for is_empty\n");
            }
            return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                         table_obj->size()->as_int() == 0);
        } else if (method_name == "keys") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR, "Expect zero argument for keys\n");
            }
            return table_obj->keys();
        } else if (method_name == "values") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect zero argument for values\n");
            }
            return table_obj->values();
        } else if (method_name == "clear") {
            if (!args.empty()) {
                return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                    "Expect zero argument for clear\n");
            }
            table_obj->clear();
            return obj;
        }
    } else if (obj->get_type() == VALUE_INSTANCE) {
        InstanceValue* inst_obj = dynamic_cast<InstanceValue*>(obj.get());
        // Find method
        if (inst_obj->struct_def->methods.count(method_name)) {
            std::shared_ptr<Value> method = inst_obj->struct_def->methods[method_name];
            AlgoValue* algo_val = dynamic_cast<AlgoValue*>(method.get());
            if (!algo_val) {
                if (method->get_type() != VALUE_ALGO) {
                    return std::make_shared<ErrorValue>(VALUE_ERROR, "Method is not an algorithm");
                }
                // Compiled struct methods use their own execute path, with
                // `self` provided by this parent binding scope.
                SymbolTable sym(parent);
                sym.set("self", obj);
                return method->execute(args, &sym);
            }

            SymbolTable sym(parent);
            ScopeCleaner cleaner(sym);
            Interpreter interpreter(sym);

            // Set self
            sym.set("self", obj);

            std::shared_ptr<Value> ret{algo_val->set_args(args, sym, interpreter)};
            if (ret->get_type() == VALUE_ERROR) return ret;

            // Execute body
            AlgorithmDefNode* algo_node =
                dynamic_cast<AlgorithmDefNode*>(algo_val->get_node_ptr().get());
            const NodeList& algo_body = algo_node->get_body();

            std::shared_ptr<Value> res = ret;
            for (int i = 0; i < algo_body.size(); ++i) {
                res = interpreter.visit(algo_body[i]);
                if (res->get_type() == VALUE_ERROR) return res;
                if (res->get_type() == VALUE_RETURN)
                    return dynamic_cast<ReturnValue*>(res.get())->get_value();
            }
            return res;
        }
    }
    return std::make_shared<ErrorValue>(
        VALUE_ERROR, "Unknown member or invalid object for " + method_name + "\n");
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_print(const std::string& str) {
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

std::shared_ptr<Value> BuiltinAlgoValue::execute_int(const std::string& str) {
    if (str[0] != '-' && !std::isdigit(str[0])) {
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            "Cannot convert \"" + str + "\" to an int");
    }
    for (int i{1}; i < str.size(); ++i)
        if (!std::isdigit(str[0]))
            return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                "Cannot convert \"" + str + "\" to an int");
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT, std::stoll(str));
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_float(const std::string& str) {
    int point{0};
    if (str[0] != '-' && !std::isdigit(str[0])) {
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            "Cannot convert \"" + str + "\" to an int");
    }
    for (int i{1}; i < str.size(); ++i) {
        if (!std::isdigit(str[0]) && (str[0] != '.' || point == 1)) {
            return std::make_shared<ErrorValue>(VALUE_ERROR,
                                                "Cannot convert \"" + str + "\" to an int");
        }
        if (str[0] == '.') point++;
    }
    return std::make_shared<TypedValue<double>>(VALUE_FLOAT, std::stod(str));
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_string(const std::string& str) {
    return std::make_shared<TypedValue<std::string>>(VALUE_STRING, str);
}

std::shared_ptr<Value> operator+(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, a->as_double() + b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() + b->as_int());
    else if (a->get_type() == VALUE_STRING && b->get_type() == VALUE_STRING)
        return std::make_shared<TypedValue<std::string>>(VALUE_STRING,
                                                         a->as_string() + b->as_string());
    else
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            Color(0xFF, 0x39, 0x6E).get() +
                                                "Runtime ERROR: ADD operation can only apply on "
                                                "number or two string\n" RESET);
}

std::shared_ptr<Value> operator-(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, a->as_double() - b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() - b->as_int());
    else
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() +
                             "Runtime ERROR: SUB operation can only apply on number\n" RESET);
}

std::shared_ptr<Value> operator*(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, a->as_double() * b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() * b->as_int());
    else if (a->get_type() == VALUE_STRING && b->get_type() == VALUE_INT) {
        std::string ret, str_a{a->as_string()};
        int64_t times{b->as_int()};
        for (int i{0}; i < times; ++i) ret += str_a;
        return std::make_shared<TypedValue<std::string>>(VALUE_STRING, ret);
    } else
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            Color(0xFF, 0x39, 0x6E).get() +
                                                "Runtime ERROR: MUL operation can only apply on "
                                                "number or string and int\n" RESET);
}

std::shared_ptr<Value> operator/(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (b->as_double() == 0.0)
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: DIV by 0\n" RESET);
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, a->as_double() / b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() / b->as_int());
    else
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() +
                             "Runtime ERROR: DIV operation can only apply on number\n" RESET);
}

std::shared_ptr<Value> operator%(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() != VALUE_INT || b->get_type() != VALUE_INT)
        return std::make_shared<ErrorValue>(
            VALUE_ERROR,
            Color(0xFF, 0x39, 0x6E).get() + "Cannot apply \"%\" operation on float\n" RESET);
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() % b->as_int());
}

std::shared_ptr<Value> operator==(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_INSTANCE || b->get_type() == VALUE_INSTANCE ||
        a->get_type() == VALUE_ARRAY || b->get_type() == VALUE_ARRAY ||
        a->get_type() == VALUE_HASH_TABLE || b->get_type() == VALUE_HASH_TABLE ||
        a->get_type() == VALUE_STRUCT || b->get_type() == VALUE_STRUCT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a.get() == b.get());
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_double() == b->as_double());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_string() == b->as_string());
}

std::shared_ptr<Value> operator!=(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_INSTANCE || b->get_type() == VALUE_INSTANCE ||
        a->get_type() == VALUE_ARRAY || b->get_type() == VALUE_ARRAY ||
        a->get_type() == VALUE_HASH_TABLE || b->get_type() == VALUE_HASH_TABLE ||
        a->get_type() == VALUE_STRUCT || b->get_type() == VALUE_STRUCT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a.get() != b.get());
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_double() != b->as_double());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_string() != b->as_string());
}

std::shared_ptr<Value> operator<(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_double() < b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() < b->as_int());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_string() < b->as_string());
}

std::shared_ptr<Value> operator>(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_double() > b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() > b->as_int());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_string() > b->as_string());
}

std::shared_ptr<Value> operator<=(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_double() <= b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() <= b->as_int());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_string() <= b->as_string());
}

std::shared_ptr<Value> operator>=(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_double() >= b->as_double());
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() >= b->as_int());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_string() >= b->as_string());
}

std::shared_ptr<Value> operator&&(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                     a->as_double() != 0 && b->as_double() != 0);
    else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                     a->as_int() != 0 && b->as_int() != 0);
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() && b->as_int());
}

std::shared_ptr<Value> operator||(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                     a->as_double() != 0 || b->as_double() != 0);
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() || b->as_int());
}

std::shared_ptr<Value> operator-(std::shared_ptr<Value> a) {
    if (a->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, 0 - a->as_double());
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, 0 - a->as_int());
}

std::shared_ptr<Value> operator!(std::shared_ptr<Value> a) {
    if (a->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT, a->as_double() == 0);
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, a->as_int() == 0);
}

std::shared_ptr<Value> pow(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
    if (a->as_double() == 0.0 && b->as_double() == 0.0)
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: 0 to the 0\n" RESET);
    if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
        return std::make_shared<TypedValue<double>>(VALUE_FLOAT,
                                                    std::pow(a->as_double(), b->as_double()));
    else
        return std::make_shared<TypedValue<int64_t>>(VALUE_INT, std::pow(a->as_int(), b->as_int()));
}

std::shared_ptr<Value> InstanceValue::get_member(const std::string& name,
                                                 std::shared_ptr<Value> self) {
    if (members.count(name)) {
        return members[name];
    }
    // Check for methods in struct definition
    if (struct_def->methods.count(name)) {
        if (self.get() == nullptr) {
            // Fallback if self not provided, but this shouldn't happen for method calls
            // Create a copy? Or error?
            // For now, create a copy as before, but warn?
            return std::make_shared<BoundMethodValue>(std::make_shared<InstanceValue>(*this), name);
        }
        return std::make_shared<BoundMethodValue>(self, name);
    }
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Member not found: " + name);
}

void InstanceValue::set_member(const std::string& name, std::shared_ptr<Value> val) {
    // If it's declared in struct def members, we can set it.
    bool found = false;
    for (const auto& mem : struct_def->members) {
        if (mem == name) {
            found = true;
            break;
        }
    }
    if (found) {
        members[name] = val;
    } else {
        members[name] = val;  // Just set it for now.
    }
}
std::shared_ptr<Value> StructValue::execute(const NodeList& args, SymbolTable* parent) {
    // Constructor call
    std::shared_ptr<InstanceValue> instance =
        std::make_shared<InstanceValue>(std::make_shared<StructValue>(*this));

    // Initialize members to NONE
    for (const auto& member : members) {
        instance->set_member(member, std::make_shared<Value>(VALUE_NONE));
    }

    // Call constructor if exists
    if (methods.count("constructor")) {
        std::shared_ptr<Value> ctor = methods["constructor"];
        // We need to bind the constructor to the instance
        std::shared_ptr<BoundMethodValue> bound_ctor =
            std::make_shared<BoundMethodValue>(instance, "constructor");
        // But BoundMethodValue execute logic for custom objects is not implemented
        // in pseudo.cpp yet (only ArrayValue). We need to implement it. Actually,
        // let's reuse AlgoValue::execute but inject 'self'.

        // Wait, BoundMethodValue holds the object and the method name.
        // Its execute() needs to look up the method (which we have in 'ctor') and
        // call it with 'self' = obj.

        // Actually, if we use BoundMethodValue, we need to implement execute for
        // generic objects. Alternatively, we can manually call ctor->execute(args,
        // parent) but we need to inject 'self'. AlgoValue::execute creates a new
        // symbol table. We need to add 'self' to it. But AlgoValue::execute
        // interface doesn't allow injecting symbols easily before execution.
        // However, AlgoValue::execute does:
        // SymbolTable sym(parent);
        // set_args(args, sym, interpreter);
        // ...

        // We can manually do what AlgoValue::execute does.
        // Or we can modify AlgoValue to support binding?
        // Or implement BoundMethodValue::execute properly.
    }

    // For now, let's rely on BoundMethodValue which we will implement/update in
    // pseudo.cpp.
    if (methods.count("constructor")) {
        std::shared_ptr<BoundMethodValue> bound_ctor =
            std::make_shared<BoundMethodValue>(instance, "constructor");
        std::shared_ptr<Value> ret = bound_ctor->execute(args, parent);
        if (ret->get_type() == VALUE_ERROR) return ret;
    }

    return instance;
}

/// --------------------
/// Run
/// --------------------

namespace {

std::string trim(const std::string& str) {
    size_t begin = 0;
    while (begin < str.size() && std::isspace(static_cast<unsigned char>(str[begin]))) {
        begin++;
    }

    size_t end = str.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(str[end - 1]))) {
        end--;
    }
    return str.substr(begin, end - begin);
}

bool parse_import_line(const std::string& line, std::string& target) {
    std::string trimmed = trim(line);
    std::string keyword = "import";
    if (trimmed.rfind(keyword, 0) != 0) {
        return false;
    }
    if (trimmed.size() > keyword.size() &&
        !std::isspace(static_cast<unsigned char>(trimmed[keyword.size()]))) {
        return false;
    }

    target = trim(trimmed.substr(keyword.size()));
    if (target.empty()) {
        return false;
    }

    if (target.front() == '"' && target.back() == '"' && target.size() >= 2) {
        target = target.substr(1, target.size() - 2);
    }
    return !target.empty();
}

std::vector<std::filesystem::path> candidate_import_paths(const std::string& target,
                                                          const std::filesystem::path& base_dir) {
    namespace fs = std::filesystem;
    fs::path target_path(target);
    std::vector<fs::path> candidates;

    if (target_path.is_absolute()) {
        candidates.push_back(target_path);
    } else {
        if (!target_path.has_extension()) {
            candidates.push_back(fs::current_path() / "lib" / (target + ".ps"));
        }
        candidates.push_back(base_dir / target_path);
        candidates.push_back(fs::current_path() / target_path);
    }

    if (!target_path.has_extension()) {
        candidates.push_back(base_dir / (target + ".ps"));
        candidates.push_back(fs::current_path() / (target + ".ps"));
    }

    return candidates;
}

bool read_file(const std::filesystem::path& path, std::string& text) {
    std::ifstream input(path);
    if (!input) {
        return false;
    }
    std::stringstream buffer;
    buffer << input.rdbuf();
    text = buffer.str();
    if (!text.empty() && text.back() != '\n') {
        text += '\n';
    }
    return true;
}

bool resolve_import(const std::string& target, const std::filesystem::path& base_dir,
                    std::filesystem::path& resolved) {
    namespace fs = std::filesystem;
    for (const auto& candidate : candidate_import_paths(target, base_dir)) {
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            resolved = fs::weakly_canonical(candidate, ec);
            if (ec) {
                resolved = fs::absolute(candidate, ec);
            }
            return true;
        }
    }
    return false;
}

}  // namespace

bool expand_imports(const std::string& file_name, const std::string& text, ImportState& state,
                    std::string& expanded, std::string& error) {
    namespace fs = std::filesystem;
    fs::path current_path(file_name);
    fs::path base_dir = current_path.has_parent_path() ? fs::absolute(current_path).parent_path()
                                                       : fs::current_path();

    std::stringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        std::string target;
        if (!parse_import_line(line, target)) {
            expanded += line + "\n";
            continue;
        }

        fs::path import_path;
        if (!resolve_import(target, base_dir, import_path)) {
            error = "Import ERROR: cannot resolve \"" + target + "\" from " + base_dir.string();
            return false;
        }

        std::string import_key = import_path.string();
        if (state.loaded.count(import_key)) {
            continue;
        }
        if (state.loading.count(import_key)) {
            error = "Import ERROR: circular import involving " + import_key;
            return false;
        }

        std::string import_text;
        if (!read_file(import_path, import_text)) {
            error = "Import ERROR: cannot read " + import_key;
            return false;
        }

        state.loading.insert(import_key);
        std::string import_expanded;
        if (!expand_imports(import_key, import_text, state, import_expanded, error)) {
            return false;
        }
        state.loading.erase(import_key);
        state.loaded.insert(import_key);
        expanded += import_expanded;
        expanded += "\n";
    }
    return true;
}

std::string run(std::string file_name, std::string text, SymbolTable& global_symbol_table) {
    ImportState import_state;
    std::string expanded_text;
    std::string import_error;
    if (!expand_imports(file_name, text, import_state, expanded_text, import_error)) {
        std::cout << import_error << "\n";
        return "ABORT";
    }

    Lexer lexer(file_name, expanded_text);
    TokenList tokens = lexer.make_tokens();
    if (tokens.empty()) return "";
    if (tokens[0]->get_type() == TOKEN_ERROR) {
        // std::cout << "Tokens: " << tokens << "\n";
        // Detailed Lexer Error?
        // Lexer returns TOKEN_ERROR in list.
        // But lexer usually returns a list with one error token if it fails?
        // Or a list where some tokens are errors.
        // Check lexer.cpp.
        // Lexer::make_tokens() returns a list. If error, it pushes ErrorToken.
        // Let's iterate and find errors.
        for (auto tok : tokens) {
            if (tok->get_type() == TOKEN_ERROR) {
                std::cout << "Error: " << tok->get_value() << ", line " << tok->get_pos().line + 1
                          << ", column: " << tok->get_pos().column << "\n";
                std::cout << error_marker(expanded_text, tok->get_pos(), tok->get_pos());
                return "ABORT";
            }
        }
    }

    Parser parser(tokens);
    NodeList ast = parser.parse();

    for (auto node : ast) {
        if (node->get_type() == NODE_ERROR) {
            // std::cout << "Nodes: " << node->get_node() << "\n";
            std::shared_ptr<Token> err_tok = node->get_tok();
            if (err_tok) {
                std::cout << "Error: " << err_tok->get_value() << ", line "
                          << err_tok->get_pos().line + 1
                          << ", column: " << err_tok->get_pos().column << "\n";
                std::cout << error_marker(expanded_text, err_tok->get_pos(), err_tok->get_pos());
            } else {
                std::cout << "Error: " << node->get_node() << "\n";
            }
            return "ABORT";
        }
    }

    Interpreter interpreter(global_symbol_table, file_name == "stdin");
    ArrayValue* ret{new ArrayValue(ValueList(0))};
    for (auto node : ast) {
        ret->push_back(interpreter.visit(node));
        if (ret->back()->get_type() == VALUE_ERROR) {
            std::cout << ret->back()->get_num() << "\n";
            return "ABORT";
        }
    }

    while (ret->get_type() == VALUE_ARRAY && ret->back()->get_type() == VALUE_ARRAY) {
        ret = dynamic_cast<ArrayValue*>(ret->back().get());
    }

    if (file_name == "stdin" && ret->operator[](0)->get_type() != VALUE_NONE) {
        std::cout << ret->get_num() << "\n";
    }
    return "";
}
