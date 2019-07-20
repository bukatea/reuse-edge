#pragma once

#include "notation.hpp"
#include "protocol.hpp"
#include "search.hpp"

namespace goldfish
{
class ChessTest : public Protocol
{
public:
    ChessTest() : search(*this) {}

    std::string generate_fen(double p) const;

    std::string receive_response() const;

    void receive_quit(bool wait = false);

    void receive_position(const std::string &input);

    void receive_go(std::size_t depth);

    void send_best_move(Move best_move, Move ponder_move) final;

    void send_status(int      current_depth,
                     int      current_max_depth,
                     uint64_t total_nodes,
                     uint64_t tb_hits,
                     Move     current_move,
                     int      current_move_number) final;

    void send_status(bool     force,
                     int      current_depth,
                     int      current_max_depth,
                     uint64_t total_nodes,
                     uint64_t tb_hits,
                     Move     current_move,
                     int      current_move_number) final;

    void send_move(const RootEntry& entry,
                   int              current_depth,
                   int              current_max_depth,
                   uint64_t         total_nodes,
                   uint64_t         tb_hits) final;

    const static std::vector<std::string> possiblestarts;

private:
    std::chrono::system_clock::time_point start_time;
    std::chrono::system_clock::time_point status_start_time;

    Search   search;
    Position current_position = Notation::to_position(Notation::STANDARDPOSITION);
    std::string response;

    std::string generate_frag(std::size_t length, const std::pair<std::vector<char>, std::vector<int> > &possible, const std::vector<int *> &amountptrs, std::mt19937 &gen) const;
};

}  // namespace goldfish
