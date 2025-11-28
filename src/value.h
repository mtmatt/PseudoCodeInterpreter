/// --------------------
/// Value
/// --------------------

#ifndef VALUE_H
#define VALUE_H

#include <string>
#include <vector>
#include <memory>
#include <map>
#include "node.h"

const std::string VALUE_NONE{"NONE"};
const std::string VALUE_INT{"Int"};
const std::string VALUE_FLOAT{"Float"};
const std::string VALUE_ALGO{"Algo"};
const std::string VALUE_STRING{"Str"};
const std::string VALUE_ERROR{"ERROR"};
const std::string VALUE_ARRAY{"Array"};
const std::string VALUE_STRUCT{"Struct"};
const std::string VALUE_INSTANCE{"Instance"};

const std::map<char, char> REVERSE_ESCAPE_CHAR {
    {'\n', 'n'}, {'\r', 'r'},
    {'\b', 'b'}, {'\"', '\"'},
    {'\'', '\''}, {'\\', '\\'},
    {'\t', 't'}
};

class SymbolTable;
class Interpreter;
class Value {
public:
    Value(const std::string& _type = VALUE_NONE)
        : type(_type) {}
    virtual std::string get_num() { return type;}
    virtual std::string repr() { return type;}
    virtual std::string get_type(){ return type;}
    virtual std::shared_ptr<Value> execute(NodeList args = {}, SymbolTable *parent = nullptr)
        { return std::make_shared<Value>();};
    friend std::ostream& operator<<(std::ostream &out, Value &token);
    
protected:
    std::string type;
};

using ValueList = std::vector<std::shared_ptr<Value>>;

template<typename T>
class TypedValue: public Value {
public:
    TypedValue(const std::string& _type, const T &_value) 
        : Value(_type), value(_value) {}
    std::string get_num() override;
    std::string repr() override;

protected:
    T value;
};

using ErrorValue = TypedValue<std::string>;

class BaseAlgoValue: public Value {
public:
    BaseAlgoValue(const std::string &_algo_name, std::shared_ptr<Node> _value) 
        : Value(VALUE_ALGO), value(_value), algo_name(_algo_name) {}
    std::string get_num() override { return algo_name;}
    virtual std::shared_ptr<Value> set_args(NodeList&, SymbolTable&, Interpreter&);
    std::string repr() override { return get_num();}

protected:
    std::string algo_name;
    std::shared_ptr<Node> value;
};

class AlgoValue: public BaseAlgoValue {
public:
    AlgoValue(const std::string &_algo_name, std::shared_ptr<Node> _value) 
        : BaseAlgoValue(_algo_name, _value) {}
    std::string get_num() override { return algo_name;}
    std::string repr() override { return get_num();}
    std::shared_ptr<Value> execute(NodeList args = {}, SymbolTable *parent = nullptr) override;
    friend class BoundMethodValue;
    // Expose value for friends/derived or public use if needed for method binding
    std::shared_ptr<Node> get_node_ptr() { return value; }
protected:
};

class BuiltinAlgoValue: public BaseAlgoValue {
public:
    BuiltinAlgoValue(const std::string &_algo_name, std::shared_ptr<Node> _value) 
        : BaseAlgoValue(_algo_name, _value) {}
    std::string get_num() override { return algo_name;}
    std::shared_ptr<Value> execute(NodeList args = {}, SymbolTable *parent = nullptr) override;
    std::shared_ptr<Value> execute_print(const std::string&);
    std::shared_ptr<Value> execute_read();
    std::shared_ptr<Value> execute_read_line();
    std::shared_ptr<Value> execute_clear();
    std::shared_ptr<Value> execute_int(const std::string&);
    std::shared_ptr<Value> execute_float(const std::string&);
    std::shared_ptr<Value> execute_string(const std::string&);
    std::string repr() override { return get_num();}
protected:
};

class BoundMethodValue : public Value {
public:
    BoundMethodValue(std::shared_ptr<Value> _obj, std::string _method_name)
        : Value(VALUE_ALGO), obj(_obj), method_name(_method_name) {}
    std::shared_ptr<Value> execute(NodeList args = {}, SymbolTable *parent = nullptr) override;
    std::string get_num() override { return method_name; }
    std::string repr() override { return "<Bound Method " + method_name + ">"; }
protected:
    std::shared_ptr<Value> obj;
    std::string method_name;
};

class ArrayValue: public Value {
public:
    ArrayValue(ValueList _value) 
        : Value(VALUE_ARRAY), value(_value) {}
    std::string get_num() override;
    std::shared_ptr<Value>& operator[](int p);
    void push_back(std::shared_ptr<Value>);
    std::shared_ptr<Value> pop_back();
    std::shared_ptr<Value>& size() { return sz = std::make_shared<TypedValue<int64_t>>(VALUE_INT, value.size());};
    std::shared_ptr<Value>& back() { return value.back();};
    std::string repr() override { return get_num();}

    void resize(int new_size) {
        if (new_size < 0) {
            return;
        }
        if (static_cast<size_t>(new_size) < value.size()) {
            value.resize(new_size);
        } else if (static_cast<size_t>(new_size) > value.size()) {
            size_t old_size = value.size();
            value.resize(new_size);
            for (size_t i = old_size; i < static_cast<size_t>(new_size); ++i) {
                value[i] = std::make_shared<Value>(); // Fill with default Value
            }
        }
    }

    std::shared_ptr<Value> sz, error;
protected:
    ValueList value;
};

std::shared_ptr<Value> operator+(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator-(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator*(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator/(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator%(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> pow(std::shared_ptr<Value>, std::shared_ptr<Value>);

std::shared_ptr<Value> operator==(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator!=(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator<(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator>(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator<=(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator>=(std::shared_ptr<Value>, std::shared_ptr<Value>);

std::shared_ptr<Value> operator&&(std::shared_ptr<Value>, std::shared_ptr<Value>);
std::shared_ptr<Value> operator||(std::shared_ptr<Value>, std::shared_ptr<Value>);

std::shared_ptr<Value> operator-(std::shared_ptr<Value>);
std::shared_ptr<Value> operator!(std::shared_ptr<Value>);

class StructValue : public Value {
public:
    StructValue(const std::string& _name, const std::vector<std::string>& _members, const std::map<std::string, std::shared_ptr<Value>>& _methods)
        : Value(VALUE_STRUCT), name(_name), members(_members), methods(_methods) {}

    std::string get_num() override { return name; }
    std::string repr() override { return "<Struct " + name + ">"; }

    std::vector<std::string> members;
    std::map<std::string, std::shared_ptr<Value>> methods;
    std::string name;
    std::shared_ptr<Value> execute(NodeList args = {}, SymbolTable *parent = nullptr) override;
};

class InstanceValue : public Value {
public:
    InstanceValue(std::shared_ptr<StructValue> _struct_def)
        : Value(VALUE_INSTANCE), struct_def(_struct_def) {}

    std::string get_num() override { return struct_def->name + " Instance"; }
    std::string repr() override { return "<Instance of " + struct_def->name + ">"; }

    std::shared_ptr<Value> get_member(const std::string& name);
    void set_member(const std::string& name, std::shared_ptr<Value> val);

    std::shared_ptr<StructValue> struct_def;
    std::map<std::string, std::shared_ptr<Value>> members;
};

template class TypedValue<double>;
template class TypedValue<int64_t>;
template class TypedValue<std::string>;

#endif
