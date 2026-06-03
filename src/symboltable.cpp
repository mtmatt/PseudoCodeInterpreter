/// --------------------
/// SymbolTable
/// --------------------

#include "symboltable.h"
#include "color.h"
#include <memory>

std::shared_ptr<Value> SymbolTable::get(const std::string& name) {
    auto found = symbols.find(name);
    if(found == symbols.end()){
        if(parent != nullptr) {
            return parent->get(name);
        } else if(BUILTIN_ALGOS.count(name)) {
            return BUILTIN_ALGOS.at(name);
        } else {
            return std::make_shared<ErrorValue>(
                VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Identifier: \""+ name +"\" has not defined\n" RESET);
        }
    }
    return found->second;
}

void SymbolTable::set(const std::string& name, std::shared_ptr<Value> value) {
    if (value->get_type() == VALUE_INSTANCE) {
        contains_instance = true;
    }
    symbols[name] = value;
}

void SymbolTable::erase(const std::string& name) {
    symbols.erase(name);
}

bool SymbolTable::contains_local(const std::string& name) const {
    return symbols.find(name) != symbols.end();
}

std::shared_ptr<Value> SymbolTable::get_local(const std::string& name) const {
    auto found = symbols.find(name);
    if (found == symbols.end()) {
        return nullptr;
    }
    return found->second;
}
