#include "error.h"
#include <iostream>
#include <sstream>
#include <vector>

std::string error_marker(std::string text, Position pos_start, Position pos_end) {
    std::string result;
    std::stringstream ss(text);
    std::string line;
    int current_line = 0;

    // Find the start line of the error
    // Note: Position line is 0-indexed in the current implementation based on Position::advance
    // but usually displayed as 1-indexed. Let's check Position implementation.
    // Position starts at line 0.

    int idx_start = pos_start.index;
    int idx_end = pos_end.index;

    // We only need to find the line where the error occurred.
    // The example shows:
    // 30:    for i <- 1 to n:
    //        -----------^-

    while (std::getline(ss, line)) {
        if (current_line == pos_start.line) {
            result += std::to_string(current_line + 1) + ":    " + line + "\n";
            result += "     ";
            for(int i = 0; i < std::to_string(current_line + 1).length(); ++i) result += " ";

            // Calculate column position
            // pos_start.column is the column index
            for (int i = 0; i < pos_start.column; i++) {
                result += "-";
            }
            result += "^";
            // If we want to mark until pos_end, we can extend dashes.
            // But ErrorToken usually has just one position.
            result += "-";
            result += "\n";
            break;
        }
        current_line++;
    }
    return result;
}
