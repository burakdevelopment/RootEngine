#pragma once
#include "position.h"
#include "move.h"
#include <string>

namespace Book {

    Move probe(Position& pos, const std::string& book_path);
}
