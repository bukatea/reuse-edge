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
#include <ndn-cxx/util/scheduler.hpp>
#include <boost/asio/io_service.hpp>

#include <cstddef>
#include <iostream>
#include <string>
#include <../eigen/Eigen/Dense>
#include <../eigen/Eigen/src/Core/IO.h>
#include <thread>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <set>
#include <sys/stat.h>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <iterator>
#include <queue>

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

// reuse table data structure for matrix
struct reusable_table {
    // maps hash of matrix string representations -> (exponent, byte offsets)
    std::unordered_multimap<std::string, std::pair<std::size_t, std::size_t> > umm;
    // maps hash of matrix string representations -> (byte offset intermediate values)
    std::unordered_map<std::string, std::size_t> byte_offsets;
    // mutex for the accessing the table
    std::shared_timed_mutex re_m;

    std::queue<std::thread> cachers;
    std::mutex q_m;
};

// client handler data structure - each client has one
struct client_handler {
    bool wait_to_grab;
    std::mutex m;
    std::string content;
    bool tready;
    int iteration;
    Eigen::MatrixXi mat;
    int counter;
    int numinter;
    std::thread work;
    std::unordered_multimap<std::string, std::pair<std::size_t, std::size_t> >::iterator mat_tableid;

    client_handler() = default;
    client_handler(const std::unordered_multimap<std::string, std::pair<std::size_t, std::size_t> >::iterator &it) : wait_to_grab(false), tready(false), iteration(0), counter(0), mat_tableid(it) {}
};

class Producer : noncopyable {
    public:
        Producer(bool uc) : m_face(m_ioService), m_scheduler(m_ioService), use_cache(uc) {
            mkdir("reusables", S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
        }

        void run() {
            // setup interest filter for computation requests
            m_face.setInterestFilter("/edge-compute/computer",
                                     bind(&Producer::onInterest, this, _1, _2),
                                     RegisterPrefixSuccessCallback(),
                                     bind(&Producer::onRegisterFailed, this, _1, _2));
            m_face.processEvents();
        }

    private:
      // auxiliary function for converting a Eigen::IOFormat-ted string into a Eigen::MatrixXi
      Eigen::MatrixXi strtoMatrix(const std::string &s, int dimension) {
          Eigen::MatrixXi res(dimension, dimension);
          for (std::size_t index = 0, count = 0; index != s.size(); index = s.find("|", index) + 1, count++) {
              std::vector<int> rowv;
              rowv.reserve(dimension);
              std::string srow(s.substr(index, s.find("|", index) - index + 1));
              std::size_t pos;
              std::size_t tot = 0;
              std::size_t *next = &pos;
              while (tot < srow.size()) {
                  rowv.push_back(std::stoi(srow.substr(tot), next));
                  tot += pos + 1;
              }
              res.row(count) = Eigen::Map<const Eigen::MatrixXi>(rowv.data(), 1, dimension);
          }
          return res;
      }

      void multiplyMatrix(int ri, int dimension, int exponent, std::size_t hash) {
          std::cout << "start thread" << std::endl;
          // uncomment following to log cpu in timestamps.dat
//          {
//              std::lock_guard<std::mutex> i_hate_everything(file_m);
//              // for cpu logging
//              std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//              log << "compute, ri: " << ri << " exp: " << exponent << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
//              //
//          }
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[ri];
          Eigen::MatrixXi res;
          if (exponent <= 0)
              // trivial case
              res = Eigen::MatrixXi::Identity(dimension, dimension);
          else {
              // nontrivial; first check if we enabled reuse
              if (use_cache) {
                  // compare function for exponent-ordered set
                  static auto compfirst = [](const std::pair<std::size_t, std::size_t> &a, const std::pair<std::size_t, std::size_t> &b){
                      return a.first < b.first;
                  };
                  // state variables
                  std::ostringstream oss;
                  std::ofstream new_f;
                  int i;
                  std::string currmatstr;
                  std::vector<Eigen::MatrixXi> cache_waitlist;
                  std::shared_lock<std::shared_timed_mutex> slock(reuse_table.re_m);
                  // check if the hash already exists in the reuse table
                  if (chr.mat_tableid != reuse_table.umm.end()) {
                      // it exists! we have it
                      // let's get the matrix string
                      currmatstr = chr.mat_tableid->first;
                      // use the matrix string to retrieve an equal_range of all of the exponents and their respective byte offsets in the master file for the matrix
                      auto its = reuse_table.umm.equal_range(chr.mat_tableid->first);
                      slock.unlock();
                      std::string filename("reusables/" + std::to_string(hash) + ".dat");
                      std::set<std::pair<std::size_t, std::size_t>, decltype(compfirst)> exps(compfirst);
                      // sort them in a set
                      std::transform(its.first, its.second, std::inserter(exps, exps.begin()), [](const std::pair<std::string, std::pair<std::size_t, std::size_t> > &a){
                          return a.second;
                      });
                      // now, we choose the exponent in the reuse table closest in difference to the exponent of the current task
                      auto less_it = exps.upper_bound(std::make_pair(exponent, 0));
                      if (less_it != exps.begin())
                          --less_it;
                      // we're going to use that exponent number as the STARTING point (i) for the multiplication, to save unnecessary multiplications
                      i = less_it->first;
                      slock.lock();
                      {
                          std::ifstream iFile(filename);
                          std::string line;
                          // get the actual base matrix associated with the hash from the file
                          std::getline(iFile, line);
                          // put it in an Eigen::MatrixXi
                          chr.mat = strtoMatrix(line, dimension);
                          // then, jump to the byte offset of the closest exponent and extract that matrix as the starting point
                          iFile.seekg(less_it->second, std::ifstream::beg);
                          std::getline(iFile, line);
                          res = strtoMatrix(line, dimension);
                      }
                      slock.unlock();
                      // now open the file for writing
                      new_f.open(filename, std::ofstream::out | std::ofstream::app);
                  } else {
                      // hash does not exist in the reuse table, this is the first time we've seen it
                      slock.unlock();
                      // we turn chr.mat into a string to use later
                      oss << chr.mat.format(PayloadFmt);
                      currmatstr = oss.str();
                      // clear the ostringstream
                      oss.str(std::string());
                      oss.clear();
                      slock.lock();
                      std::string filename("reusables/" + std::to_string(reuse_table.umm.hash_function()(currmatstr)) + ".dat");
                      // check to see if matrix exists in reuse table
                      if (reuse_table.umm.find(currmatstr) != reuse_table.umm.end()) {
                          // it exists
                          // use the matrix string to retrieve an equal_range of all of the exponents and their respective byte offsets in the master file for the matrix
                          auto its = reuse_table.umm.equal_range(currmatstr);
                          slock.unlock();
                          std::set<std::pair<std::size_t, std::size_t>, decltype(compfirst)> exps(compfirst);
                          // sort them in a set
                          std::transform(its.first, its.second, std::inserter(exps, exps.begin()), [](const std::pair<std::string, std::pair<std::size_t, std::size_t> > &a){
                              return a.second;
                          });
                          // now, we choose the exponent in the reuse table closest in difference to the exponent of the current task
                          auto less_it = exps.upper_bound(std::make_pair(exponent, 0));
                          if (less_it != exps.begin())
                              --less_it;
                          // we're going to use that exponent number as the STARTING point (i) for the multiplication, to save unnecessary multiplications
                          i = less_it->first;
                          slock.lock();
                          {
                              std::ifstream iFile(filename);
                              std::string line;
                              // advance to the byte offset to grab the matrix as the starting point
                              iFile.seekg(less_it->second);
                              std::getline(iFile, line);
                              res = strtoMatrix(line, dimension);
                          }
                          slock.unlock();
                          // now open the file for writing
                          new_f.open(filename, std::ofstream::out | std::ofstream::app);
                      } else {
                          // first time ever seeing the matrix in the reuse table
                          slock.unlock();
                          // set the original matrix as the starting point because that's all we have
                          res = chr.mat;
                          oss << res.format(PayloadFmt);
                          // open file for writing matrix
                          new_f.open(filename, std::ofstream::out | std::ofstream::trunc);
                          std::unique_lock<std::shared_timed_mutex> ulock(reuse_table.re_m);
                          new_f << oss.str();
                          // create entries in reuse table and the byte offsets table
                          reuse_table.umm.emplace(currmatstr, std::make_pair(1, 0));
                          reuse_table.byte_offsets.emplace(currmatstr, oss.str().size() + 1);
                          ulock.unlock();
                          oss.str(std::string());
                          oss.clear();
                          // set the starting point to the beginning
                          i = 1;
                      }
                  }
                  slock.lock();
                  chr.mat_tableid = reuse_table.umm.end();
                  slock.unlock();
                  int j = i;
                  // start the actual multiplication
                  for (; i < exponent; i++)
                      // while multiplying, copy the result into the cache_waitlist for recording later
                      cache_waitlist.emplace_back(res *= chr.mat);
                  // multiplication has finished
                  if (j < exponent) {
                      // if there are things to cache, cache them
                      std::lock_guard<std::mutex> lock(reuse_table.q_m);
                      // check to see if there are too many cachers (changeable)
                      if (reuse_table.cachers.size() >= std::thread::hardware_concurrency()) {
                          std::cout << "too many cachers!" << std::endl;
                          // join the cacher at the front of the queue
                          if (reuse_table.cachers.front().joinable())
                              reuse_table.cachers.front().join();
                          // make room
                          reuse_table.cachers.pop();
                      }
                      // push another cacher onto the queue
                      reuse_table.cachers.emplace([=, oss = std::move(oss), new_f = std::move(new_f)]() mutable {
                          std::cout << "start caching thread for ri " << ri << std::endl;
                          int ival = j;
                          std::lock_guard<std::shared_timed_mutex> ulock(reuse_table.re_m);
                          // start recording the matrices to file
                          for (; j < exponent; j++) {
                              oss << cache_waitlist[j - ival].format(PayloadFmt);
                              new_f << std::endl << oss.str();
                              // add new entries to the reuse table for new exponents
                              reuse_table.umm.emplace(currmatstr, std::make_pair(j + 1, reuse_table.byte_offsets[currmatstr]));
                              reuse_table.byte_offsets[currmatstr] += oss.str().size() + 1;
                              oss.str(std::string());
                              oss.clear();
                          }
                          std::cout << "end caching thread for ri " << ri << std::endl;
                      });
                  }
              } else {
                  // reuse disabled, so naively calculate
                  res = chr.mat;
                  for (int i = 1; i < exponent; i++)
                      res *= chr.mat;
              }
          }
          // we're not currently_operating anymore, so signal the waiting threads (if any)
          currently_operating[hash].signal();
          {
              std::lock_guard<std::mutex> map_lock(map_m);
              currently_operating.erase(hash);
          }
          std::cout << "signaled" << std::endl;
          // finally set the content to the result
          // for now, it just replies Done
          {
              std::lock_guard<std::mutex> locker(chr.m);
              chr.content = "Done";
              // thread is finished, set the ready flag
              chr.tready = true;
          }
          std::cout << "end thread" << std::endl;
          // uncomment following to log cpu in timestamps.dat
//          {
//              std::lock_guard<std::mutex> i_hate_everything(file_m);
//              // for cpu logging
//              std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//              log << "endcomp, ri: " << ri << " exp: " << exponent << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
//              //
//          }
      }

//      int estimateTime() {
//          return log((iteration = 1) * 50.0) / log(1.005) - 750.0;
//      }

      // CTT estimation function
      int estimateTime(int ri) {
          return log(++ch[ri].iteration * 50.0) / log(1.005) - 750.0;
      }

      void onInterest(const InterestFilter& filter, const Interest& interest) {
          std::cout << "received interest " << interest << std::endl;
    
          // Create new name, based on Interest's name
          Name dataName(interest.getName());
    
          std::ostringstream oss;
          oss << dataName;
          std::string s(oss.str());
          int start = nthOccurrence(s, "/", 3) + 1;
          int end = nthOccurrence(s, "/", 4);
          // extract requesterid of client from name
          int requesterid = std::stoi(s.substr(start, end - start));
          start = end + 1;
          end = nthOccurrence(s, "/", 5);
          std::string op = s.substr(start, end - start);

          int dim, exp;
          std::size_t hash;

          // client is not "registered", then create an entry for that requesterid with a client_handler instance to handle the matrix computation
          if (ch.find(requesterid) == ch.end())
              ch.emplace(std::piecewise_construct, std::forward_as_tuple(requesterid), std::forward_as_tuple(reuse_table.umm.end()));
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[requesterid];

          // Create preliminary Data packet
          shared_ptr<Data> data = make_shared<Data>();
          data->setName(dataName);
          data->setFreshnessPeriod(10_s); // 10 seconds
          {
              std::unique_lock<std::mutex> locker(chr.m, std::defer_lock);
              if (op == "multiply") {
                  // check if this interest is the first for this task
                  if (!chr.iteration) {
                      // it's the first, so initialize state variables
                      chr.counter = 0;
                      chr.wait_to_grab = false;
                      start = end + 1;
                      end = nthOccurrence(s, "/", 6);
                      dim = std::stoi(s.substr(start, end - start));
                      start = end + 1;
                      end = nthOccurrence(s, "/", 7);
                      exp = std::stoi(s.substr(start, end - start));
                      // uncomment following to log cpu in timestamps.dat
//                       {
//                           // for cpu logging
//                           std::lock_guard<std::mutex> i_hate_everything(file_m);
//                           std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//                           log << "initial, ri: " << requesterid << " exp: " << exp << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
//                           //
//                       }
                      // if enabling reuse,
                      if (use_cache) {
                          // extract hash
                          hash = std::stoull(s.substr(end + 1));
                          std::lock_guard<std::mutex> map_lock(map_m);
                          // check to see if someone else is currently_operating on the matrix with the same hash
                          if (currently_operating.find(hash) == currently_operating.end()) {
                              // there isn't anyone currently_operating; create an entry in the currently_operating table, because now we operating on it
                              currently_operating.emplace(std::piecewise_construct, std::forward_as_tuple(hash), std::make_tuple());
                              // lock/increment semaphore to show that we are operating
                              currently_operating[hash].wait();
                          } else
                              // there is somebody currently_operating on this matrix, so we wait to grab the results
                              chr.wait_to_grab = true;
                      }
                      // lock the mutex to make sure nobody changes content while we are setting the CTT
                      locker.lock();
                      chr.content = "CTT: " + std::to_string(estimateTime(requesterid));
                      if (chr.wait_to_grab)
                          // we found the hash and someone is using it, so tell the client that it does not have to send matrix
                          chr.content += ", found";
                      else {
                          // nobody is currently_operating on the matrix
                          std::shared_lock<std::shared_timed_mutex> slock(reuse_table.re_m);
                          auto hf = reuse_table.umm.hash_function();
			  // see if we can find the hash of the matrix in the reuse table; if so we set the iterator for it so we can use it access it directly instead of searching through the entire table again

                          for (auto it = reuse_table.umm.begin(); it != reuse_table.umm.end(); it++){
                              if (hf(it->first) == hash) {
                                  chr.mat_tableid = it;
                                  break;
                              }
                          }
                          if (chr.mat_tableid != reuse_table.umm.end())
                              // we found the hash and someone ISN'T using it, so tell the client that it does not have to send matrix
                              chr.content += ", found";
                      }
                  } else {
                      // lock the mutex to make sure nobody changes content while we are setting the result
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
          {
              // lock mutex to make sure no one else is sending while we are
              std::lock_guard<std::mutex> lock(face_m);
              m_face.put(*data);
          }

          if (chr.iteration == 1) {
              // first interest, there's some stuff to do
              if (chr.wait_to_grab) {
                  // we decided earlier that someone is currently operating on my matrix, so we wait
                  chr.work = std::thread([&, hash, requesterid, dim, exp]{
                      // wait for the guy who's currently_operating to finish and notify us
                      currently_operating[hash].wait();
                      std::cout << "done waiting" << std::endl;
                      // NOW we can execute this task because we know it's in the table
                      {
                          std::shared_lock<std::shared_timed_mutex> slock(reuse_table.re_m);
                          auto hf = reuse_table.umm.hash_function();
                          // we KNOW that the hash of the matrix is in the table, so we set the iterator for it so we can use it access it directly instead of searching through the entire table again
                          for (auto it = reuse_table.umm.begin(); it != reuse_table.umm.end(); it++){
                              if (hf(it->first) == hash) {
                                  chr.mat_tableid = it;
                                  break;
                              }
                          }
                      }
                      // proceed to multiplying with an iterator pointing to the exact matrix entry we want to use
                      multiplyMatrix(requesterid, dim, exp, hash);
                  });
              } else {
                  // we shouldn't wait because nobody is operating on this matrix right now
                  std::shared_lock<std::shared_timed_mutex> slock(reuse_table.re_m);
                  // is the hash already in the reuse table?
                  if (chr.mat_tableid == reuse_table.umm.end()) {
                      // nope, so we need the client to send the matrix
                      slock.unlock();
                      // prepare
                      int rows = APP_OCTET_LIM / (dim * 4);
                      int start = nthOccurrence(s, "/", 2) + 1;
                      chr.mat.conservativeResize(dim, dim);
                      chr.numinter = std::ceil(static_cast<double>(dim) / rows);
                      std::cout << "Number of interests sent: " << chr.numinter << std::endl;
                      for (int i = 0; i < chr.numinter; i++) {
                          // create interest requesting for a specific part of the matrix
                          Interest matreq(Name(s.replace(start, std::string::npos, "requester/" + std::to_string(requesterid) + "/matrix/" + std::to_string(i * rows) + '/' + std::to_string(i * rows + rows))).appendVersion());
                          matreq.setInterestLifetime(1_s);
                          matreq.setMustBeFresh(true);

                          // schedule the events in 30 millisecond intervals (hardware requirement for Pi's)
                          // they all run in separate threads for very short periods
                          m_scheduler.scheduleEvent(time::milliseconds(i * 30), [=]{
                              // lock mutex to make sure no one else is sending while we are
                              std::lock_guard<std::mutex> lock(face_m);
                              m_face.expressInterest(matreq,
                                                     bind(&Producer::onData, this, _1, _2, requesterid, i, dim, exp, rows, hash),
                                                     bind(&Producer::onNack, this, _1, _2),
                                                     bind(&Producer::onTimeout, this, _1, requesterid, i, dim, exp, rows, hash));
                          });

                      }
                  } else {
                      // yes, so we can proceed directly to multiplying because we HAVE matrix in the table already
                      slock.unlock();
                      chr.work = std::thread(&Producer::multiplyMatrix, this, requesterid, dim, exp, hash);
                  }
              }
          }
          std::cout << "end onInterest" << std::endl;
      }

      void onData(const Interest& interest, const Data& data, int ri, int crow, int dimension, int exponent, int r, std::size_t hash) {
          // we received part of the matrix, so we need to know where to put it
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[ri];
          std::string dcontent(reinterpret_cast<const char *>(data.getContent().value()), data.getContent().value_size());
          // iterate over rows (delimited by "|")
          for (std::size_t index = 0, count = 0; index != dcontent.size(); index = dcontent.find("|", index) + 1, count++) {
              std::vector<int> rowv;
              rowv.reserve(dimension);
              std::string srow(dcontent.substr(index, dcontent.find("|", index) - index + 1));
              std::size_t pos;
              std::size_t tot = 0;
              std::size_t *next = &pos;
              // iterate over columns in each row (delimited by ",")
              while (tot < srow.size()) {
                  rowv.push_back(std::stoi(srow.substr(tot), next));
                  tot += pos + 1;
              }
              // set the corresponding row in the Eigen::MatrixXi we have here
              chr.mat.row(crow * r + count) = Eigen::Map<const Eigen::MatrixXi>(rowv.data(), 1, dimension);
          }
          std::string intername(interest.toUri());
          std::string dataname(data.getName().toUri());
          // compare interest name with data name (without metadata)
          if (intername.substr(0, nthOccurrence(intername, "/", 7)) == dataname.substr(0, nthOccurrence(dataname, "/", 7)))
              // data corresponds to the correct interest, so increment counter
              chr.counter++;
          std::cout << "Count: " << chr.counter << std::endl;
          if (chr.counter == chr.numinter)
              // we've received all of the data to our interests, so start multiplication
              chr.work = std::thread(&Producer::multiplyMatrix, this, ri, dimension, exponent, hash);
      }
    
      void onNack(const Interest& interest, const lp::Nack& nack) {
          std::cerr << "received Nack with reason " << nack.getReason()
                    << " for interest " << interest << std::endl;
      }
    
      void onTimeout(const Interest& interest, int requesterid, int i, int dim, int exp, int rows, int hash) {
          std::cerr << "Timeout " << interest << std::endl;
          std::string intername = interest.toUri();
          Interest send_this(Name(intername.substr(0, nthOccurrence(intername, "/", 7))).appendVersion());
          std::lock_guard<std::mutex> locker(face_m);
          // re-express the interest with a different Version to avoid the duplicate-Interest Nack
          m_face.expressInterest(send_this,
                                 bind(&Producer::onData, this, _1, _2, requesterid, i, dim, exp, rows, hash),
                                 bind(&Producer::onNack, this, _1, _2),
                                 bind(&Producer::onTimeout, this, _1, requesterid, i, dim, exp, rows, hash));
      }

      void onRegisterFailed(const Name& prefix, const std::string& reason) {
          std::cerr << "ERROR: Failed to register prefix \""
                    << prefix << "\" in local hub's daemon (" << reason << ")"
                    << std::endl;
          m_face.shutdown();
      }
    
    private:
        boost::asio::io_service m_ioService;
        Face m_face;
        std::mutex face_m;
        Scheduler m_scheduler;
        bool use_cache;
        static const Eigen::IOFormat PayloadFmt;
        std::map<int, client_handler> ch;
        reusable_table reuse_table;
        std::map<std::size_t, binary_sem> currently_operating;
        std::mutex map_m;
        std::mutex file_m;
};

const Eigen::IOFormat Producer::PayloadFmt(0, Eigen::DontAlignCols, ",", "|", "", "", "", "|");

} // namespace examples
} // namespace ndn

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ./MAC_matrix <Use Cache?>" << std::endl;
        return 1;
    }
    ndn::examples::Producer producer(std::atoi(argv[1]));
    try {
      producer.run();
    }
    catch (const std::exception& e) {
      std::cerr << "ERROR: " << e.what() << std::endl;
    }
    return 0;
}
