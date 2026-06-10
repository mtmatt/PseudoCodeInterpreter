/// --------------------
/// LLVM AOT compiler
/// --------------------

#ifndef COMPILER_H
#define COMPILER_H

#include <string>
#include <vector>

#include "node.h"

namespace llvm {
class Module;
}  // namespace llvm

class Compiler {
   public:
    // Lowers the parsed program into `module` (a `main` function plus one
    // function per Algorithm). Returns false and fills `errors` when the
    // program uses features the compiler does not support.
    static bool compile(const NodeList& ast, llvm::Module& module,
                        std::vector<std::string>& errors);
};

#endif
