//!
//! @file gramcat.cpp
//! Читает protobuf файл и выводит на экран, например, "gramcat uni.bin | less"
//!

#include <absl/types/optional.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>

#include "../colloc.hpp"
#include "../streamer.hpp"
#include "../tools.hpp"
#include "grams.pb.h"

template <class T> auto load_idmap(const std::string &fin) {
  absl::flat_hash_map<u32, std::string> idmap;
  auto set_one = [&](T *msg) { idmap.try_emplace(msg->id(), msg->str()); };
  cllc::read_apply<T>(fin, set_one);
  return idmap;
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fprintf(stderr, "wrong number of arguments\n");
    return EXIT_FAILURE;
  }

  auto dtype = cllc::get_data_type(argv[1]);

  auto print_unigram = [](const grams::Unigram *msg) {
    printf("%s\t%50u\t%u\n", msg->str().data(), msg->id(), msg->weight());
  };
  auto print_bigram = [](const grams::Bigram *msg) {
    printf("%u\t\t%u\t\t%u\n", msg->id1(), msg->id2(), msg->weight());
  };
  auto print_trigram = [](const grams::Trigram *msg) {
    printf("%u\t\t%u\t\t%u\t\t%u\n", msg->id1(), msg->id2(), msg->id3(),
           msg->weight());
  };
  auto print_lemid = [](const grams::LemId *msg) {
    cllc::print_term(msg->str());
    printf("\t%50u\n", msg->id());
  };
  auto print_lemfreq = [](const grams::LemFreq *msg) {
    cllc::print_term(msg->str());
    printf("\t%50u\t%u\n", msg->id(), msg->weight());
  };
  auto print_bigram_decode = [&](const grams::Bigram *msg) {
    static auto uni = load_idmap<grams::Unigram>(argv[2]);
    auto it1 = uni.find(msg->id1());
    auto it2 = uni.find(msg->id2());
    printf("%50s\t%-50s%16u\n", it1->second.c_str(), it2->second.c_str(),
           msg->weight());
  };
  auto print_lem2group = [](const grams::Lem2Group *msg) {
    printf("%u%16u%16.9lf\n", msg->lid1(), msg->lid2(), msg->weight());
    for (const auto &cs : msg->cases()) {
      printf("\t\t%u%16u%16u\n", cs.wid1(), cs.wid2(), cs.count());
    }
  };
  auto print_lemg2roup_decode = [&](const grams::Lem2Group *msg) {
    static auto uni = load_idmap<grams::Unigram>(argv[2]);
    static auto unilem = load_idmap<grams::LemId>(argv[3]);

    auto it1 = unilem.find(msg->lid1());
    auto it2 = unilem.find(msg->lid2());
    cllc::print_term(it1->second, 30);
    cllc::print_term(it2->second, 30);
    printf("%16.9lf\n", msg->weight());

    for (auto &cs : msg->cases()) {
      auto it3 = uni.find(cs.wid1());
      auto it4 = uni.find(cs.wid2());
      printf("%50s\t%-50s%16u\n", it3->second.c_str(), it4->second.c_str(),
             cs.count());
    }
  };
  auto print_lem3group = [](const grams::Lem3Group *msg) {
    printf("%u%20u%20u%20.9lf\n", msg->lid1(), msg->lid2(), msg->lid3(),
           msg->weight());
    for (const auto &cs : msg->cases()) {
      printf("\t%20u%20u%20u%20u\n", cs.wid1(), cs.wid2(), cs.wid3(),
             cs.count());
    }
  };
  auto print_lem3group_decode = [&](const grams::Lem3Group *msg) {
    static auto uni = load_idmap<grams::Unigram>(argv[2]);
    static auto unilem = load_idmap<grams::LemId>(argv[3]);

    auto it1 = unilem.find(msg->lid1());
    auto it2 = unilem.find(msg->lid2());
    auto it3 = unilem.find(msg->lid3());
    cllc::print_term(it1->second, 30);
    cllc::print_term(it2->second, 30);
    cllc::print_term(it3->second, 30);
    printf("%16.9lf\n", msg->weight());

    for (auto &cs : msg->cases()) {
      auto it3 = uni.find(cs.wid1());
      auto it4 = uni.find(cs.wid2());
      auto it5 = uni.find(cs.wid3());
      printf("%50s\t%-50s%-50s%16u\n", it3->second.c_str(), it4->second.c_str(),
             it5->second.c_str(), cs.count());
    }
  };

  // comment //////////////////////////////////////////////////////////////////

  if (dtype == grams::Unigram::GetDescriptor()->name()) {
    printf("%s\t%50s\t\t%s\n", "WORD", "ID", "COUNT");
    cllc::read_apply<grams::Unigram>(argv[1], print_unigram);
  } else if (dtype == grams::Bigram::GetDescriptor()->name()) {
    if (argc == 2) {
      printf("ID1\t\tID2\t\tCOUNT\n");
      cllc::read_apply<grams::Bigram>(argv[1], print_bigram);
    } else if (argc == 3) {
      cllc::read_apply<grams::Bigram>(argv[1], print_bigram_decode);
    } else {
      std::cerr << dtype << ":wrong number of arguments\n";
    }
  } else if (dtype == grams::Trigram::GetDescriptor()->name()) {
    printf("ID1\t\tID2\t\tID3\t\tCOUNT\n");
    cllc::read_apply<grams::Trigram>(argv[1], print_trigram);
  } else if (dtype == grams::LemId::GetDescriptor()->name()) {
    printf("%s\t%50s\n", "LEM", "ID");
    cllc::read_apply<grams::LemId>(argv[1], print_lemid);
  } else if (dtype == grams::LemFreq::GetDescriptor()->name()) {
    printf("%s\t%50s\t%s\n", "LEM", "ID", "COUNT");
    cllc::read_apply<grams::LemFreq>(argv[1], print_lemfreq);
  } else if (dtype == grams::Lem2Group::GetDescriptor()->name()) {
    if (argc == 2) {
      cllc::read_apply<grams::Lem2Group>(argv[1], print_lem2group);
    } else if (argc == 4) {
      cllc::read_apply<grams::Lem2Group>(argv[1], print_lemg2roup_decode);
    } else {
      std::cerr << dtype << ":wrong number of arguments\n";
    }
  } else if (dtype == grams::Lem3Group::GetDescriptor()->name()) {
    if (argc == 2) {
      cllc::read_apply<grams::Lem3Group>(argv[1], print_lem3group);
    } else if (argc == 4) {
      cllc::read_apply<grams::Lem3Group>(argv[1], print_lem3group_decode);
    } else {
      std::cerr << dtype << ":wrong number of arguments\n";
    }
  } else if (dtype.empty()) {
    std::cerr << "could't read data header\n";
  } else {
    std::cerr << "data type not implemented\n";
  }

  return 0;
}
