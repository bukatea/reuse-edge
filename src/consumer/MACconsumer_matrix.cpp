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
#include <../eigen/Eigen/Dense>
#include <../eigen/Eigen/src/Core/IO.h>
#include <random>
#include <thread>
#include <atomic>
#include <fstream>
#include <functional>

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
        Consumer(int id, int d, int e, int mc, const std::string &fn, bool uc) : d_(d), e_(e), use_cache(uc), numinter(std::ceil(static_cast<double>(d_) / static_cast<int>(APP_OCTET_LIM / (d_ * 4)))), packets(numinter, make_shared<Data>()), packiter(packets.begin()), mc_(mc), lifetime(0), flag(false), prodreceived(0), intereststr("/edge-compute/computer/" + std::to_string(id) + "/multiply/" + std::to_string(d_) + "/" + std::to_string(e_)), filename(fn, std::ofstream::out | std::ofstream::app), send(true) {}
    
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

            // create matrix based on parameters
            constructMatrix();
            // if enabling reuse,
            if (use_cache)
                // enable hashing of the matrix at the CN to avoid resends
                intereststr += "/" + std::to_string(std::hash<std::string>()(content));

            // create initial
            Name n(intereststr);
            Interest interest(n.appendVersion());
            interest.setInterestLifetime(100_s); // 2 seconds
            interest.setMustBeFresh(true);

            // create default signature (not used but required by ndn-cxx)
            Signature signature;
            SignatureInfo signatureInfo(static_cast<tlv::SignatureTypeValue>(255));
            signature.setInfo(signatureInfo);
            signature.setValue(makeNonNegativeIntegerBlock(tlv::SignatureValue, 0));
            // pre-sign data packets
            for (int i = 0; i < numinter; i++)
                packets[i]->setSignature(signature);

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
            // check if hash is found at the CN
            if (send) {
                // if not, wait for interests asking for the data
                while (prodreceived < numinter);
                prodreceived = 0;
            }

            // loop over CTTs until receive the result
            while (!flag) {
                std::this_thread::sleep_for(std::chrono::milliseconds(lifetime));
                // re-express
                Interest rinterest(Name(intereststr).appendVersion());
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
            filename << d_ << ' ' << e_ << ' ' << (diff / 1000) << "ms" << std::endl;
        }
    
    private:
        void onData(const Interest& interest, const Data& data) {
            std::string dcontent(reinterpret_cast<const char *>(data.getContent().value()), data.getContent().value_size());
            std::cout << "Received data " << data;
            std::cout << "Content: " << dcontent << std::endl;
            if (dcontent.find("CTT: ") != std::string::npos) {
                // CTT, check whether the hash of the matrix was found at CN
                if (dcontent.find("found") != std::string::npos)
                    // found, set send flag, no need to send matrix
                    send = false;
                // set new wait time and loop
                lifetime = std::stoi(dcontent.substr(5));
            } else
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
    
        void constructMatrix() {
            // create d_ x d_ square matrix filled with mc_
            Eigen::MatrixXi mat(Eigen::MatrixXi::NullaryExpr(d_, d_, [&](){
                return mc_;
            }));
            std::ostringstream pl;
            // stringify it
            pl << mat.format(PayloadFmt);
            content = pl.str();
        }

        void onInterest(const InterestFilter &filter, const Interest &interest) {
            std::cout << "received interest " << interest << std::endl;

            Name dataName(interest.getName());
            std::ostringstream oss;
            oss << dataName;
            std::string s(oss.str());
            int start = nthOccurrence(s, "/", 4) + 1;
            std::string op = s.substr(start, nthOccurrence(s, "/", 5) - start);
    
            if (op == "matrix") {
                start = nthOccurrence(s, "/", 5) + 1;
                int end = nthOccurrence(s, "/", 6);
                // block of rows: [begrow, endrow)
                int begrow = std::stoi(s.substr(start, end - start)) == 0 ? 0 : (nthOccurrence(content, "|", std::stoi(s.substr(start, end - start))) + 1);
                int endrow = nthOccurrence(content, "|", std::stoi(s.substr(end + 1)));
                if (endrow == std::string::npos)
                    endrow = content.size() - 1;
                // get specific part of the matrix based on begrow and endrow
                std::string portion(content.substr(begrow, endrow - begrow + 1));

                (*packiter)->setName(dataName);
                (*packiter)->setFreshnessPeriod(10_s);
                (*packiter)->setContent(reinterpret_cast<const uint8_t *>(portion.data()), portion.size());
                std::cout << "sending data " << *(*packiter) << std::endl;
                // send data
                m_face_prod.put(*(*packiter));
                // go to next pre-signed packet
                ++packiter;
            }
            // increment global counter
            prodreceived++;
            std::cout << "Number of interests received: " << prodreceived << std::endl;
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
        int d_;
        int e_;
        bool use_cache;
        int numinter;
        std::vector<shared_ptr<Data> > packets;
        std::vector<shared_ptr<Data> >::iterator packiter;
        int mc_;
        int lifetime;
        bool flag;
        std::atomic<int> prodreceived;
        std::string intereststr;
        static const Eigen::IOFormat PayloadFmt;
        std::string content;
        std::ofstream filename;
        bool send;
};

const Eigen::IOFormat Consumer::PayloadFmt(0, Eigen::DontAlignCols, ",", "|", "", "", "", "|");

} // namespace examples
} // namespace ndn

int main(int argc, char** argv) {
    if (argc != 7) {
        std::cerr << "usage: ./MACconsumer_matrix <ID> <Dimensions of Matrix> <Exponent> <Matrix Code> <File Name> <Use Cache?>" << std::endl;
        return 1;
    }
    ndn::examples::Consumer consumer(std::atoi(argv[1]), std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]), std::string(argv[5]), std::atoi(argv[6]));
    try {
        consumer.run();
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
    }
    return 0;
}
