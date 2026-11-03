#ifndef COMPLEXITY_H
#define COMPLEXITY_H

#include <string>

#include "node.h"

// Estimates source-level time bounds without executing the parsed program.
// Unsupported control flow and calls are reported as unknown, with reasons.
std::string analyze_complexity(const NodeList& ast);

#endif
