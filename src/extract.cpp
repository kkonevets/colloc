#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../colloc.hpp"
#include "../tools.hpp"

using namespace cllc;

int main(int argc, char *argv[]) {
  if (argc != 3) {
    printf("wrong number of arguments\n");
    exit(EXIT_FAILURE);
  }

  auto dcorpus = std::string(argv[1]) + "/";
  auto dsave = std::string(argv[2]) + "/";

  convert(dcorpus, dsave, 0, 0);
  lemmatize(dsave);

  bigram_stat(dsave);
  group_lem2(dsave, 1'000);
  bifreq_stat(dsave);
  filter_bilems(dsave, 1'000, 0.01);

  trigram_stat(dsave);
  group_lem3(dsave, 1'000);
  trifreq_stat(dsave);
  filter_trilems(dsave, 1'000, 0.003);

  to_zmap(dsave, "v1.10");
}
