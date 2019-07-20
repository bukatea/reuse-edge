/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2013-2018 Regents of the University of California.
 *
 * This file is part of ndn-cxx library (NDN C++ library with eXperimental eXtensions).
 *
 * ndn-cxx library is free software: you can redistribute it and/or modify it under the
 * terms of the GNU Lesser General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option) any later version.
 *
 * ndn-cxx library is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 * You should have received copies of the GNU General Public License and GNU Lesser
 * General Public License along with ndn-cxx, e.g., in COPYING.md file.  If not, see
 * <http://www.gnu.org/licenses/>.
 *
 * See AUTHORS.md for complete list of ndn-cxx authors and contributors.
 *
 * @author Alexander Afanasyev <http://lasr.cs.ucla.edu/afanasyev/index.html>
 */


#include <ndn-cxx/face.hpp>

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <fstream>

#include "chesstest.hpp"

#define APP_OCTET_LIM (MAX_NDN_PACKET_SIZE - 400)

int nthOccurrence(const std::string& str, const std::string& findMe, int nth) {
    size_t pos = 0;
    int cnt = 0;

    while (cnt != nth) {
        pos = str.find(findMe, pos);
        if (pos == std::string::npos)
            return std::string::npos; 
        pos += 1;
        cnt++;
    }   
    return pos - 1;
}

// Enclosing code in ndn simplifies coding (can also use `using namespace ndn`)
namespace ndn {
// Additional nested namespaces can be used to prevent/limit name conflicts
namespace examples {

class Consumer : noncopyable {
    public:
        Consumer(int id, double p, int d, const std::string &fn)
            : p_(p),
              d_(d),
              intereststr("/edge-compute/computer/" + std::to_string(id) + "/chess/" + std::to_string(d_)),
              numinter(std::ceil(static_cast<double>(d_) / static_cast<int>(APP_OCTET_LIM / (d_ * 4)))),
              lifetime(0),
              flag(false),
              filename(fn, std::ofstream::out | std::ofstream::app),
              use_file(false) {}

        Consumer(int id, double p, int d, const std::string &fn, const std::string &ifn, int lineno)
            : p_(p),
              d_(d),
              intereststr("/edge-compute/computer/" + std::to_string(id) + "/chess/" + std::to_string(d_)),
              numinter(std::ceil(static_cast<double>(d_) / static_cast<int>(APP_OCTET_LIM / (d_ * 4)))),
              lifetime(0),
              flag(false),
              filename(fn, std::ofstream::out | std::ofstream::app),
              use_file(true),
              infile(ifn),
              ln(lineno) {}
    
        void run() {
            goldfish::ChessTest engine;
            std::string fen;
            // check if run from file FENs
            if (use_file) {
                // get specific line number
                for (int i = 0; i < ln; i++)
                    std::getline(infile, fen);
            } else {
                // generate FEN randomly, log in file
                fen = engine.generate_fen(p_);
                std::ofstream params("chessparams.txt", std::ofstream::out | std::ofstream::app);
                params << fen << std::endl;
            }
            intereststr += "/" + fen;
            engine.receive_quit();
 
            // create initial
            Name n(intereststr);
            Interest interest(n.appendVersion());
            interest.setInterestLifetime(100_s); // 2 seconds
            interest.setMustBeFresh(true);
    
            // start timer
            std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
            m_face.expressInterest(interest,
                                        bind(&Consumer::onData, this,  _1, _2),
                                        bind(&Consumer::onNack, this, _1, _2),
                                        bind(&Consumer::onTimeout, this, _1));
            std::cout << "Sending interest " << interest << std::endl;

            // processEvents will block until the requested data received or timeout occurs
            // receive first reply
            m_face.processEvents();

            // loop over CTTs until receive the result
            while (!flag) {
                std::this_thread::sleep_for(std::chrono::milliseconds(lifetime));
                // re-express
                Interest rinterest(Name(intereststr).appendVersion());
                rinterest.setInterestLifetime(30_s);
                rinterest.setMustBeFresh(true);
                m_face.expressInterest(rinterest,
                                       bind(&Consumer::onData, this,  _1, _2),
                                       bind(&Consumer::onNack, this, _1, _2),
                                       bind(&Consumer::onTimeout, this, _1));
                std::cout << "Sending result interest " << rinterest << std::endl;
                m_face.processEvents();
            }
            auto diff = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            // end timer, log in file
            filename << p_ << ' ' << d_ << ' ' << (diff / 1000) << "ms" << std::endl;
        }
    
    private:
        void onData(const Interest& interest, const Data& data) {
            std::string dcontent(reinterpret_cast<const char *>(data.getContent().value()), data.getContent().value_size());
            std::cout << "Received data " << data;
            std::cout << "Content: " << dcontent << std::endl;
            if (dcontent.find("CTT: ") != std::string::npos)
                // CTT, set new wait time and loop
                lifetime = std::stoi(dcontent.substr(5));
            else
                // result, end
                flag = true;
        }
    
        void onNack(const Interest& interest, const lp::Nack& nack) {
            std::cerr << "received Nack with reason " << nack.getReason()
                      << " for interest " << interest << std::endl;
        }
    
        void onTimeout(const Interest& interest) {
            std::cerr << "Timeout " << interest << std::endl;
        }
    
    private:
        Face m_face;
        double p_;
        int d_;
        int numinter;
        int lifetime;
        bool flag;
        std::string intereststr;
        std::ofstream filename;
        bool use_file;
        std::ifstream infile;
        int ln;
};

} // namespace examples
} // namespace ndn

int main(int argc, char** argv) {
    if (argc != 5 && argc != 7) {
        std::cerr << "usage: ./MACconsumer_matrix <ID> <Probability of Starting Move> <Depth> <File Name> [<FEN Input File> <Line Number>]" << std::endl;
        return 1;
    }
    if (argc == 7) {
        // run with FENs from a file
        ndn::examples::Consumer consumer(std::atoi(argv[1]), std::atof(argv[2]), std::atoi(argv[3]), std::string(argv[4]), std::string(argv[5]), std::atoi(argv[6]));
        consumer.run();
    } else {
        // run with random FENs
        ndn::examples::Consumer consumer(std::atoi(argv[1]), std::atof(argv[2]), std::atoi(argv[3]), std::string(argv[4]));
        consumer.run();
    }

    return 0;
}
