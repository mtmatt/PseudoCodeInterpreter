/// --------------------
/// Algorithm analysis
/// --------------------

#ifndef ANALYSIS_H
#define ANALYSIS_H

#include <memory>
#include <string>
#include <vector>

#include "node.h"

// True for recursive algorithms whose bodies only use numeric literals, their
// own arguments, numeric operators, ifs, and self-calls: their results can be
// memoized by argument values (used by both the interpreter and the compiled
// runtime).
bool is_memoizable_numeric_algo(const std::shared_ptr<Node>& node, const std::string& algo_name,
                                const std::vector<std::string>& args);

#endif
