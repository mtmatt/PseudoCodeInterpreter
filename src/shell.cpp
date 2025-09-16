#include <iostream>
#include <fstream>
#include <chrono>
#include "pseudo.h"
#include "color.h"

using time_point = std::chrono::steady_clock::time_point;

void run_shell(std::string file_name) {
    SymbolTable global_symbol_table;
    while(true) {
        std::cout << Color(0x34, 0xD3, 0xDE) << "Pseudo >> " RESET;
        std::string input;
        std::getline(std::cin, input);
        time_point start{std::chrono::steady_clock::now()};
        std::cout << run(file_name, input, global_symbol_table) << "\n";
        time_point end{std::chrono::steady_clock::now()};
        int64_t time_cost{std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count()};
        std::cout << "Execution time: " << time_cost << " ms\n";
    }
}

void run_code(std::string file_name, char *args[]) {
    std::ifstream input(file_name);
    std::string code, line;
    while(std::getline(input, line)) {
        code += line + "\n";
    }
    SymbolTable global_symbol_table;
    run(file_name, code, global_symbol_table);
}

int main(int argc, char *args[]) {
    if(argc == 1) {
        run_shell("stdin");
    } else {
        run_code(args[1], args + 1);
    }
    return 0;
}