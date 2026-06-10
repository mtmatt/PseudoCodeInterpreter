/// --------------------
/// Import expansion
/// --------------------

#ifndef IMPORTS_H
#define IMPORTS_H

#include <set>
#include <string>

struct ImportState {
    std::set<std::string> loaded;
    std::set<std::string> loading;
};

// Recursively replaces `import` lines in `text` with the imported file
// contents. Returns false and fills `error` on unresolved, unreadable, or
// circular imports.
bool expand_imports(const std::string& file_name, const std::string& text, ImportState& state,
                    std::string& expanded, std::string& error);

#endif
