/// --------------------
/// pseudoc driver
/// --------------------
///
/// Compiles a .ps source file to a native executable:
///   pseudoc <file.ps> [-o <out>] [--emit-llvm] [--runtime-lib <path>]

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "compiler.h"
#include "error.h"
#include "imports.h"
#include "lexer.h"
#include "node.h"
#include "parser.h"
#include "token.h"

namespace {

namespace fs = std::filesystem;

void usage() {
    std::cout << "usage: pseudoc <file.ps> [-o <output>] [--emit-llvm] "
                 "[--runtime-lib <path>]\n";
}

std::string find_runtime_lib(const std::string& flag_value, const char* argv0) {
    std::vector<fs::path> candidates;
    if (!flag_value.empty()) {
        candidates.emplace_back(flag_value);
    }
    if (const char* env = std::getenv("PSEUDO_RT_LIB")) {
        candidates.emplace_back(env);
    }
    std::error_code ec;
    fs::path exe = fs::weakly_canonical(fs::path(argv0), ec);
    if (!ec && exe.has_parent_path()) {
        candidates.push_back(exe.parent_path() / "build" / "libpseudort.a");
    }
    candidates.push_back(fs::current_path() / "build" / "libpseudort.a");

    for (const auto& candidate : candidates) {
        if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
            return candidate.string();
        }
    }
    return "";
}

std::string shell_quote(const std::string& text) {
    std::string quoted = "'";
    for (char c : text) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

}  // namespace

int main(int argc, char** argv) {
    std::string input_path, output_path, runtime_lib_flag;
    bool emit_llvm = false;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-o" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--emit-llvm") {
            emit_llvm = true;
        } else if (arg == "--runtime-lib" && i + 1 < argc) {
            runtime_lib_flag = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            usage();
            return 0;
        } else if (!arg.empty() && arg[0] == '-') {
            std::cout << "unknown option: " << arg << "\n";
            usage();
            return 1;
        } else if (input_path.empty()) {
            input_path = arg;
        } else {
            usage();
            return 1;
        }
    }
    if (input_path.empty()) {
        usage();
        return 1;
    }
    if (output_path.empty()) {
        output_path = fs::path(input_path).stem().string();
        if (output_path.empty()) {
            output_path = "a.out";
        }
    }

    std::ifstream input(input_path);
    if (!input) {
        std::cout << "cannot open " << input_path << "\n";
        return 1;
    }
    std::string code, line;
    while (std::getline(input, line)) {
        code += line + "\n";
    }

    ImportState import_state;
    std::string expanded_text, import_error;
    if (!expand_imports(input_path, code, import_state, expanded_text, import_error)) {
        std::cout << import_error << "\n";
        return 1;
    }

    Lexer lexer(input_path, expanded_text);
    TokenList tokens = lexer.make_tokens();
    for (const auto& tok : tokens) {
        if (tok->get_type() == TOKEN_ERROR) {
            std::cout << "Error: " << tok->get_value() << ", line " << tok->get_pos().line + 1
                      << ", column: " << tok->get_pos().column << "\n";
            std::cout << error_marker(expanded_text, tok->get_pos(), tok->get_pos());
            return 1;
        }
    }

    Parser parser(tokens);
    NodeList ast = parser.parse();
    for (const auto& node : ast) {
        if (node->get_type() == NODE_ERROR) {
            std::shared_ptr<Token> err_tok = node->get_tok();
            if (err_tok) {
                std::cout << "Error: " << err_tok->get_value() << ", line "
                          << err_tok->get_pos().line + 1
                          << ", column: " << err_tok->get_pos().column << "\n";
                std::cout << error_marker(expanded_text, err_tok->get_pos(), err_tok->get_pos());
            } else {
                std::cout << "Error: " << node->get_node() << "\n";
            }
            return 1;
        }
    }

    llvm::LLVMContext context;
    llvm::Module module(input_path, context);
    std::vector<std::string> compile_errors;
    if (!Compiler::compile(ast, module, compile_errors)) {
        for (const auto& message : compile_errors) {
            std::cout << message << "\n";
        }
        return 1;
    }

    if (emit_llvm) {
        module.print(llvm::outs(), nullptr);
        return 0;
    }

    if (llvm::verifyModule(module, &llvm::errs())) {
        std::cout << "internal error: generated module is invalid\n";
        return 1;
    }

    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    std::string triple_name = llvm::sys::getDefaultTargetTriple();
    std::string lookup_error;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple_name, lookup_error);
    if (target == nullptr) {
        std::cout << "target lookup failed: " << lookup_error << "\n";
        return 1;
    }
    llvm::TargetOptions target_options;
    llvm::TargetMachine* machine = target->createTargetMachine(
        llvm::Triple(triple_name), "generic", "", target_options, llvm::Reloc::PIC_);
    module.setDataLayout(machine->createDataLayout());
    module.setTargetTriple(llvm::Triple(triple_name));

    // Standard O2 pipeline.
    llvm::LoopAnalysisManager lam;
    llvm::FunctionAnalysisManager fam;
    llvm::CGSCCAnalysisManager cgam;
    llvm::ModuleAnalysisManager mam;
    llvm::PassBuilder pass_builder(machine);
    pass_builder.registerModuleAnalyses(mam);
    pass_builder.registerCGSCCAnalyses(cgam);
    pass_builder.registerFunctionAnalyses(fam);
    pass_builder.registerLoopAnalyses(lam);
    pass_builder.crossRegisterProxies(lam, fam, cgam, mam);
    llvm::ModulePassManager mpm =
        pass_builder.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
    mpm.run(module, mam);

    fs::path object_path =
        fs::temp_directory_path() / ("pseudoc-" + std::to_string(::getpid()) + ".o");
    {
        std::error_code ec;
        llvm::raw_fd_ostream object_stream(object_path.string(), ec, llvm::sys::fs::OF_None);
        if (ec) {
            std::cout << "cannot write " << object_path.string() << ": " << ec.message() << "\n";
            return 1;
        }
        llvm::legacy::PassManager emit_pm;
        if (machine->addPassesToEmitFile(emit_pm, object_stream, nullptr,
                                         llvm::CodeGenFileType::ObjectFile)) {
            std::cout << "target cannot emit object files\n";
            return 1;
        }
        emit_pm.run(module);
    }

    std::string runtime_lib = find_runtime_lib(runtime_lib_flag, argv[0]);
    if (runtime_lib.empty()) {
        std::cout << "cannot find libpseudort.a (build it with `make runtime`, "
                     "or pass --runtime-lib)\n";
        fs::remove(object_path);
        return 1;
    }

    std::string linker = "c++";
    if (const char* env = std::getenv("PSEUDO_LD")) {
        linker = env;
    }
    std::string command = linker + " " + shell_quote(object_path.string()) + " " +
                          shell_quote(runtime_lib) + " -o " + shell_quote(output_path);
    int status = std::system(command.c_str());
    fs::remove(object_path);
    if (status != 0) {
        std::cout << "link failed: " << command << "\n";
        return 1;
    }
    return 0;
}
