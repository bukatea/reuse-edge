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
#include <cstring>
#include <string>
#include <chrono>
#include <random>
#include <thread>
#include <atomic>
#include <fstream>
#include <functional>
#include <cstddef>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_io.h>
#include <condition_variable>

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

class Consumer : noncopyable {
    public:
        Consumer(int id, double o, int w, const std::string &imn, const std::string &fn)
            : o_(o),
              w_(w),
              imn_(imn),
              lifetime(0),
              flag(false),
              intereststr("/edge-compute/computer/" + std::to_string(id) + "/detectfaces/" + std::to_string(o_)),
              filename(fn, std::ofstream::out | std::ofstream::app) {}
    
        void run() {
            // start producer listener face
            std::thread prodlistener([&](){
                m_face_prod.setInterestFilter("/edge-compute/requester",
                                              bind(&Consumer::onInterest, this, _1, _2),
                                              RegisterPrefixSuccessCallback(),
                                              bind(&Consumer::onRegisterFailed, this, _1, _2));
                m_face_prod.processEvents();
            });
            // runs in background forever
            prodlistener.detach();

            // init time for NFD (important)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // load the image from file name
            dlib::array2d<unsigned char> img;
            dlib::load_image(img, imn_);

            // bounds checking on the snapshot width
            w_ = std::min(w_, static_cast<std::size_t>(img.nc()));
            // create special interest name for the first to differentiate between trials
            std::string specintereststr(intereststr);
            specintereststr += "/" + std::to_string(img.nr()) + "x" + std::to_string(w_) + "/first";
            numinter = std::ceil(static_cast<double>(img.nr()) / static_cast<int>(APP_OCTET_LIM / w_));
            // initialize data packet array
            packets.insert(packets.begin(), numinter, std::make_pair(false, make_shared<Data>()));

            // create default signature (not used but required by ndn-cxx)
            Signature signature;
            SignatureInfo signatureInfo(static_cast<tlv::SignatureTypeValue>(255));
            signature.setInfo(signatureInfo);
            signature.setValue(makeNonNegativeIntegerBlock(tlv::SignatureValue, 0));
            // pre-sign data packets
            for (int i = 0; i < numinter; i++)
                packets[i].second->setSignature(signature);

            // create first snapshot
            dlib::array2d<unsigned char> subimg;
            dlib::assign_image(subimg, dlib::sub_image(img, dlib::rectangle(w_, img.nr())));
            std::size_t offset_f = 1;
            const std::size_t move = w_ * (1 - o_);
            // loop over all snapshots
            while (subimg.nc() > 0) {
                content = std::basic_string<unsigned char>(subimg.begin(), subimg.end());
                // get content string from sub-image

                // create initial
                Name n(specintereststr);
                Interest interest(n.appendVersion());
                interest.setInterestLifetime(100_s); // 2 seconds
                interest.setMustBeFresh(true);
    
                // start timer
                std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
                m_face_cons.expressInterest(interest,
                                            bind(&Consumer::onData, this,  _1, _2),
                                            bind(&Consumer::onNack, this, _1, _2),
                                            bind(&Consumer::onTimeout, this, _1));
                std::cout << "Sending interest " << interest << std::endl;

                // processEvents will block until the requested data received or timeout occurs
                // receive first reply
                m_face_cons.processEvents();
                std::unique_lock<std::mutex> locker(mu);
                // wait until all packets are filled with data using bool as indicator, i.e. wait for all the interests for data to come in and to be filled
                // smarter than the prodreceived method
                cond.wait(locker, [&]{
                    return std::all_of(packets.begin(), packets.end(), [](const std::pair<bool, shared_ptr<Data> > &p){
                        return p.first;
                    });
                });

                // loop over CTTs until receive the result
                while (!flag) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(lifetime));
                    // re-express
                    Interest rinterest(Name(specintereststr).appendVersion());
                    rinterest.setInterestLifetime(30_s);
                    rinterest.setMustBeFresh(true);
                    m_face_cons.expressInterest(rinterest,
                                           bind(&Consumer::onData, this,  _1, _2),
                                           bind(&Consumer::onNack, this, _1, _2),
                                           bind(&Consumer::onTimeout, this, _1));
                    std::cout << "Sending result interest " << rinterest << std::endl;
                    m_face_cons.processEvents();
                }
                auto diff = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
                // end timer, log in file
                filename << o_ << ' ' << w_ << ' ' << (diff / 1000) << "ms" << std::endl;
                // reset results flag for next snapshot
                flag = false;

                // assign next sub-image by moving by overlap increment
                dlib::assign_image(subimg, dlib::sub_image(img, dlib::rectangle(offset_f * move, 0, std::min(offset_f * move + w_ - 1, static_cast<std::size_t>(img.nc() - 1)), img.nr() - 1)));
                offset_f++;

                w_ = subimg.nc();
                specintereststr = intereststr + "/" + std::to_string(img.nr()) + "x" + std::to_string(w_);
                if (static_cast<int>(APP_OCTET_LIM / w_))
                    numinter = std::ceil(static_cast<double>(img.nr()) / static_cast<int>(APP_OCTET_LIM / w_));
                // change number of data packets per snapshot (they are still pre-signed though)
                packets.resize(numinter);
                // reset bools to false for the next cycle
                std::transform(packets.begin(), packets.end(), packets.begin(), [](const std::pair<bool, shared_ptr<Data> > &p){
                    return std::make_pair(false, std::move(p.second));
                });
            }
            // if a floating point error is raised at the end, it's okay
            // everything has completed correctly
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
                // data, end
                flag = true;
        }
    
        void onNack(const Interest& interest, const lp::Nack& nack) {
            std::cerr << "received Nack with reason " << nack.getReason()
                      << " for interest " << interest << std::endl;
        }
    
        void onTimeout(const Interest& interest) {
            std::cerr << "Timeout " << interest << std::endl;
        }
    
        void onInterest(const InterestFilter &filter, const Interest &interest) {
            std::cout << "received interest " << interest << std::endl;

            Name dataName(interest.getName());
            std::ostringstream oss;
            oss << dataName;
            std::string s(oss.str());
            int start = nthOccurrence(s, "/", 4) + 1;
            std::string op = s.substr(start, nthOccurrence(s, "/", 5) - start);
            int end;
            int snum;
    
            if (op == "detectfaces") {
                start = nthOccurrence(s, "/", 5) + 1;
                end = nthOccurrence(s, "/", 6);
                // block of rows: [begrow, endrow)
                int begrow = std::stoi(s.substr(start, end - start));
                int endrow = std::stoi(s.substr(end + 1));
                // packet number for array
                snum = begrow / (endrow - begrow);
                // get specific part of the image based on begrow and endrow
                std::basic_string<unsigned char> portion(content.substr(begrow * w_, endrow * w_ - begrow * w_));

                // set the bool
                if (!packets[snum].first)
                    packets[snum].first = true;
                packets[snum].second->setName(dataName);
                packets[snum].second->setFreshnessPeriod(10_s);
                packets[snum].second->setContent(reinterpret_cast<const unsigned char *>(portion.data()), portion.size());
                std::cout << "sending data " << *(packets[snum].second) << std::endl;
                // send data
                m_face_prod.put(*(packets[snum].second));
            }
            // check whether all the interests have been replied to by checking bools for true
            if (std::all_of(packets.begin(), packets.end(), [](const std::pair<bool, shared_ptr<Data> > &p){
                return p.first;
            })) {
                // notify waiting thread so it can move on for waiting for CTTs
                std::lock_guard<std::mutex> locker(mu);
                cond.notify_one();
            }
        }
    
        void onRegisterFailed(const Name &prefix, const std::string &reason) {
            std::cerr << "ERROR: Failed to register prefix \""
                      << prefix << "\" in local hub's daemon (" << reason << ")"
                      << std::endl;
            m_face_prod.shutdown();
        }
    
    private:
        Face m_face_cons;
        Face m_face_prod;
        double o_;
        std::size_t w_;
        std::string imn_;
        bool use_cache;
        int numinter;
        std::vector<std::pair<bool, shared_ptr<Data> > > packets;
        std::mutex mu;
        std::condition_variable cond;
        int lifetime;
        bool flag;
        std::string intereststr;
        std::basic_string<unsigned char> content;
        std::ofstream filename;
};

} // namespace examples
} // namespace ndn

int main(int argc, char** argv) {
    if (argc != 6) {
        std::cerr << "usage: ./MACconsumer_simcamera <ID> <Overlap Fraction> <Width of Sub-image> <Image> <File Name>" << std::endl;
        return 1;
    }
    ndn::examples::Consumer consumer(std::atoi(argv[1]), std::atof(argv[2]), std::atoi(argv[3]), std::string(argv[4]), std::string(argv[5]));
    try {
        consumer.run();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
    }
    return 0;
}
