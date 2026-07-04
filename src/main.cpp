#include "uci.h"
#include "attacks.h"
#include "search.h"
#include "tt.h"
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::ios_base::sync_with_stdio(false);

    Attacks::init();
    TT::init(64);
    Search::init();

    if (argc > 1 && std::string(argv[1]) == "bench") {
        int depth = 12;
        if (argc > 2) depth = std::stoi(argv[2]);
        UCI::bench(depth);
        return 0;
    }

    UCI::loop();
    return 0;
}
