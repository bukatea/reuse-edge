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

#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_io.h>

#include <cstddef>
#include <iostream>
#include <string>
#include <thread>
#include <mutex>
#include <utility>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <set>
#include <algorithm>
#include <fstream>

#define UPSCALE 2
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

// client handler data structure - each client has one
struct client_handler {
    std::mutex m;
    std::string content;
    bool tready;
    int iteration;
    dlib::array2d<unsigned char> img;
    int counter;
    int numinter;
    std::thread work;
    int subnumber;
    dlib::frontal_face_detector detector;
    // reuse table data structure: note that for this application it is per client rather than per CN
    // maps overlap percentage -> set of dlib::rectangles representing detected face coordinates
    std::map<double, std::set<dlib::rectangle> > reuse_table;

    client_handler() : tready(false), iteration(0), counter(0), subnumber(0), detector(dlib::get_frontal_face_detector()) {}
};

class Producer : noncopyable {
    public:
        Producer(bool uc) : m_face(m_ioService), m_scheduler(m_ioService), use_cache(uc) {}

        void run() {
            // setup interest filter for computation requests
            m_face.setInterestFilter("/edge-compute/computer",
                                     bind(&Producer::onInterest, this, _1, _2),
                                     RegisterPrefixSuccessCallback(),
                                     bind(&Producer::onRegisterFailed, this, _1, _2));
            m_face.processEvents();
        }

    private:

      void detectImageFaces(int ri, double overlap, int width) {
          std::cout << "start thread " << ri << std::endl;
          // uncomment following to log cpu in timestamps.dat
//          {
//              // for cpu logging
//              std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//              log << "compute, ri: " << ri << " width: " << width << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
//              //
//          }
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[ri];
          // upscale the image to detect more faces
          for (std::size_t i = 0; i < UPSCALE; i++)
              dlib::pyramid_up(chr.img);
          width *= UPSCALE * 2;
          const std::size_t move = std::ceil(width * (1 - overlap));
          // create a new entry for this overlap if one does not exist already
          if (chr.reuse_table.find(overlap) == chr.reuse_table.end())
              chr.reuse_table.emplace(std::piecewise_construct, std::forward_as_tuple(overlap), std::make_tuple());
          dlib::sub_image_proxy<dlib::array2d<unsigned char> > sub(chr.img, dlib::rectangle(width - move, 0, chr.img.nc() - 1, chr.img.nr() - 1));
          std::vector<dlib::rectangle> dets;
          if (!chr.reuse_table[overlap].empty())
              // this overlap percentage exists for this camera already, so we want to detect faces in the sub-image
              dets = chr.detector(sub);
          else
              // this overlap percentage doesn't exist, we don't have anything for reference currently, so run the algorithm on the whole snapshot
              dets = chr.detector(chr.img);
          std::size_t total_faces = dets.size();
          if (use_cache)
              std::cout << "Faces detected in non-overlap: " << total_faces << std::endl;
          if (!chr.reuse_table[overlap].empty()) {
              // since we only detected faces in the sub-image, we need to retreive results for the overlapped region we didn't calculate over
              std::vector<dlib::rectangle> relevant(chr.reuse_table[overlap].lower_bound(dlib::rectangle(dlib::point((chr.subnumber - 1) * move, 0))), chr.reuse_table[overlap].end());
              // add overlapped region number of faces to the newly computed region for a sum over the whole snapshot
              total_faces += relevant.size();
              // translate rectangles to make absolute coordinates for the whole capture instead of ones relative to the current snapshot
              std::transform(dets.begin(), dets.end(), dets.begin(), [&](const dlib::rectangle &a){
                  return dlib::translate_rect(a, width - move + (chr.subnumber - 1) * move, 0);
              });
          }
          if (use_cache)
              // save ordered set of rectangles (newly computed) for future use
              chr.reuse_table[overlap].insert(dets.begin(), dets.end());
          std::cout << "Total faces detected: " << total_faces << std::endl;
          {
              std::lock_guard<std::mutex> locker(chr.m);
              chr.content = std::to_string(total_faces);
              // thread is finished, set the ready flag
              chr.tready = true;
          }
          std::cout << "end thread " << ri << std::endl;
          // uncomment following to log cpu in timestamps.dat
//          {
//              // for cpu logging
//              std::ofstream log("timestamps.dat", std::ofstream::out | std::ofstream::app);
//              log << "endcomp, ri: " << ri << " width: " << width << ' ' << std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count() << std::endl;
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
          // extract requesterid of client from name
          int requesterid = std::stoi(s.substr(start, end - start));
          start = end + 1;
          end = nthOccurrence(s, "/", 5);
          std::string op = s.substr(start, end - start);

          double overlap;
          int height, width;

          // client is not "registered", then create an entry for that requesterid with a client_handler instance to handle matrix computation
          if (ch.find(requesterid) == ch.end())
              ch.emplace(std::piecewise_construct, std::forward_as_tuple(requesterid), std::make_tuple());
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[requesterid];

          // Create preliminary Data packet
          shared_ptr<Data> data = make_shared<Data>();
          data->setName(dataName);
          data->setFreshnessPeriod(1_s); // 10 seconds
          {
              std::unique_lock<std::mutex> locker(chr.m, std::defer_lock);
              if (op == "detectfaces") {
                  // check if interest is the first for this task
                  if (!chr.iteration) {
                      // it's the first, so initialize state variables
                      chr.counter = 0;
                      start = end + 1;
                      end = s.substr(start).find('/') + start;
//                      end = nthOccurrence(s, "/", 6);
                      overlap = std::stod(s.substr(start, end - start));
                      start = end + 1;
                      end = s.substr(start).find('/') + start;
//                      end = nthOccurrence(s, "/", 7);
                      int sec = s.substr(start, end - start).find('x') + start;
                      height = std::stoi(s.substr(start, sec));
                      width = std::stoi(s.substr(sec + 1, end - start));
                      // For counting trials
//                      if (nthOccurrence(s, "/", 8) != std::string::npos)
                      if (s.substr(end + 1).find('/') != std::string::npos)
                          chr.reuse_table.erase(overlap);
                      //
                      // lock the mutex to make sure nobody changes content while we are setting the CTT
                      locker.lock();
                      chr.content = "CTT: " + std::to_string(estimateTime(requesterid));
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
                      // failsafe if for ensuring that we don't continue to count replies to retransmission interests as part of a task where input data has already been completely received
                      if (chr.counter != chr.numinter) {
                          chr.counter = chr.numinter;
                          // increment snapshot counter
                          chr.subnumber++;
                          // start detection
                          chr.work = std::thread(&Producer::detectImageFaces, this, requesterid, overlap, width);
                      }
                  }
              }
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
              // prepare
              int rows = APP_OCTET_LIM / width;
              int start = nthOccurrence(s, "/", 2) + 1;
              chr.img.set_size(height, width);
              chr.numinter = std::ceil(static_cast<double>(height) / rows);
              std::cout << "Number of interests sent: " << chr.numinter << std::endl;
              for (int i = 0; i < chr.numinter; i++) {
                  // create interest requesting for a specific part of the image
                  Interest imgreq(Name(s.replace(start, std::string::npos, "requester/" + std::to_string(requesterid) + "/detectfaces/" + std::to_string(i * rows) + '/' + std::to_string(i * rows + rows))).appendVersion());
                  imgreq.setInterestLifetime(2_s);
                  imgreq.setMustBeFresh(true);

                  // schedule the events in 30 millisecond intervals (hardware requirement for Pi's)
                  // they all run in separate threads for very short periods
                  m_scheduler.scheduleEvent(time::milliseconds(i * 30), [=]{
                      // lock mutex to make sure no one else is sending while we are
                      std::lock_guard<std::mutex> lock(face_m);
                      m_face.expressInterest(imgreq,
                                             bind(&Producer::onData, this, _1, _2, requesterid, i, overlap, width, rows),
                                             bind(&Producer::onNack, this, _1, _2),
                                             bind(&Producer::onTimeout, this, _1, requesterid, i, overlap, width, rows));
                  });
              }
          }
          std::cout << "end onInterest" << std::endl;
      }

      void onData(const Interest& interest, const Data& data, int ri, int crow, double ol, int w, int r) {
          // we received part of the image, so we need to know where to put it
          // save a reference to minimize operator[] calls
          client_handler &chr = ch[ri];
          std::basic_string<unsigned char> dcontent(reinterpret_cast<const unsigned char *>(data.getContent().value()), data.getContent().value_size());
          // iterate over rows
          for (std::size_t index = 0; index < std::min(static_cast<std::size_t>(r), dcontent.size() / w); index++) {
              std::basic_string<unsigned char> srow(dcontent.substr(index * w, w));
              // iterate over columns in each row
              for (std::size_t ei = 0; ei < srow.size(); ei++)
                  // set the corresponding pixel in the dlib::array2d<unsigned char> we have
                  chr.img[crow * r + index][ei] = srow[ei];
          }
          std::string intername(interest.toUri());
          std::string dataname(data.getName().toUri());
          // compare interest name with data name (without metadata)
          if (intername.substr(0, nthOccurrence(intername, "/", 7)) == dataname.substr(0, nthOccurrence(dataname, "/", 7)))
              // data corresponds to the correct interest, so increment counter
              chr.counter++;
          std::cout << "Count: " << chr.counter << std::endl;
          if (chr.counter == chr.numinter) {
              // increment snapshot counter
              chr.subnumber++;
              // we've received all the data to our interests for this snapshot, start face detection
              chr.work = std::thread(&Producer::detectImageFaces, this, ri, ol, w);
          }
      }
    
      void onNack(const Interest& interest, const lp::Nack& nack) {
          std::cerr << "received Nack with reason " << nack.getReason()
                    << " for interest " << interest << std::endl;
      }
    
      void onTimeout(const Interest& interest, int requesterid, int i, double overlap, int width, int rows) {
          std::cerr << "Timeout " << interest << std::endl;
          if (ch[requesterid].counter != ch[requesterid].numinter) {
              std::string intername(interest.toUri());
              Interest send_this(Name(intername.substr(0, nthOccurrence(intername, "/", 7))).appendVersion());
              std::lock_guard<std::mutex> locker(face_m);
              // re-express the interest with a different Version to avoid the duplicate-Interest Nack
              m_face.expressInterest(send_this,
                                     bind(&Producer::onData, this, _1, _2, requesterid, i, overlap, width, rows),
                                     bind(&Producer::onNack, this, _1, _2),
                                     bind(&Producer::onTimeout, this, _1, requesterid, i, overlap, width, rows));
          }
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
        std::map<int, client_handler> ch;
        std::mutex man_m;
};

} // namespace examples
} // namespace ndn

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: ./MAC_simcamera <Use Cache?>" << std::endl;
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
