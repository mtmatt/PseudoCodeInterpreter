#ifndef ERROR_H
#define ERROR_H

#include <string>
#include "position.h"

std::string error_marker(std::string text, Position pos_start, Position pos_end);

#endif