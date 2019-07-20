#include "chesstest.hpp"

#include <iostream>
#include <string>

int main() {
    goldfish::ChessTest engine;
    std::string fen = engine.generate_fen(0.5);
    std::cout << fen << std::endl;
    engine.receive_position(fen);
    engine.receive_go(10);
    engine.receive_quit();
    std::cout << engine.receive_response() << std::endl;
}
