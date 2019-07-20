#include "chesstest.hpp"

#include "uci.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>

namespace goldfish
{

const std::vector<std::string> ChessTest::possiblestarts = {"rnbqkbnr/pppppppp/8/8/8/P7/1PPPPPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/1P6/P1PPPPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/2P5/PP1PPPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/3P4/PPP1PPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/4P3/PPPP1PPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/5P2/PPPPP1PP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/6P1/PPPPPP1P/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/7P/PPPPPPP1/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/P7/8/1PPPPPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/1P6/8/P1PPPPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/2P5/8/PP1PPPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/5P2/8/PPPPP1PP/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/6P1/8/PPPPPP1P/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/7P/8/PPPPPPP1/RNBQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/N7/PPPPPPPP/R1BQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/2N5/PPPPPPPP/R1BQKBNR w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R w KQkq - 0 1",
                                                            "rnbqkbnr/pppppppp/8/8/8/7N/PPPPPPPP/RNBQKB1R w KQkq - 0 1"};


std::string ChessTest::generate_frag(std::size_t length, const std::pair<std::vector<char>, std::vector<int> > &possible, const std::vector<int *> &amountptrs, std::mt19937 &gen) const {
    bool nopieces = false;
    if (std::all_of(amountptrs.begin(), amountptrs.end(), [](int *a){
        return !*a;
    }))
        nopieces = true;
    std::string fragment;
    int piecenum = possible.first.size();
    int phnum = possible.second.size();

    std::uniform_int_distribution<> dis(0, piecenum + phnum - 1);
    std::uniform_int_distribution<> disf(0, piecenum - 1);
    std::uniform_int_distribution<> diss(piecenum, piecenum + phnum - 1);

    if (possible.second.empty()) {
        std::size_t total_amts = 0;
        std::for_each(amountptrs.begin(), amountptrs.end(), [&](int *a){
            total_amts += *a; 
        });
        if (total_amts < 8)
            *amountptrs[0] += 8 - total_amts;
    }   

    int remaining = length;
    for (std::size_t j = 0; j < length && remaining > 0; j++) {
        int choice = nopieces ? diss(gen) : dis(gen);
        if (choice < piecenum) {
            while (!*amountptrs[choice])
                choice = disf(gen);
            fragment += possible.first[choice];
            --*amountptrs[choice];
            if (std::all_of(amountptrs.begin(), amountptrs.end(), [](int *a){
                return !*a;
            }))
                nopieces = true;
            remaining--;
        } else {
            int value = possible.second[choice - piecenum];
            while (remaining - value < 0) {
                value = possible.second[diss(gen) - piecenum];
            }
            if (std::isdigit(fragment.back()))
                fragment.back() += value;
            else
                fragment += std::to_string(value);
            remaining -= value;
        }
    }

    return fragment;
}

std::string ChessTest::generate_fen(double p) const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::bernoulli_distribution startdis(p);
    if (startdis(gen)) {
        std::uniform_int_distribution<> fendis(0, possiblestarts.size() - 1);
        return possiblestarts[fendis(gen)];
    } else {
        static std::bernoulli_distribution disb;

        const static std::pair<std::vector<char>, std::vector<int> > possible = {{'p', 'r', 'n', 'b', 'q', 'P', 'R', 'N', 'B', 'Q'}, {1, 2, 3, 4, 5, 6, 7, 8}};
        std::string fen;
        std::array<int, 10> amounts = {8, 2, 2, 2, 1, 8, 2, 2, 2, 1};
        std::vector<int *> aptrs;
        for (std::size_t i = 0; i < amounts.size(); i++)
            aptrs.push_back(amounts.data() + i);
        std::pair<std::vector<char>, std::vector<int> > firstpenult(possible);
        firstpenult.first.erase(firstpenult.first.begin() + 7);
        std::pair<std::vector<char>, std::vector<int> > lastpenult(possible);
        lastpenult.first.erase(lastpenult.first.begin() + 2);

        const static std::pair<std::vector<char>, std::vector<int> > guard = {{'p', 'r', 'n', 'b', 'q'}, {}};
        std::pair<std::vector<char>, std::vector<int> > lastguard(guard);
        std::transform(lastguard.first.begin(), lastguard.first.end(), lastguard.first.begin(), [](char c){
            return std::isupper(c) ? std::tolower(c) : std::toupper(c);
        });

        const static std::pair<std::vector<char>, std::vector<int> > firstfour = {{'p', 'r', 'n', 'b', 'q', 'P', 'N', 'B'}, {1, 2, 3, 4}};
        std::pair<std::vector<char>, std::vector<int> > firstthree(firstfour);
        firstthree.second.pop_back();
        std::vector<int *> afptrs = {amounts.data(), amounts.data() + 1, amounts.data() + 2, amounts.data() + 3, amounts.data() + 4, amounts.data() + 5, amounts.data() + 7, amounts.data() + 8};

        std::pair<std::vector<char>, std::vector<int> > lastfour(firstfour);
        std::transform(lastfour.first.begin(), lastfour.first.end(), lastfour.first.begin(), [](char c){
            return std::isupper(c) ? std::tolower(c) : std::toupper(c);
        });
        std::pair<std::vector<char>, std::vector<int> > lastthree(lastfour);
        lastthree.second.pop_back();
        std::vector<int *> asptrs = {amounts.data() + 5, amounts.data() + 6, amounts.data() + 7, amounts.data() + 8, amounts.data() + 9, amounts.data(), amounts.data() + 2, amounts.data() + 3};

        fen += generate_frag(4, firstfour, afptrs, gen) + "k" + generate_frag(3, firstthree, afptrs, gen) + "/"
             + generate_frag(8, guard, {amounts.begin(), amounts.begin() + 1, amounts.begin() + 2, amounts.begin() + 3, amounts.begin() + 4}, gen) + "/"
             + generate_frag(8, firstpenult, {amounts.begin(), amounts.begin() + 1, amounts.begin() + 2, amounts.begin() + 3, amounts.begin() + 4, amounts.begin() + 5, amounts.begin() + 6, amounts.begin() + 8, amounts.begin() + 9}, gen) + "/";

        for (std::size_t i = 0; i < 2; i++)
            fen += generate_frag(8, possible, aptrs, gen) + "/";

        fen += generate_frag(8, lastpenult, {amounts.begin(), amounts.begin() + 1, amounts.begin() + 3, amounts.begin() + 4, amounts.begin() + 5, amounts.begin() + 6, amounts.begin() + 7, amounts.begin() + 8, amounts.begin() + 9}, gen) + "/"
             + generate_frag(8, lastguard, {amounts.begin() + 5, amounts.begin() + 6, amounts.begin() + 7, amounts.begin() + 8, amounts.begin() + 9}, gen) + "/"
             + generate_frag(4, lastfour, asptrs, gen) + "K" + generate_frag(3, lastthree, asptrs, gen);

        fen += (disb(gen) ? " w" : " b");
        fen += " - - 0 1";

        return fen;
    }
}

std::string ChessTest::receive_response() const {
    return response;
}

void ChessTest::receive_quit(bool wait) {
    if (wait)
        search.wait_for_finished();
    search.quit();
}

void ChessTest::receive_position(const std::string &input) {
    search.stop();

    current_position = Notation::to_position(input);
}

void ChessTest::receive_go(std::size_t depth) {
    search.stop();
    response.clear();

    search.new_depth_search(current_position, Depth(depth));

    // Go...
    search.start();
    start_time        = std::chrono::system_clock::now();
    status_start_time = start_time;
}

void ChessTest::send_best_move(Move best_move, Move ponder_move) {
    response += "bestmove ";

    if (best_move != Move::NO_MOVE) {
        response += Notation::from_move(best_move);
        if (ponder_move != Move::NO_MOVE)
            response += " ponder " + Notation::from_move(ponder_move);
    } else
        response += "NO_MOVE";
}

void ChessTest::send_status(int      current_depth,
                           int      current_max_depth,
                           uint64_t total_nodes,
                           uint64_t tb_hits,
                           Move     current_move,
                           int      current_move_number) {
    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - status_start_time).count() >= 1000)
        send_status(false,
                    current_depth,
                    current_max_depth,
                    total_nodes,
                    tb_hits,
                    current_move,
                    current_move_number);
}

void ChessTest::send_status(bool     force,
                           int      current_depth,
                           int      current_max_depth,
                           uint64_t total_nodes,
                           uint64_t tb_hits,
                           Move     current_move,
                           int      current_move_number) {
    auto time_delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start_time);

    if (force || time_delta.count() >= 1000) {
        std::cout << "info";
        std::cout << " depth " << current_depth;
        std::cout << " seldepth " << current_max_depth;
        std::cout << " nodes " << total_nodes;
        std::cout << " time " << time_delta.count();
        std::cout << " nps " << (time_delta.count() >= 1000 ? (total_nodes * 1000) / time_delta.count() : 0);
        std::cout << " tbhits " << tb_hits;

        if (current_move != Move::NO_MOVE) {
            std::cout << " currmove " << Notation::from_move(current_move);
            std::cout << " currmovenumber " << current_move_number;
        }

        std::cout << std::endl;

        status_start_time = std::chrono::system_clock::now();
    }
}

void ChessTest::send_move(const RootEntry& entry,
                         int              current_depth,
                         int              current_max_depth,
                         uint64_t         total_nodes,
                         uint64_t         tb_hits) {
    auto time_delta = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now() - start_time);

    std::cout << "info";
    std::cout << " depth " << current_depth;
    std::cout << " seldepth " << current_max_depth;
    std::cout << " nodes " << total_nodes;
    std::cout << " time " << time_delta.count();
    std::cout << " nps " << (time_delta.count() >= 1000 ? (total_nodes * 1000) / time_delta.count() : 0);
    std::cout << " tbhits " << tb_hits;

    if (std::abs(entry.value) >= Value::CHECKMATE_THRESHOLD) {
        // Calculate mate distance
        int mate_depth = Value::CHECKMATE - std::abs(entry.value);
        std::cout << " score mate " << ((entry.value > 0) - (entry.value < 0)) * (mate_depth + 1) / 2;
    } else
        std::cout << " score cp " << entry.value;

    if (entry.pv.size > 0) {
        std::cout << " pv";
        for (int i = 0; i < entry.pv.size; i++)
            std::cout << " " << Notation::from_move(entry.pv.moves[i]);
    }

    std::cout << std::endl;

    status_start_time = std::chrono::system_clock::now();
}

}  // namespace goldfish
