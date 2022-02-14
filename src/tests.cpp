#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>

#include "baalbek/babylon/languages/rus.hpp"
#include "baalbek/babylon/lingproc.hpp"

#include "../colloc.hpp"
#include "../kmerge.hpp"
#include "../tools.hpp"
#include "grams.pb.h"

const std::string DSAVE = "/home/guyos/Documents/data/test/colloc/";

void print_docimage(Baalbek::language::docimage &doci) {
  for (const auto &word : doci) {
    auto word_utf8 = codepages::widetombcs(codepages::codepage_utf8,
                                           word.pwsstr, word.length);
    printf("%s\n", word_utf8.c_str());
    for (const auto &term : word) {
      cllc::print_term(absl::string_view(term.data(), term.size()));
      std::cout << " class " << term.get_class() << " forms "
                << term.get_forms().size() << std::endl;
    }
  }
  std::cout << "\n";
}

TEST(CollocMerge, ExtSorter) {
  using namespace cllc;
  std::string dparts = DSAVE + "/parts";

  cllc::system_exec("mkdir -p " + dparts);

  auto gen_msg = [](u32 id1, u32 id2, u32 count) {
    grams::Bigram msg;
    msg.set_id1(id1);
    msg.set_id2(id2);
    msg.set_weight(count);
    return msg;
  };

  std::vector<grams::Bigram> v;
  v.emplace_back(gen_msg(8, 9, 3235));
  v.emplace_back(gen_msg(4, 12, 235));
  v.emplace_back(gen_msg(1, 2, 444235));
  v.emplace_back(gen_msg(8, 8, 9985));

  auto fin = DSAVE + "/grams_to_sort.bin";
  {
    OFStreamer<grams::Bigram> os(fin, v.size());
    for (auto &msg : v) {
      os.write(msg);
    }
  }

  BigramMore cmp;
  std::sort(v.begin(), v.end(), [cmp](auto &a, auto &b) { return cmp(b, a); });
  auto is = IFStreamer<grams::Bigram>(fin);

  auto sorter =
      ExternalSorter<IFStreamer<grams::Bigram>, BigramMore>(dparts, 2);
  auto merger = sorter.sort_unstable(is);
  auto vit = v.begin();
  BigramEq eq;
  for (auto it = merger.begin(); it != merger.end(); ++it, ++vit) {
    ASSERT_TRUE(eq(*it, *vit));
  }
}

TEST(PrintLems, DISABLED_Extended) {
  Baalbek::language::processor lingproc;
  lingproc.AddLanguageModule(0, new Baalbek::language::Russian());

  Baalbek::document doc;
  std::string corpus = "честно говоря";

  doc.add_str(corpus.c_str(), corpus.size())
      .set_codepage(codepages::codepage_utf8);
  doc = lingproc.NormalizeEncoding(doc);
  auto doci = lingproc.MakeDocumentImage(doc);
  for (auto &w : doci) {
    printf("%ld ", w.size());
    for (auto &term : w) {
      cllc::print_term(absl::string_view(term.data(), term.size()), 10);
    }
    printf("\n");
  }
}

TEST(Colloc, DISABLED_zmap) {
  Baalbek::language::processor lingproc;
  lingproc.AddLanguageModule(0, new Baalbek::language::Russian());

  Baalbek::document doc;
  std::string corpus = "марлона брандо энциклопедическим словарём брокгауза";

  doc.add_str(corpus.c_str(), corpus.size())
      .set_codepage(codepages::codepage_utf8);
  doc = lingproc.NormalizeEncoding(doc);
  auto doci = lingproc.MakeDocumentImage(doc);

  auto &w1 = doci[0];
  auto &w2 = doci[1];
  auto t1 = std::string(w1[0].data(), w1[0].size());
  auto t2 = std::string(w2[0].data(), w2[0].size());

  std::ifstream t("/home/guyos/Documents/data/colloc/stat_v1.10.map");
  if (!t.is_open())
    throw std::runtime_error("cant open file");
  std::string buff((std::istreambuf_iterator<char>(t)),
                   std::istreambuf_iterator<char>());
  auto dump = mtc::zmap::dump(buff.data() + 30);

  cllc::print_term(t1, 10);
  printf(" ");
  cllc::print_term(t2, 10);
  printf("\n");

  auto val = dump.get(absl::StrCat(t1, t2));
  if (val != nullptr) {
    auto val1 = val->get_zmap();
    auto str = val1->get("txt")->get_charstr();
    auto count = val1->get("dc")->get_word32();
    printf("%s %u\n", str->c_str(), *count);
  }

  auto t3 = std::string(doci[2][0].data(), doci[2][0].size());
  auto t4 = std::string(doci[3][0].data(), doci[3][0].size());
  auto t5 = std::string(doci[4][0].data(), doci[4][0].size());

  cllc::print_term(t3, 10);
  printf(" ");
  cllc::print_term(t4, 10);
  printf(" ");
  cllc::print_term(t5, 10);
  printf("\n");

  val = dump.get(absl::StrCat(t3, t4, t5));
  if (val != nullptr) {
    auto val1 = val->get_zmap();
    auto str = val1->get("txt")->get_charstr();
    auto count = val1->get("dc")->get_word32();
    printf("%s %u\n", str->c_str(), *count);
  }
}

TEST(PrintLems, DISABLED_bisort) {
  using namespace cllc;

  std::string dsave = "/home/guyos/Documents/data/colloc/";
  using sorter_t =
      ExternalSorter<IFStreamer<grams::Bigram>, LemGroupLess<grams::Bigram>>;
  auto sorter = sorter_t(dsave + "/bi_counts_parts", 100'000'000);
  auto is = IFStreamer<grams::Bigram>(dsave + "/bi.bin");
  auto merger = sorter.sort_unstable(is);

  OFStreamer<grams::Bigram> os(dsave + "/bi_sorted_weight.bin");
  for (auto it = merger.begin(); it != merger.end(); ++it) {
    os.write(*it);
  }
}

int main(int argc, char **argv) {
  cllc::system_exec("rm -rf " + DSAVE);

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
