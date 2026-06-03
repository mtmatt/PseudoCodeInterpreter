#include "lexer.h"
#include "node.h"
#include "parser.h"
#include "token.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

struct Json {
    using Object = std::map<std::string, Json>;
    using Array = std::vector<Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value;

    Json() : value(nullptr) {}
    Json(std::nullptr_t) : value(nullptr) {}
    Json(bool v) : value(v) {}
    Json(double v) : value(v) {}
    Json(int v) : value(static_cast<double>(v)) {}
    Json(const char* v) : value(std::string(v)) {}
    Json(std::string v) : value(std::move(v)) {}
    Json(Array v) : value(std::move(v)) {}
    Json(Object v) : value(std::move(v)) {}

    const Object* object() const { return std::get_if<Object>(&value); }
    const Array* array() const { return std::get_if<Array>(&value); }
    const std::string* string() const { return std::get_if<std::string>(&value); }
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value); }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& input) : input(input) {}

    Json parse() {
        skip_ws();
        return parse_value();
    }

private:
    Json parse_value() {
        skip_ws();
        if (pos >= input.size()) return nullptr;
        char ch = input[pos];
        if (ch == '"') return parse_string();
        if (ch == '{') return parse_object();
        if (ch == '[') return parse_array();
        if (ch == 't') {
            pos += 4;
            return true;
        }
        if (ch == 'f') {
            pos += 5;
            return false;
        }
        if (ch == 'n') {
            pos += 4;
            return nullptr;
        }
        return parse_number();
    }

    Json parse_object() {
        Json::Object object;
        ++pos;
        skip_ws();
        while (pos < input.size() && input[pos] != '}') {
            Json key_json = parse_string();
            std::string key = key_json.string() ? *key_json.string() : "";
            skip_ws();
            if (pos < input.size() && input[pos] == ':') ++pos;
            object[key] = parse_value();
            skip_ws();
            if (pos < input.size() && input[pos] == ',') {
                ++pos;
                skip_ws();
            }
        }
        if (pos < input.size() && input[pos] == '}') ++pos;
        return object;
    }

    Json parse_array() {
        Json::Array array;
        ++pos;
        skip_ws();
        while (pos < input.size() && input[pos] != ']') {
            array.push_back(parse_value());
            skip_ws();
            if (pos < input.size() && input[pos] == ',') {
                ++pos;
                skip_ws();
            }
        }
        if (pos < input.size() && input[pos] == ']') ++pos;
        return array;
    }

    Json parse_string() {
        std::string out;
        if (pos < input.size() && input[pos] == '"') ++pos;
        while (pos < input.size()) {
            char ch = input[pos++];
            if (ch == '"') break;
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (pos >= input.size()) break;
            char escaped = input[pos++];
            switch (escaped) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                    if (pos + 4 <= input.size()) pos += 4;
                    out += '?';
                    break;
                default: out += escaped; break;
            }
        }
        return out;
    }

    Json parse_number() {
        size_t start = pos;
        if (pos < input.size() && input[pos] == '-') ++pos;
        while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        if (pos < input.size() && input[pos] == '.') {
            ++pos;
            while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos]))) ++pos;
        }
        return std::stod(input.substr(start, pos - start));
    }

    void skip_ws() {
        while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos]))) ++pos;
    }

    const std::string& input;
    size_t pos{0};
};

const Json* get(const Json& json, const std::string& key) {
    const auto* object = json.object();
    if (!object) return nullptr;
    auto it = object->find(key);
    return it == object->end() ? nullptr : &it->second;
}

const Json* at_path(const Json& json, std::initializer_list<const char*> path) {
    const Json* current = &json;
    for (const char* key : path) {
        current = get(*current, key);
        if (!current) return nullptr;
    }
    return current;
}

std::string json_escape(const std::string& text) {
    std::string out;
    for (char ch : text) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out += "\\u00";
                    const char* hex = "0123456789abcdef";
                    auto byte = static_cast<unsigned char>(ch);
                    out += hex[(byte >> 4) & 0xf];
                    out += hex[byte & 0xf];
                } else {
                    out += ch;
                }
        }
    }
    return out;
}

std::string stringify(const Json& json) {
    if (json.is_null()) return "null";
    if (const auto* b = std::get_if<bool>(&json.value)) return *b ? "true" : "false";
    if (const auto* n = std::get_if<double>(&json.value)) {
        std::ostringstream out;
        out << *n;
        return out.str();
    }
    if (const auto* s = json.string()) return "\"" + json_escape(*s) + "\"";
    if (const auto* array = json.array()) {
        std::string out = "[";
        for (size_t i = 0; i < array->size(); ++i) {
            if (i) out += ",";
            out += stringify((*array)[i]);
        }
        out += "]";
        return out;
    }
    const auto* object = json.object();
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : *object) {
        if (!first) out += ",";
        first = false;
        out += "\"" + json_escape(key) + "\":" + stringify(value);
    }
    out += "}";
    return out;
}

void send_json(const std::string& payload) {
    std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
    std::cout.flush();
}

void send_response(const Json& id, const std::string& result) {
    send_json("{\"jsonrpc\":\"2.0\",\"id\":" + stringify(id) + ",\"result\":" + result + "}");
}

void send_error(const Json& id, int code, const std::string& message) {
    send_json("{\"jsonrpc\":\"2.0\",\"id\":" + stringify(id) + ",\"error\":{\"code\":" +
              std::to_string(code) + ",\"message\":\"" + json_escape(message) + "\"}}");
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines{""};
    for (char ch : text) {
        if (ch == '\n') {
            lines.push_back("");
        } else {
            lines.back() += ch;
        }
    }
    return lines;
}

int line_length(const std::vector<std::string>& lines, int line) {
    if (line < 0 || line >= static_cast<int>(lines.size())) return 0;
    return static_cast<int>(lines[line].size());
}

std::string diagnostic_json(const std::string& message, const Position& pos, const std::vector<std::string>& lines) {
    int line = std::max(0, pos.line);
    int character = std::max(0, pos.column);
    int end_character = std::min(std::max(character + 1, 1), std::max(line_length(lines, line), character + 1));
    return "{\"range\":{\"start\":{\"line\":" + std::to_string(line) +
           ",\"character\":" + std::to_string(character) + "},\"end\":{\"line\":" +
           std::to_string(line) + ",\"character\":" + std::to_string(end_character) +
           "}},\"severity\":1,\"source\":\"pseudo-lsp\",\"message\":\"" + json_escape(message) + "\"}";
}

std::string strip_line_comments(const std::string& text) {
    std::string sanitized = text;
    bool in_string = false;
    bool escaped = false;
    for (size_t i = 0; i + 1 < sanitized.size(); ++i) {
        char ch = sanitized[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '/' && sanitized[i + 1] == '/') {
            while (i < sanitized.size() && sanitized[i] != '\n') {
                sanitized[i++] = ' ';
            }
        }
    }
    return sanitized;
}

std::string diagnostics_for(const std::string& uri, const std::string& text) {
    std::vector<std::string> lines = split_lines(text);
    std::vector<std::string> diagnostics;
    std::string parse_text = strip_line_comments(text);

    Lexer lexer(uri, parse_text);
    TokenList tokens = lexer.make_tokens();
    for (const auto& token : tokens) {
        if (token->get_type() == TOKEN_ERROR) {
            diagnostics.push_back(diagnostic_json(token->get_value(), token->get_pos(), lines));
        }
    }

    if (diagnostics.empty() && !tokens.empty()) {
        Parser parser(tokens);
        NodeList ast = parser.parse();
        for (const auto& node : ast) {
            if (node->get_type() != NODE_ERROR) continue;
            std::shared_ptr<Token> token = node->get_tok();
            if (token) {
                diagnostics.push_back(diagnostic_json(token->get_value(), token->get_pos(), lines));
            } else {
                Position pos(0, 0, 0, uri);
                diagnostics.push_back(diagnostic_json(node->get_node(), pos, lines));
            }
            break;
        }
    }

    std::string out = "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" +
                      json_escape(uri) + "\",\"diagnostics\":[";
    for (size_t i = 0; i < diagnostics.size(); ++i) {
        if (i) out += ",";
        out += diagnostics[i];
    }
    out += "]}}";
    return out;
}

std::string completion_items() {
    struct Item {
        std::string label;
        int kind;
        std::string detail;
        std::string insert_text;
    };
    const std::vector<Item> items{
        {"Algorithm", 14, "Define an algorithm", "Algorithm ${1:name}(${2:args}):\n    ${0}"},
        {"Struct", 14, "Define a struct", "Struct ${1:Name}:\n    ${0}"},
        {"if", 14, "Conditional block", "if ${1:condition} then\n    ${0}"},
        {"else", 14, "Else branch", "else\n    ${0}"},
        {"for", 14, "For loop", "for ${1:i} <- ${2:1} to ${3:10} do\n    ${0}"},
        {"while", 14, "While loop", "while ${1:condition} do\n    ${0}"},
        {"repeat", 14, "Repeat loop", "repeat\n    ${1}\nuntil ${0:condition}"},
        {"return", 14, "Return from algorithm", "return ${0:value}"},
        {"break", 14, "Exit the nearest loop", "break"},
        {"continue", 14, "Skip to the next loop iteration", "continue"},
        {"true", 12, "Builtin constant", "true"},
        {"false", 12, "Builtin constant", "false"},
        {"none", 12, "Builtin constant", "none"},
        {"print", 3, "Builtin function", "print(${0:value})"},
        {"print()", 3, "Builtin function", "print(${0:value})"},
        {"read", 3, "Builtin function", "read()"},
        {"read_line", 3, "Builtin function", "read_line()"},
        {"clear", 3, "Builtin function", "clear()"},
        {"quit", 3, "Builtin function", "quit()"},
        {"int", 3, "Type conversion", "int(${0:value})"},
        {"float", 3, "Type conversion", "float(${0:value})"},
        {"string", 3, "Type conversion", "string(${0:value})"},
        {"import", 14, "Import a pseudocode library", "import ${0:dsa}"},
        {"import dsa", 9, "Import the DSA standard library", "import dsa"},
        {"LinkedList", 7, "Linked list constructor", "LinkedList()"},
        {"Stack", 7, "Stack constructor", "Stack()"},
        {"Queue", 7, "Queue constructor", "Queue()"},
        {"Tree", 7, "Tree constructor", "Tree()"},
        {"RBTree", 7, "Red-black tree constructor", "RBTree()"},
        {"BTree", 7, "B-tree constructor", "BTree(${0:order})"},
        {"DSU", 7, "Disjoint-set union constructor", "DSU()"},
        {"append", 2, "LinkedList.append(value)", "append(${0:value})"},
        {"prepend", 2, "LinkedList.prepend(value)", "prepend(${0:value})"},
        {"pop_front", 2, "LinkedList.pop_front()", "pop_front()"},
        {"get", 2, "LinkedList.get(index)", "get(${0:index})"},
        {"set", 2, "LinkedList.set(index, value)", "set(${1:index}, ${0:value})"},
        {"contains", 2, "Collection contains(value)", "contains(${0:value})"},
        {"is_empty", 2, "Collection is_empty()", "is_empty()"},
        {"push", 2, "Stack or array push(value)", "push(${0:value})"},
        {"pop", 2, "Stack or array pop()", "pop()"},
        {"peek", 2, "Stack.peek()", "peek()"},
        {"enqueue", 2, "Queue.enqueue(value)", "enqueue(${0:value})"},
        {"dequeue", 2, "Queue.dequeue()", "dequeue()"},
        {"front", 2, "Queue.front()", "front()"},
        {"insert", 2, "Tree insert(value)", "insert(${0:value})"},
        {"insert(index, value)", 2, "Array insert(index, value)", "insert(${1:index}, ${0:value})"},
        {"remove", 2, "Array remove(index)", "remove(${0:index})"},
        {"min", 2, "Tree min()", "min()"},
        {"max", 2, "Tree max()", "max()"},
        {"height", 2, "BTree.height()", "height()"},
        {"root_color", 2, "RBTree.root_color()", "root_color()"},
        {"make_set", 2, "DSU.make_set(value)", "make_set(${0:value})"},
        {"merge", 2, "DSU.merge(a, b)", "merge(${1:a}, ${0:b})"},
        {"connected", 2, "DSU.connected(a, b)", "connected(${1:a}, ${0:b})"},
        {"find", 2, "DSU.find(value)", "find(${0:value})"},
        {"size", 2, "Collection or array size()", "size()"},
        {"resize", 2, "Array resize(size)", "resize(${0:size})"},
        {"back", 2, "Array back()", "back()"},
    };

    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i) out += ",";
        out += "{\"label\":\"" + json_escape(items[i].label) + "\",\"kind\":" +
               std::to_string(items[i].kind) + ",\"detail\":\"" + json_escape(items[i].detail) +
               "\",\"insertTextFormat\":2,\"insertText\":\"" + json_escape(items[i].insert_text) + "\"}";
    }
    out += "]";
    return out;
}

std::optional<std::string> read_message() {
    std::string line;
    size_t content_length = 0;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        const std::string prefix = "Content-Length:";
        if (line.rfind(prefix, 0) == 0) {
            content_length = static_cast<size_t>(std::stoul(line.substr(prefix.size())));
        }
    }
    if (content_length == 0 || std::cin.eof()) return std::nullopt;
    std::string content(content_length, '\0');
    std::cin.read(content.data(), static_cast<std::streamsize>(content_length));
    if (std::cin.gcount() != static_cast<std::streamsize>(content_length)) return std::nullopt;
    return content;
}

std::string word_at(const std::string& text, int target_line, int target_character) {
    std::vector<std::string> lines = split_lines(text);
    if (target_line < 0 || target_line >= static_cast<int>(lines.size())) return "";
    const std::string& line = lines[target_line];
    int character = std::clamp(target_character, 0, static_cast<int>(line.size()));
    int start = character;
    while (start > 0 && (std::isalnum(static_cast<unsigned char>(line[start - 1])) || line[start - 1] == '_')) --start;
    int end = character;
    while (end < static_cast<int>(line.size()) && (std::isalnum(static_cast<unsigned char>(line[end])) || line[end] == '_')) ++end;
    return line.substr(start, end - start);
}

std::optional<std::string> hover_for(const std::string& word) {
    static const std::map<std::string, std::string> docs{
        {"Algorithm", "Defines a reusable algorithm. Example: `Algorithm add(a, b):`."},
        {"Struct", "Defines a struct with members and methods."},
        {"if", "Starts a conditional expression. Use `then` before the body."},
        {"for", "Iterates from a start value to an end value: `for i <- 1 to 10 do`."},
        {"while", "Runs a block while the condition is true."},
        {"repeat", "Runs a block until the trailing condition becomes true."},
        {"print", "Builtin function that writes values to stdout."},
        {"read", "Builtin function that reads one whitespace-separated token."},
        {"read_line", "Builtin function that reads a full input line."},
        {"int", "Converts a value to an integer."},
        {"float", "Converts a value to a float."},
        {"string", "Converts a value to a string."},
        {"LinkedList", "Standard library linked list. Methods: `append`, `prepend`, `pop_front`, `get`, `set`, `contains`, `size`, `is_empty`."},
        {"Stack", "Standard library stack. Methods: `push`, `pop`, `peek`, `size`, `is_empty`."},
        {"Queue", "Standard library queue. Methods: `enqueue`, `dequeue`, `front`, `size`, `is_empty`."},
        {"Tree", "Standard library sorted tree. Methods: `insert`, `contains`, `min`, `max`, `size`, `is_empty`."},
        {"break", "Exits the nearest `for`, `while`, or `repeat` loop."},
        {"continue", "Skips to the next iteration of the nearest loop."},
        {"RBTree", "Red-black sorted-set API. Methods: `insert`, `contains`, `min`, `max`, `size`, `is_empty`, `root_color`."},
        {"BTree", "Minimum-degree B-tree API. Methods: `insert`, `contains`, `min`, `max`, `size`, `is_empty`, `height`."},
        {"DSU", "Disjoint-set union with path compression and union by rank. Methods: `make_set`, `find`, `merge`, `connected`, `size`."},
        {"push", "Adds a value to an array or stack."},
        {"pop", "Removes and returns the last array item or top stack item."},
        {"insert", "Inserts into a tree, or inserts an array value at a 1-based index."},
        {"remove", "Removes and returns an array value at a 1-based index."},
        {"resize", "Resizes an array, filling new slots with `none`."},
        {"enqueue", "Adds a value to the back of a queue."},
        {"dequeue", "Removes and returns the front queue value."},
    };
    auto it = docs.find(word);
    if (it == docs.end()) return std::nullopt;
    return it->second;
}

} // namespace

int main() {
    std::map<std::string, std::string> documents;
    bool running = true;

    while (running) {
        auto content = read_message();
        if (!content) break;

        Json message;
        try {
            message = JsonParser(*content).parse();
        } catch (const std::exception& ex) {
            std::cerr << "pseudo-lsp: failed to parse JSON-RPC message: " << ex.what() << "\n";
            continue;
        }

        const Json* id = get(message, "id");
        const Json* method_json = get(message, "method");
        const std::string method = method_json && method_json->string() ? *method_json->string() : "";

        if (method == "initialize") {
            const std::string result =
                "{\"capabilities\":{\"textDocumentSync\":1,\"completionProvider\":{\"triggerCharacters\":[\".\",\"(\"]},"
                "\"hoverProvider\":true}}";
            if (id) send_response(*id, result);
        } else if (method == "initialized") {
            continue;
        } else if (method == "shutdown") {
            if (id) send_response(*id, "null");
        } else if (method == "exit") {
            running = false;
        } else if (method == "textDocument/didOpen") {
            const Json* uri = at_path(message, {"params", "textDocument", "uri"});
            const Json* text = at_path(message, {"params", "textDocument", "text"});
            if (uri && text && uri->string() && text->string()) {
                documents[*uri->string()] = *text->string();
                send_json(diagnostics_for(*uri->string(), *text->string()));
            }
        } else if (method == "textDocument/didChange") {
            const Json* uri = at_path(message, {"params", "textDocument", "uri"});
            const Json* changes = at_path(message, {"params", "contentChanges"});
            if (uri && uri->string() && changes && changes->array() && !changes->array()->empty()) {
                const Json* text = get(changes->array()->front(), "text");
                if (text && text->string()) {
                    documents[*uri->string()] = *text->string();
                    send_json(diagnostics_for(*uri->string(), *text->string()));
                }
            }
        } else if (method == "textDocument/didClose") {
            const Json* uri = at_path(message, {"params", "textDocument", "uri"});
            if (uri && uri->string()) {
                documents.erase(*uri->string());
                send_json("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"" +
                          json_escape(*uri->string()) + "\",\"diagnostics\":[]}}");
            }
        } else if (method == "textDocument/completion") {
            if (id) send_response(*id, completion_items());
        } else if (method == "textDocument/hover") {
            const Json* uri = at_path(message, {"params", "textDocument", "uri"});
            const Json* line = at_path(message, {"params", "position", "line"});
            const Json* character = at_path(message, {"params", "position", "character"});
            const auto* line_num = line ? std::get_if<double>(&line->value) : nullptr;
            const auto* char_num = character ? std::get_if<double>(&character->value) : nullptr;
            std::string result = "null";
            if (uri && uri->string() && line_num && char_num && documents.count(*uri->string())) {
                std::string word = word_at(documents[*uri->string()], static_cast<int>(*line_num), static_cast<int>(*char_num));
                if (auto hover = hover_for(word)) {
                    result = "{\"contents\":{\"kind\":\"markdown\",\"value\":\"" + json_escape(*hover) + "\"}}";
                }
            }
            if (id) send_response(*id, result);
        } else if (id) {
            send_error(*id, -32601, "Method not implemented: " + method);
        }
    }

    return 0;
}
