#ifndef PSEUDO_H
#define PSEUDO_H

#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <map>
#include <set>
#include "position.h"
#include "token.h"
#include "value.h"
#include "node.h"
#include "parser.h"
#include "lexer.h"
#include "symboltable.h"
#include "interpreter.h"

/// --------------------
/// Run
/// --------------------

std::string run(std::string, std::string, SymbolTable&);

#endif