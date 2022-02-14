#include <iostream>

#include "../kmerge.hpp"
#include "../tools.hpp"
#include "grams.pb.h"

using namespace cllc;

// find tri_parts/ -name "*.bin" -exec read_total '{}' \;
int main(int argc, char *argv[]) {
  auto dtype = get_data_type(argv[1]);

  size_t total = 0;
  if (dtype == grams::Unigram::GetDescriptor()->name()) {
    total = cllc::read_total<grams::Unigram>(argv[1]);
  } else if (dtype == grams::Bigram::GetDescriptor()->name()) {
    total = cllc::read_total<grams::Bigram>(argv[1]);
  } else if (dtype == grams::LemId::GetDescriptor()->name()) {
    total = cllc::read_total<grams::LemId>(argv[1]);
  } else if (dtype == grams::LemFreq::GetDescriptor()->name()) {
    total = cllc::read_total<grams::LemFreq>(argv[1]);
  } else if (dtype == grams::Lem2Group::GetDescriptor()->name()) {
    total = cllc::read_total<grams::Lem2Group>(argv[1]);
  } else if (dtype == grams::Trigram::GetDescriptor()->name()) {
    total = cllc::read_total<grams::Trigram>(argv[1]);
  } else if (dtype.empty()) {
    std::cerr << "could't read data header\n";
    exit(1);
  } else {
    std::cerr << "data type not implemented\n";
    exit(1);
  }

  printf("%lu\n", total);

  return 0;
}
