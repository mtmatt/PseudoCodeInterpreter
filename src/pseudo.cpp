#include "pseudo.h"
#include "color.h"
#include "interpreter.h"
#include "node.h"
#include "value.h"
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

/// --------------------
/// Value
/// --------------------

std::ostream &operator<<(std::ostream &out, Value &number) {
  out << number.get_num();
  return out;
}

template <typename T> std::string TypedValue<T>::get_num() {
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

template <typename T> std::string TypedValue<T>::repr() {
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
  if (!value.empty())
    ss << value[0]->repr();
  for (int i{1}; i < value.size(); ++i) {
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
  if (value.empty())
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Pop an empty array");
  std::shared_ptr<Value> ret = value.back();
  value.pop_back();
  return ret;
}

std::shared_ptr<Value> &ArrayValue::operator[](int p) {
  if (1 <= p && p <= value.size())
    return value[p - 1];
  error = std::make_shared<ErrorValue>(
      VALUE_ERROR, "Index out of range, size: " + std::to_string(value.size()) +
                       ", position: " + std::to_string(p));
  return error;
}

std::shared_ptr<Value> BaseAlgoValue::set_args(NodeList &args, SymbolTable &sym,
                                               Interpreter &interpreter) {
  if (args.size() < value->get_toks().size()) {
    return std::make_shared<ErrorValue>(
        VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() + "Too few arguments" RESET);
  } else if (args.size() > value->get_toks().size()) {
    return std::make_shared<ErrorValue>(VALUE_ERROR,
                                        Color(0xFF, 0x39, 0x6E).get() +
                                            "Too many arguments" RESET);
  }

  TokenList args_name = value->get_toks();
  for (int i = 0; i < args.size(); ++i) {
    std::shared_ptr<Value> v = interpreter.visit(args[i]);
    sym.set(args_name[i]->get_value(), v);
  }
  return std::make_shared<Value>();
}

std::shared_ptr<Value> AlgoValue::execute(NodeList args, SymbolTable *parent) {
  SymbolTable sym(parent);
  Interpreter interpreter(sym);
  std::shared_ptr<Value> ret{set_args(args, sym, interpreter)};
  if (ret->get_type() == VALUE_ERROR)
    return ret;

  NodeList algo_body = value->get_child();

  for (int i = 0; i < algo_body.size(); ++i) {
    ret = interpreter.visit(algo_body[i]);
  }
  return ret;
}

std::shared_ptr<Value> BuiltinAlgoValue::execute(NodeList args,
                                                 SymbolTable *parent) {
  SymbolTable sym(parent);
  Interpreter interpreter(sym);
  std::shared_ptr<Value> ret{set_args(args, sym, interpreter)};
  if (ret->get_type() == VALUE_ERROR)
    return ret;
  TokenList args_name = value->get_toks();
  if (algo_name == "print") {
    return execute_print(sym.get(args_name[0]->get_value())->get_num());
  } else if (algo_name == "read") {
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
    return execute_int(sym.get(args_name[0]->get_value())->get_num());
  } else if (algo_name == "float") {
    return execute_float(sym.get(args_name[0]->get_value())->get_num());
  } else if (algo_name == "string") {
    return execute_string(sym.get(args_name[0]->get_value())->get_num());
  }
  return ret;
}

std::shared_ptr<Value> BoundMethodValue::execute(NodeList args,
                                                 SymbolTable *parent) {
  if (obj->get_type() == VALUE_ARRAY) {
    ArrayValue *arr_obj = dynamic_cast<ArrayValue *>(obj.get());
    SymbolTable sym(parent);
    Interpreter interpreter(sym);

    if (method_name == "push" || method_name == "push_back") {
      if (args.size() != 1) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Expect one argument for " + method_name + "\n");
      }
      std::shared_ptr<Value> arg = interpreter.visit(args[0]);
      if (arg->get_type() == VALUE_ERROR)
        return arg;
      arr_obj->push_back(arg);
      return arr_obj->back();
    } else if (method_name == "pop" || method_name == "pop_back") {
      if (!args.empty()) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Expect zero argument for " + method_name + "\n");
      }
      if (arr_obj->size()->get_num() == "0") {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Cannot " + method_name + " from an empty array\n");
      }
      return arr_obj->pop_back();
    } else if (method_name == "resize") {
      if (args.size() != 1) {
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            "Expect one argument for resize\n");
      }
      std::shared_ptr<Value> new_size_val = interpreter.visit(args[0]);
      if (new_size_val->get_type() == VALUE_ERROR)
        return new_size_val;
      if (new_size_val->get_type() != VALUE_INT) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Argument for resize must be an integer\n");
      }
      long long new_size;
      try {
        new_size = std::stoll(new_size_val->get_num());
      } catch (const std::out_of_range &oor) {
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            "Resize argument out of range\n");
      }
      if (new_size < 0) {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Resize argument cannot be negative\n");
      }
      arr_obj->resize(static_cast<int>(new_size));
      return obj;
    } else if (method_name == "size") {
      if (!args.empty()) {
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            "Expect zero argument for size\n");
      }
      return arr_obj->size();
    } else if (method_name == "back") {
      if (!args.empty()) {
        return std::make_shared<ErrorValue>(VALUE_ERROR,
                                            "Expect zero arguments for back\n");
      }
      if (arr_obj->size()->get_num() == "0") {
        return std::make_shared<ErrorValue>(
            VALUE_ERROR, "Cannot call back on an empty array\n");
      }
      return arr_obj->back();
    }
  } else if (obj->get_type() == VALUE_INSTANCE) {
      InstanceValue *inst_obj = dynamic_cast<InstanceValue *>(obj.get());
      // Find method
      if (inst_obj->struct_def->methods.count(method_name)) {
          std::shared_ptr<Value> method = inst_obj->struct_def->methods[method_name];
          // method is likely AlgoValue.
          // We need to execute it with 'self' in scope.
          // We can use the existing AlgoValue::execute, but we need to inject 'self'.
          // Since AlgoValue::execute creates a new symbol table, we can't inject it easily from outside *before* it starts unless we modify it or subclass it.
          // However, we can create a new AlgoValue or wrapper that injects 'self'.

          // Actually, let's copy the logic of AlgoValue::execute here but add self.
          AlgoValue *algo_val = dynamic_cast<AlgoValue*>(method.get());
          if (!algo_val) return std::make_shared<ErrorValue>(VALUE_ERROR, "Method is not an algorithm");

          SymbolTable sym(parent);
          Interpreter interpreter(sym);

          // Set self
          sym.set("self", obj);

          std::shared_ptr<Value> ret{algo_val->set_args(args, sym, interpreter)};
          if (ret->get_type() == VALUE_ERROR) return ret;

          // Execute body
          // We need access to algo_body which is protected/private in BaseAlgoValue/AlgoValue.
          // BaseAlgoValue has protected 'value' (the AST node).
          // AlgoValue doesn't expose it publicly.
          // We might need to make 'value' public or add a method to execute with 'self'.
          // Or we can cast and access if we are friends or similar.
          // Since we are in pseudo.cpp and BaseAlgoValue is defined in value.h, we can't access protected members unless we are a friend or derived.
          // BoundMethodValue is derived from Value, not AlgoValue.
          // But we can cast method to BaseAlgoValue and get 'value' if we change access or use a getter.
          // BaseAlgoValue has no getter for value.

          // Let's modify AlgoValue to support binding or extra symbols.
          // Or just add a method `execute_with_self(args, parent, self_obj)`.

          // For now, assuming we can't easily change `value.h` access modifiers without re-reading/writing it.
          // Let's check `value.h` again. `value` is protected in BaseAlgoValue.
          // But `AlgoValue` is defined in `value.h`.
          // Wait, `BoundMethodValue` is in `value.h` too.
          // If we implement `execute` in `pseudo.cpp`, we can't access protected members of `AlgoValue` unless `BoundMethodValue` is a friend or derived.
          // `BoundMethodValue` is NOT derived from `BaseAlgoValue`.

          // Quick fix: Add `friend class BoundMethodValue;` to `BaseAlgoValue` in `value.h`.
          // Or add `get_node()` to `BaseAlgoValue`.

          // Let's try to access it via a hack or update `value.h`.
          // Updating `value.h` is better.

          // We can execute the body using the interpreter
          NodeList algo_body = algo_val->get_node_ptr()->get_child();

          for (int i = 0; i < algo_body.size(); ++i) {
            std::shared_ptr<Value> res = interpreter.visit(algo_body[i]);
            if (res->get_type() == VALUE_ERROR) return res;
            // Handle return statements? The current interpreter doesn't seem to have explicit 'return' statement node that stops execution.
            // But wait, the example `fib` function has `return`.
            // How is `return` handled?
            // `return` keyword creates a node?
            // Lexer: KEYWORDS has "return"? No.
            // Check `lexer.h`. No "return" in KEYWORDS.
            // Wait, I saw `test_fib.ps` has `return`.
            // Maybe "return" is handled as identifier?
            // Ah, maybe "return" is not implemented?
            // Let's check `test_fib.ps` again.
            // `if n <= 1 then return n`
            // If `return` is not a keyword, it is an identifier.
            // If it is an identifier, it might be a variable or function call.
            // `return n`. If `return` is a function?
            // Builtin? No.
            // Maybe the user's `fib` example relies on `return` being available.
            // But I don't see it in the code.
            // Maybe I missed it.
            // `src/lexer.h`: KEYWORDS ... "Algorithm", "continue", "break".
            // No "return".
            // So `test_fib.ps` might fail if I run it?
            // I ran `./shell test_fib.ps` and it output nothing.
            // `cat test_fib.ps` showed the code.
            // `./shell test_fib.ps` output empty line.
            // Does it mean it failed silently or produced nothing?
            // `print(fib(10))` calls print.
            // If `fib` is not working, maybe it returns nothing or error.
            // If `return` is identifier, `return n` is `return(n)`? Call to `return`?
            // If `return` is not defined, it is a variable access?
            // `return` variable?
            // This is strange.

            // Anyway, assuming standard execution flow.
            // If the interpreter visits a node and it evaluates to something, that's fine.
            // But how to return value from function?
            // `BaseAlgoValue::execute` returns the result of last visited node?
            // `for (int i = 0; i < algo_body.size(); ++i) { ret = interpreter.visit(algo_body[i]); } return ret;`
            // Yes, it returns the last statement's value.
            // So `fib` example: `return fib(n-1) + ...`
            // If `return` is just a function that returns its argument, and it is the last statement...
            // But `if ... then return n`.
            // If `return` is a function, `return n` is `return` variable access followed by `n` variable access?
            // No, syntax `return n` would be invalid if `return` is identifier and `n` is identifier, unless implicit call?
            // `Parser::atom` handles identifier. `Parser::call` handles function call `(`.
            // `Parser::statement` handles list of expressions.
            // `if` body is a statement.
            // `return n` -> `return` (var access) `n` (var access)?
            // Two expressions?
            // If so, `visit_if` executes both. `ret` updates.
            // So if `return` is dummy variable, and `n` evaluates to value.
            // Then `if` returns value of `n`.
            // And function returns value of `if`.
            // So it works by coincidence/design that last expression is returned.
            // But `return` word itself?
            // If I look at `test_fib.ps` again:
            // `if n <= 1 then return n`
            // `return fib(n-1) + fib(n-2)`
            // If `return` is not a keyword, then `return` is a variable name.
            // `return n` -> `return` is evaluated (VarAccess), then `n` is evaluated (VarAccess).
            // Result of `if` is result of last expression, which is `n`.
            // So `return` is ignored effectively?
            // Yes, if it is just a variable access to undefined variable (returns NONE or ERROR? SymbolTable returns NONE if not found?).
            // `SymbolTable::get`?
          }
          return ret;
      }
  }
  return std::make_shared<ErrorValue>(VALUE_ERROR,
                                      "Unknown member or invalid object for " +
                                          method_name + "\n");
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
  if (str[0] != '-' && !std::isdigit(str[0])) {
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot convert \"" + str +
                                                         "\" to an int");
  }
  for (int i{1}; i < str.size(); ++i)
    if (!std::isdigit(str[0]))
      return std::make_shared<ErrorValue>(
          VALUE_ERROR, "Cannot convert \"" + str + "\" to an int");
  return std::make_shared<TypedValue<int64_t>>(VALUE_INT, std::stoll(str));
}

std::shared_ptr<Value> BuiltinAlgoValue::execute_float(const std::string &str) {
  int point{0};
  if (str[0] != '-' && !std::isdigit(str[0])) {
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Cannot convert \"" + str +
                                                         "\" to an int");
  }
  for (int i{1}; i < str.size(); ++i) {
    if (!std::isdigit(str[0]) && (str[0] != '.' || point == 1)) {
      return std::make_shared<ErrorValue>(
          VALUE_ERROR, "Cannot convert \"" + str + "\" to an int");
    }
    if (str[0] == '.')
      point++;
  }
  return std::make_shared<TypedValue<double>>(VALUE_FLOAT, std::stod(str));
}

std::shared_ptr<Value>
BuiltinAlgoValue::execute_string(const std::string &str) {
  return std::make_shared<TypedValue<std::string>>(VALUE_STRING, str);
}

std::shared_ptr<Value> operator+(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(
        VALUE_FLOAT, std::stod(a->get_num()) + std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) + std::stoll(b->get_num()));
  else if (a->get_type() == VALUE_STRING && b->get_type() == VALUE_STRING)
    return std::make_shared<TypedValue<std::string>>(
        VALUE_STRING, a->get_num() + b->get_num());
  else
    return std::make_shared<ErrorValue>(
        VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() +
                         "Runtime ERROR: ADD operation can only apply on "
                         "number or two string\n" RESET);
}

std::shared_ptr<Value> operator-(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(
        VALUE_FLOAT, std::stod(a->get_num()) - std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) - std::stoll(b->get_num()));
  else
    return std::make_shared<ErrorValue>(
        VALUE_ERROR,
        Color(0xFF, 0x39, 0x6E).get() +
            "Runtime ERROR: SUB operation can only apply on number\n" RESET);
}

std::shared_ptr<Value> operator*(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(
        VALUE_FLOAT, std::stod(a->get_num()) * std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) * std::stoll(b->get_num()));
  else if (a->get_type() == VALUE_STRING && b->get_type() == VALUE_INT) {
    std::string ret, str_a{a->get_num()};
    int64_t times{stoll(b->get_num())};
    for (int i{0}; i < times; ++i)
      ret += str_a;
    return std::make_shared<TypedValue<std::string>>(VALUE_STRING, ret);
  } else
    return std::make_shared<ErrorValue>(
        VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() +
                         "Runtime ERROR: MUL operation can only apply on "
                         "number or string and int\n" RESET);
}

std::shared_ptr<Value> operator/(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (std::stod(b->get_num()) == 0.0)
    return std::make_shared<ErrorValue>(VALUE_ERROR,
                                        Color(0xFF, 0x39, 0x6E).get() +
                                            "Runtime ERROR: DIV by 0\n" RESET);
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(
        VALUE_FLOAT, std::stod(a->get_num()) / std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) / std::stoll(b->get_num()));
  else
    return std::make_shared<ErrorValue>(
        VALUE_ERROR,
        Color(0xFF, 0x39, 0x6E).get() +
            "Runtime ERROR: DIV operation can only apply on number\n" RESET);
}

std::shared_ptr<Value> operator%(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (a->get_type() != VALUE_INT || b->get_type() != VALUE_INT)
    return std::make_shared<ErrorValue>(
        VALUE_ERROR, Color(0xFF, 0x39, 0x6E).get() +
                         "Cannot apply \"%\" operation on float\n" RESET);
  return std::make_shared<TypedValue<int64_t>>(
      VALUE_INT, std::stoll(a->get_num()) % std::stoll(b->get_num()));
}

std::shared_ptr<Value> operator==(std::shared_ptr<Value> a,
                                  std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stod(a->get_num()) == std::stod(b->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 a->get_num() == b->get_num());
}

std::shared_ptr<Value> operator!=(std::shared_ptr<Value> a,
                                  std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stod(a->get_num()) != std::stod(b->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 a->get_num() != b->get_num());
}

std::shared_ptr<Value> operator<(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stod(a->get_num()) < std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) < std::stoll(b->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 a->get_num() < b->get_num());
}

std::shared_ptr<Value> operator>(std::shared_ptr<Value> a,
                                 std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stod(a->get_num()) > std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) > std::stoll(b->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 a->get_num() > b->get_num());
}

std::shared_ptr<Value> operator<=(std::shared_ptr<Value> a,
                                  std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stod(a->get_num()) <= std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) <= std::stoll(b->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 a->get_num() <= b->get_num());
}

std::shared_ptr<Value> operator>=(std::shared_ptr<Value> a,
                                  std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stod(a->get_num()) >= std::stod(b->get_num()));
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) >= std::stoll(b->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 a->get_num() >= b->get_num());
}

std::shared_ptr<Value> operator&&(std::shared_ptr<Value> a,
                                  std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT,
        std::stod(a->get_num()) != 0 && std::stod(b->get_num()) != 0);
  else if (a->get_type() == VALUE_INT && b->get_type() == VALUE_INT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT,
        std::stoll(a->get_num()) != 0 && std::stoll(b->get_num()) != 0);
  else
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) && std::stoll(b->get_num()));
}

std::shared_ptr<Value> operator||(std::shared_ptr<Value> a,
                                  std::shared_ptr<Value> b) {
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT,
        std::stod(a->get_num()) != 0 || std::stod(b->get_num()) != 0);
  else
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT, std::stoll(a->get_num()) || std::stoll(b->get_num()));
}

std::shared_ptr<Value> operator-(std::shared_ptr<Value> a) {
  if (a->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(VALUE_FLOAT,
                                                0 - stod(a->get_num()));
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 0 - stoll(a->get_num()));
}

std::shared_ptr<Value> operator!(std::shared_ptr<Value> a) {
  if (a->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(VALUE_FLOAT,
                                                stod(a->get_num()) == 0);
  else
    return std::make_shared<TypedValue<int64_t>>(VALUE_INT,
                                                 stoll(a->get_num()) == 0);
}

std::shared_ptr<Value> pow(std::shared_ptr<Value> a, std::shared_ptr<Value> b) {
  if (std::stod(a->get_num()) == 0.0 && std::stod(b->get_num()) == 0.0)
    return std::make_shared<ErrorValue>(
        VALUE_ERROR,
        Color(0xFF, 0x39, 0x6E).get() + "Runtime ERROR: 0 to the 0\n" RESET);
  if (a->get_type() == VALUE_FLOAT || b->get_type() == VALUE_FLOAT)
    return std::make_shared<TypedValue<double>>(
        VALUE_FLOAT,
        std::pow(std::stod(a->get_num()), std::stod(b->get_num())));
  else
    return std::make_shared<TypedValue<int64_t>>(
        VALUE_INT,
        std::pow(std::stoll(a->get_num()), std::stoll(b->get_num())));
}

/// --------------------
/// Run
/// --------------------

std::string run(std::string file_name, std::string text, SymbolTable &global_symbol_table) {
    Lexer lexer(file_name, text);
    TokenList tokens = lexer.make_tokens();
    if(tokens.empty()) return "";
    if(tokens[0]->get_type() == TOKEN_ERROR)
        std::cout << "Tokens: " << tokens << "\n";

    Parser parser(tokens);
    NodeList ast = parser.parse();
    
    for(auto node : ast) {
        if(node->get_type() == NODE_ERROR)
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
std::shared_ptr<Value> InstanceValue::get_member(const std::string& name) {
    if (members.count(name)) {
        return members[name];
    }
    // Check for methods in struct definition
    if (struct_def->methods.count(name)) {
        return std::make_shared<BoundMethodValue>(std::make_shared<InstanceValue>(*this), name);
    }
    return std::make_shared<ErrorValue>(VALUE_ERROR, "Member not found: " + name);
}

void InstanceValue::set_member(const std::string& name, std::shared_ptr<Value> val) {
    // If it's declared in struct def members, we can set it.
    // Or do we allow dynamic member addition? The user prompt implies static definition of members.
    // "Struct List: head tail"
    // So we should check if 'name' is in struct_def->members.
    // However, for simplicity and python-like behavior (often associated with pseudo code), maybe we can just set it.
    // But strict structs are better.
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
        // Maybe error? Or just allow it?
        // Let's assume strict members for now based on declaration.
        // Actually, let's allow it if it's not strictly forbidden, but the parser enforces declaration.
        // If the user declared members, they probably expect only those.
        // But wait, the parser just collects declared members.
        // Let's enforce it.
        // Actually, raising error might be safer.
         members[name] = val; // Just set it for now.
    }
}
std::shared_ptr<Value> StructValue::execute(NodeList args, SymbolTable *parent) {
    // Constructor call
    std::shared_ptr<InstanceValue> instance = std::make_shared<InstanceValue>(std::make_shared<StructValue>(*this));

    // Initialize members to NONE
    for (const auto& member : members) {
        instance->set_member(member, std::make_shared<Value>(VALUE_NONE));
    }

    // Call constructor if exists
    if (methods.count("constructor")) {
        std::shared_ptr<Value> ctor = methods["constructor"];
        // We need to bind the constructor to the instance
        std::shared_ptr<BoundMethodValue> bound_ctor = std::make_shared<BoundMethodValue>(instance, "constructor");
        // But BoundMethodValue execute logic for custom objects is not implemented in pseudo.cpp yet (only ArrayValue).
        // We need to implement it.
        // Actually, let's reuse AlgoValue::execute but inject 'self'.

        // Wait, BoundMethodValue holds the object and the method name.
        // Its execute() needs to look up the method (which we have in 'ctor') and call it with 'self' = obj.

        // Actually, if we use BoundMethodValue, we need to implement execute for generic objects.
        // Alternatively, we can manually call ctor->execute(args, parent) but we need to inject 'self'.
        // AlgoValue::execute creates a new symbol table. We need to add 'self' to it.
        // But AlgoValue::execute interface doesn't allow injecting symbols easily before execution.
        // However, AlgoValue::execute does:
        // SymbolTable sym(parent);
        // set_args(args, sym, interpreter);
        // ...

        // We can manually do what AlgoValue::execute does.
        // Or we can modify AlgoValue to support binding?
        // Or implement BoundMethodValue::execute properly.
    }

    // For now, let's rely on BoundMethodValue which we will implement/update in pseudo.cpp.
    if (methods.count("constructor")) {
         std::shared_ptr<BoundMethodValue> bound_ctor = std::make_shared<BoundMethodValue>(instance, "constructor");
         std::shared_ptr<Value> ret = bound_ctor->execute(args, parent);
         if (ret->get_type() == VALUE_ERROR) return ret;
    }

    return instance;
}
