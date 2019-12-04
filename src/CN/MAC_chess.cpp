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
#include <ndn-cxx/security/key-chain.hpp>

#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <cmath>
#include <map>
#include <unordered_map>
#include <boost/algorithm/string.hpp>
#include <algorithm>
#include <random>
#include <fstream>

#include "chesstest.hpp"

#define APP_OCTET_LIM (MAX_NDN_PACKET_SIZE - 400)

int nthOccurrence(const std::string& str, const std::string& findMe, int nth) {
    std::size_t pos = 0;
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

// binary semaphore class: waiting for a currently operating thread for a specific matrix to finish so another thread can use the results from the reuse table
class binary_sem {
    public:
        binary_sem() : flag_(true) {}
    
        void wait() {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [=]{
                return flag_;
            });
            flag_ = false;
        }

        bool try_wait() {
            std::lock_guard<std::mutex> lk(m_);
            if (flag_) {
                flag_ = false;
                return true;
            } else
                return false;
        }

        void signal() {
            std::lock_guard<std::mutex> lk(m_);
            flag_ = true;
            cv_.notify_all();
            flag_ = true;
        }
    
    private:
        bool flag_;
        std::mutex m_;
        std::condition_variable cv_;
}; 

// client handler data structure - each client has one
struct client_handler {
    bool wait_to_grab;
    std::mutex m;
    std::string content;
    bool tready;
    int iteration;
    std::string fen;
    std::thread work;

    client_handler() : wait_to_grab(false), tready(false), iteration(0) {}
};

class Producer : noncopyable {
    public:
        Producer(double pnfm, bool uc) : non_first_frac(pnfm), use_cache(uc) {}

        void run() {
            // setup interest filter for computation requests
            m_face.setInterestFilter("/edge-compute/computer",
                                     bind(&Producer::onInterest, this, _1, _2),
                                     RegisterPrefixSuccessCallback(),
                                     bind(&Producer::onRegisterFailed, this, _1, _2));
            m_face.processEvents();
        }

    private:

      void optimalMove(int ri, int depth) {
          std::cout << "start thread" << std::endl;
          // uncomment following to log cpu in timestamps.dat
//          {
//              // for cpu logging
//              std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//              log << "compute, ri: " << ri << " depth: " << depth << ' ' << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
//              //
//          }
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[ri];
          // check if we enabled reuse
          if (use_cache) {
              std::shared_lock<std::shared_timed_mutex> slock(re_m);
              // check to see if FEN exists in reuse table
              if (reuse_table.find(chr.fen) == reuse_table.end()) {
                  // FEN does not exist
                  // there are still possiblestarts to fill in the reuse_table
                  if (reuse_table.size() < goldfish::ChessTest::possiblestarts.size()) {
                      slock.unlock();
                      // check if FEN is among the possiblestarts
                      if (std::find(goldfish::ChessTest::possiblestarts.cbegin(), goldfish::ChessTest::possiblestarts.cend(), chr.fen) != goldfish::ChessTest::possiblestarts.cend()) {
                          // it is, so we save it in the reuse table
                          std::lock_guard<std::shared_timed_mutex> slock(re_m);
                          reuse_table.emplace(std::piecewise_construct, std::forward_as_tuple(chr.fen), std::make_tuple());
                      } else {
                          // it is not a possiblestart
                          static std::random_device rd;
                          static std::mt19937 gen(rd());
                          static std::uniform_int_distribution<> dis(1, 100);
                          // we save non_first_frac percent of non-possiblestarts via RNG
                          if (dis(gen) <= non_first_frac * 100) {
                              std::lock_guard<std::shared_timed_mutex> slock(re_m);
                              reuse_table.emplace(std::piecewise_construct, std::forward_as_tuple(chr.fen), std::make_tuple());
                          }
                      }
                  }
              } else {
                  // FEN is in the reuse table already!
                  slock.unlock();
                  // we're not currently_operating anymore, so signal the waiting threads (if any)
                  currently_operating[chr.fen].signal();
                  {
                      std::lock_guard<std::mutex> map_lock(map_m);
                      currently_operating.erase(chr.fen);
                  }
                  std::cout << "signaled" << std::endl;
                  std::lock_guard<std::mutex> locker(chr.m);
                  slock.lock();
                  // finally set the content to the result
                  chr.content = reuse_table[chr.fen][depth];
                  slock.unlock();
                  // thread is finished, set the ready flag
                  chr.tready = true;
                  std::cout << "end thread" << std::endl;
                  return;
              }
          }

          // FEN is not in the table, we have to compute
          goldfish::ChessTest engine;
          engine.receive_position(chr.fen);
          engine.receive_go(depth);
          engine.receive_quit(true);
          // check if enabled reuse
          if (use_cache) {
              // it is enabled, so save the result in the table
              std::shared_lock<std::shared_timed_mutex> slock(re_m);
              if (reuse_table.find(chr.fen) != reuse_table.end())
                  reuse_table[chr.fen].emplace(depth, engine.receive_response());
          }

          // we're not currently_operating anymore, so signal the waiting threads (if any)
          currently_operating[chr.fen].signal();
          {
              std::lock_guard<std::mutex> map_lock(map_m);
              currently_operating.erase(chr.fen);
          }
          std::cout << "signaled" << std::endl;
          // finally set the content to the result
          {
              std::lock_guard<std::mutex> locker(chr.m);
              chr.content = engine.receive_response();
              // thread is finished, set the ready flag
              chr.tready = true;
          }
          std::cout << "end thread" << std::endl;
          // uncomment following to log cpu in timestamps.dat
//          {
//              // for cpu logging
//              std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//              log << "endcomp, ri: " << ri << " depth: " << depth << ' ' << std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
//              //
//          }
      }

      // CTT estimation function
      int estimateTime(int ri) {
          return log(++ch[ri].iteration * 50.0) / log(1.005) - 750.0;
      }

      void onInterest(const InterestFilter& filter, const Interest& interest) {
          std::cout << "received interest " << interest << std::endl;

          // Create new name, based on Interest's name
          Name dataName(interest.getName());
    
          std::string s(dataName.toUri());
          int start = nthOccurrence(s, "/", 3) + 1;
          int end = nthOccurrence(s, "/", 4);
          // extract requiesterid of client from name
          int requesterid = std::stoi(s.substr(start, end - start));
          start = end + 1;
          end = nthOccurrence(s, "/", 5);
          std::string op = s.substr(start, end - start);

          int depth;

          // client is not "registered", then create an entry for that requesterid with a client_handler instance to handle the matrix computation
          if (ch.find(requesterid) == ch.end())
              ch.emplace(std::piecewise_construct, std::forward_as_tuple(requesterid), std::make_tuple());
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[requesterid];

          // Create preliminary Data packet
          shared_ptr<Data> data = make_shared<Data>();
          data->setName(dataName);
          data->setFreshnessPeriod(10_s); // 10 seconds
          {
              std::unique_lock<std::mutex> locker(chr.m, std::defer_lock);
              if (op == "chess") {
                  // check if this interest is the first for this task
                  if (!chr.iteration) {
                      // it's the first, so initialize state variables
                      chr.wait_to_grab = false;
                      start = end + 1;
                      end = nthOccurrence(s, "/", 6);
                      depth = std::stoi(s.substr(start, end - start));
                      start = end + 1;
                      end = nthOccurrence(s, "/", 14);
                      chr.fen = s.substr(start, end - start);
                      // when encoding names, spaces turned into %20's, so now we need to replace them with the spaces
                      boost::replace_all(chr.fen, "%20", " ");
                      // if enabling reuse,
                      if (use_cache) {
                          std::lock_guard<std::mutex> map_lock(map_m);
                          // check to see if someone else is currently_operating on the FEN
                          if (currently_operating.find(chr.fen) == currently_operating.end()) {
                              // there isn't anyone currently_operating; create an entry in the currently_operating table, because now we operating on it
                              currently_operating.emplace(std::piecewise_construct, std::forward_as_tuple(chr.fen), std::make_tuple());
                              // lock/increment semaphore to show that we are operating
                              currently_operating[chr.fen].wait();
                          } else
                              // there is somebody currently_operating on this matrix, so we wait to grab the results
                              chr.wait_to_grab = true;
                      }
                      // lock the mutex to make sure nobody changes content while we are setting the CTT
                      locker.lock();
                      chr.content = "CTT: " + std::to_string(estimateTime(requesterid));
                  } else {
                      // lock the mutex to make sure nobody changes content while we are settingthe result
                      locker.lock();
                      if (!chr.tready) {
                          // the thread is not done, so set the CTT
                          chr.content = "CTT: " + std::to_string(estimateTime(requesterid));
                      } else {
                          // the thread is done, the result is already set in content, so join the thread, reset some variables
                          if (chr.work.joinable())
                              chr.work.join();
                          chr.tready = false;
                          chr.iteration = 0;
                      }
                  }
              }

              // finally set the content
              data->setContent(reinterpret_cast<const uint8_t *>(chr.content.data()), chr.content.size());
          }

          // create default signature (not used but required by ndn-cxx)
          Signature signature;
          SignatureInfo signatureInfo(static_cast<tlv::SignatureTypeValue>(255));
          signature.setInfo(signatureInfo);
          signature.setValue(makeNonNegativeIntegerBlock(tlv::SignatureValue, 0));
          // sign packet
          data->setSignature(signature);
    
          // Return Data packet to the requester
          std::cout << "content: " << chr.content << std::endl;
          std::cout << "sending data " << *data << std::endl;
          m_face.put(*data);

          if (chr.iteration == 1) {
              // first interest, there's some stuff to do
              if (chr.wait_to_grab) {
                  // we decided earlier that someone is currently operating on the FEN, so we wait
                  chr.work = std::thread([&]{
                      // wait for the guy who's currently_operating to finish and notify us
                      currently_operating[chr.fen].wait();
                      std::cout << "done waiting" << std::endl;
                      // NOW we can execute this task because we know it's in the table
                      optimalMove(requesterid, depth);
                  });
              } else
                  // we shouldn't wait because nobody is operating on this FEN right now
                  chr.work = std::thread(&Producer::optimalMove, this, requesterid, depth);
          }
          std::cout << "end onInterest" << std::endl;
      }

      void onRegisterFailed(const Name& prefix, const std::string& reason) {
          std::cerr << "ERROR: Failed to register prefix \""
                    << prefix << "\" in local hub's daemon (" << reason << ")"
                    << std::endl;
          m_face.shutdown();
      }
    
    private:
        Face m_face;
        double non_first_frac;
        bool use_cache;
        std::map<int, client_handler> ch;
        // maps hash of FEN -> (depth -> countermove)
        std::unordered_map<std::string, std::map<int, std::string> > reuse_table;
        std::shared_timed_mutex re_m;
        std::map<std::string, binary_sem> currently_operating;
        std::mutex map_m;
};

} // namespace examples
} // namespace ndn

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: ./MAC_matrix <Store Percent for Non-First Moves> <Use Cache?>" << std::endl;
        return 1;
    }
    ndn::examples::Producer producer(std::atof(argv[1]), std::atoi(argv[2]));
    try {
      producer.run();
    }
    catch (const std::exception& e) {
      std::cerr << "ERROR: " << e.what() << std::endl;
    }
    return 0;
}
