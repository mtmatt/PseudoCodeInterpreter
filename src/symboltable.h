/// --------------------
/// SymbolTable
/// --------------------

#ifndef SYMBOL_H
#define SYMBOL_H

#include "node.h"
#include "value.h"
#include <map>
#include <string>
#include <memory>

const std::map<std::string, std::shared_ptr<Value>> BUILTIN_ALGOS {
    {"print", std::make_shared<BuiltinAlgoValue>("print", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "print"), 
        TokenList{std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "s")}))}, 
    {"read", std::make_shared<BuiltinAlgoValue>("read", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "read"), 
        TokenList{}))},
    {"read_line", std::make_shared<BuiltinAlgoValue>("read_line", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "read_line"), 
        TokenList{}))},
    {"open", std::make_shared<BuiltinAlgoValue>("open", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "open"), 
        TokenList{}))},
    {"clear", std::make_shared<BuiltinAlgoValue>("clear", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "clear"), 
        TokenList{}))},
    {"quit", std::make_shared<BuiltinAlgoValue>("quit", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "quit"), 
        TokenList{}))},
    {"int", std::make_shared<BuiltinAlgoValue>("int", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "int"), 
        TokenList{std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "n")}))}, 
    {"float", std::make_shared<BuiltinAlgoValue>("float",
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "float"), 
        TokenList{std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "n")}))}, 
    {"string", std::make_shared<BuiltinAlgoValue>("string", 
        std::make_shared<AlgorithmDefNode>(std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "string"), 
        TokenList{std::make_shared<TypedToken<std::string>>(TOKEN_STRING, Position(), "s")}))}, 
};

class SymbolTable {
public:
    SymbolTable(SymbolTable *_parent = nullptr)
        : parent(_parent) {}
    std::shared_ptr<Value> get(std::string);
    void set(std::string, std::shared_ptr<Value>);
    void erase(std::string);
    const std::map<std::string, std::shared_ptr<Value>>& get_symbols() const { return symbols; }
    SymbolTable* get_parent() const { return parent; }
protected:
    std::map<std::string, std::shared_ptr<Value>> symbols;
    SymbolTable *parent;
};

#endif