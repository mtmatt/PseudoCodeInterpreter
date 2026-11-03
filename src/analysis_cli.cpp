#include "analysis_cli.h"

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "complexity.h"
#include "imports.h"
#include "interpreter.h"
#include "lexer.h"
#include "parser.h"
#include "profile.h"
#include "symboltable.h"

namespace {

struct Options {
    bool analyze{false};
    bool profile{false};
    std::string file;
    std::string input{"n"};
    std::vector<int64_t> sizes;
    uint64_t max_steps{1000000};
};

void print_usage() {
    std::cout
        << "Usage: pseudo [program.ps]\n"
           "       pseudo --analyze [--profile] program.ps [options]\n"
           "       pseudo --profile program.ps [options]\n\n"
           "  --analyze         Explain static time-complexity estimates without execution\n"
           "  --profile         Execute with AST counters and optimizations disabled\n"
           "  --sizes 100,200   Repeat with a fresh global integer n for each input size\n"
           "  --input name      Name of the injected size variable (default: n)\n"
           "  --max-steps N     Budget for AST evaluations + loop iterations (default: 1000000)\n"
           "  --help            Show this help\n\n"
           "Analysis reports go to stderr; program output goes to stdout.\n";
}

uint64_t parse_unsigned(const std::string& text, const std::string& option) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error(option + " requires an unsigned integer");
    }
    try {
        return std::stoull(text);
    } catch (const std::exception&) {
        throw std::runtime_error(option + " is out of range");
    }
}

Options parse_options(int argc, char* argv[]) {
    Options options;
    bool input_set = false;
    bool limit_set = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--analyze") {
            options.analyze = true;
        } else if (arg == "--profile") {
            options.profile = true;
        } else if (arg == "--sizes" || arg == "--input" || arg == "--max-steps") {
            if (++i == argc) throw std::runtime_error("Missing value for " + arg);
            std::string value = argv[i];
            if (arg == "--input") {
                Lexer lexer("--input", value);
                auto tokens = lexer.make_tokens();
                if (tokens.size() != 1 || tokens[0]->get_type() != TOKEN_IDENTIFIER ||
                    tokens[0]->get_value() != value) {
                    throw std::runtime_error("--input requires a non-keyword variable name");
                }
                options.input = value;
                input_set = true;
            } else if (arg == "--max-steps") {
                options.max_steps = parse_unsigned(value, arg);
                if (options.max_steps == 0)
                    throw std::runtime_error("--max-steps must be positive");
                limit_set = true;
            } else {
                if (!options.sizes.empty()) throw std::runtime_error("Specify --sizes only once");
                if (value.empty() || value.back() == ',') {
                    throw std::runtime_error("--sizes requires comma-separated integers");
                }
                std::istringstream stream(value);
                std::string item;
                while (std::getline(stream, item, ',')) {
                    uint64_t size = parse_unsigned(item, arg);
                    if (size > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                        throw std::runtime_error("--sizes is out of range");
                    }
                    options.sizes.push_back(static_cast<int64_t>(size));
                }
            }
        } else if (!arg.empty() && arg[0] == '-') {
            throw std::runtime_error("Unknown option: " + arg);
        } else if (options.file.empty()) {
            options.file = arg;
        } else {
            throw std::runtime_error("Specify exactly one .ps file");
        }
    }
    if (!options.analyze && !options.profile) {
        throw std::runtime_error("Specify --analyze or --profile");
    }
    if (options.file.empty()) throw std::runtime_error("Missing .ps file");
    if ((!options.sizes.empty() || input_set || limit_set) && !options.profile) {
        throw std::runtime_error("--sizes, --input and --max-steps require --profile");
    }
    if (input_set && options.sizes.empty()) {
        throw std::runtime_error("--input requires --sizes");
    }
    return options;
}

NodeList load_program(const std::string& file) {
    std::ifstream input(file);
    if (!input) throw std::runtime_error("Cannot open file: " + file);
    std::ostringstream source;
    source << input.rdbuf();
    if (input.bad()) throw std::runtime_error("Cannot read file: " + file);
    ImportState imports;
    std::string expanded;
    std::string error;
    if (!expand_imports(file, source.str(), imports, expanded, error)) {
        throw std::runtime_error(error);
    }
    Lexer lexer(file, expanded);
    auto tokens = lexer.make_tokens();
    if (tokens.empty()) return {};
    for (const auto& token : tokens) {
        if (token->get_type() == TOKEN_ERROR) {
            throw std::runtime_error("Lex error: " + token->get_value() + " (expanded line " +
                                     std::to_string(token->get_pos().line + 1) + ")");
        }
    }
    Parser parser(tokens);
    auto ast = parser.parse();
    for (const auto& node : ast) {
        if (node->get_type() == NODE_ERROR) {
            auto token = node->get_tok();
            throw std::runtime_error("Parse error: " + token->get_value() + " (expanded line " +
                                     std::to_string(token->get_pos().line + 1) + ")");
        }
    }
    return ast;
}

// Counters cover source evaluation. Timing starts after import expansion and parsing.
int profile_program(const NodeList& ast, const Options& options) {
    std::cerr
        << "Runtime profile (AST evaluations; JIT and memoization disabled)\n"
           "Native array/string/HashTable internals are excluded from operation counts.\n"
           "Elapsed time includes program I/O; ratios are observations, not Big-O proofs.\n"
        << "input\tast_evaluations\tloop_iterations\tcalls\tmax_depth\telapsed_ms\tcount_ratio\n";
    uint64_t previous_steps = 0;
    size_t runs = options.sizes.empty() ? 1 : options.sizes.size();
    for (size_t i = 0; i < runs; ++i) {
        SymbolTable symbols;
        if (!options.sizes.empty()) {
            symbols.set(options.input,
                        std::make_shared<TypedValue<int64_t>>(VALUE_INT, options.sizes[i]));
        }
        ProfileStats stats;
        std::string failure;
        auto start = std::chrono::steady_clock::now();
        try {
            ProfileSession session(stats, options.max_steps);
            Interpreter interpreter(symbols, false);
            for (const auto& node : ast) {
                auto result = interpreter.visit(node);
                if (result->get_type() == VALUE_ERROR) {
                    failure = result->get_num();
                    break;
                }
            }
            session.check_limits();
        } catch (const std::exception& error) {
            failure = error.what();
        }
        double elapsed_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                .count();
        std::cerr << (options.sizes.empty()
                          ? "program"
                          : options.input + "=" + std::to_string(options.sizes[i]))
                  << '\t' << stats.ast_evaluations << '\t' << stats.loop_iterations << '\t'
                  << stats.algorithm_calls << '\t' << stats.max_call_depth << '\t' << std::fixed
                  << std::setprecision(3) << elapsed_ms << '\t';
        if (i > 0 && previous_steps > 0) {
            std::cerr << static_cast<double>(stats.ast_evaluations) / previous_steps;
        } else {
            std::cerr << '-';
        }
        std::cerr << '\n';
        if (!failure.empty()) {
            std::cerr << "Profile stopped: " << failure << '\n';
            return 1;
        }
        previous_steps = stats.ast_evaluations;
    }
    return 0;
}

}  // namespace

int run_analysis_cli(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--help") {
        print_usage();
        return 0;
    }
    try {
        Options options = parse_options(argc, argv);
        NodeList ast = load_program(options.file);
        if (options.analyze) std::cerr << analyze_complexity(ast);
        if (options.profile) return profile_program(ast, options);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "pseudo: " << error.what() << '\n';
        return 1;
    }
}
