#include <absl/container/flat_hash_map.h>
#include <algorithm>
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <capnp/serialize.h>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "../colloc.hpp"
#include "../compare.hpp"
#include "../kmerge.hpp"
#include "../streamer.hpp"
#include "../tools.hpp"

namespace cllc {

UnigramCounts::UnigramCounts(const std::string &dsave) {
  system_exec("mkdir -p " + dsave);
  auto fname = dsave + "/corpus.bin";
  int fd{open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)};
  if (fd < 0) {
    std::ostringstream ss;
    ss << "could't open file " << fname << ", error: " << strerror(errno);
    throw std::runtime_error(ss.str());
  }
  fdStream = std::make_unique<kj::FdOutputStream>(fd);
  bufferedOut = std::make_unique<kj::BufferedOutputStreamWrapper>(*fdStream);
}

absl::optional<u32>
UnigramCounts::update_word(const Baalbek::language::word &w) {
  if (w.IsPunct())
    return absl::nullopt; // игнорируем пунктуацию

  size_t maxlen = 50;
  if (w.length > maxlen)
    return absl::nullopt; // игнорируем слишком большие слова

  // игнорим английские буквы, цифры и всякий мусор
  auto ignore = [](widechar wc) {
    if (wc < 1024 || wc > 1105) // только русские буквы
      return true;
    return false;
  };

  if (std::any_of(w.pwsstr, w.pwsstr + w.length, ignore))
    return absl::nullopt;

  wlower_.resize(w.length);
  codepages::strtolower(wlower_.data(), wlower_.size(), w.pwsstr, w.length);
  auto mbcs = codepages::widetombcs(codepages::codepage_utf8, wlower_.data(),
                                    wlower_.size());
  auto p = this->try_emplace(std::move(mbcs), this->size() + 1);
  auto &value = p.first->second;
  value.weight++;
  return value.id;
}

bool UnigramCounts::update(const Baalbek::language::docimage &doci) {
  std::vector<u32> ids;

  auto write_phrase = [&]() {
    capnp::MallocMessageBuilder message;
    Phrase::Builder phrase{message.initRoot<Phrase>()};
    ::capnp::List<u32>::Builder pids = phrase.initIds(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
      pids.set(i, ids[i]);
    }
    capnp::writePackedMessage(*bufferedOut, message);
  };

  bool empty = true;
  for (const auto &w : doci) {
    auto res = update_word(w);

    if (!res.has_value()) {
      if (ids.size() > 0) {
        write_phrase();
      }
      ids.clear();
      continue;
    }

    ids.push_back(res.value());
    empty = false;
  }

  // finally
  if (ids.size() > 0) {
    write_phrase();
  }
  ids.clear();
  if (!empty) {
    write_phrase(); // end of document
  }
  return !empty;
}

void convert(const std::string &dcorpus, const std::string &dsave, size_t from,
             size_t limit) {
  Baalbek::language::processor lingproc;
  lingproc.AddLanguageModule(0, new Baalbek::language::Russian());

  size_t i = 0, total_count = 0;
  UnigramCounts counts(dsave);
  auto fn = [&](const std::string &fname) {
    for (auto &buff : GetDocsContents(fname)) {
      Baalbek::document doc;
      doc.add_str(buff.data(), buff.size())
          .set_codepage(codepages::codepage_utf8);
      if (doc.length() == 0)
        return;

      // std::cout << fname << "\n";

      doc = lingproc.NormalizeEncoding(doc);
      auto doci = lingproc.WordBreakDocument(doc);
      total_count += counts.update(doci);

      if (i % 100 == 0)
        std::cout << "\r" << i << ": " << counts.size() << std::flush;
      i++;
    }
  };

  cllc::listFiles(dcorpus, fn, from, limit);

  save_uni(counts, dsave + "/uni.bin");

  printf("\ntotal_count: %lu\n", total_count);
  std::ofstream total_count_file;
  total_count_file.open(dsave + "/total_count.txt");
  if (total_count_file.is_open()) {
    total_count_file << total_count;
    total_count_file.close();
  } else {
    std::stringstream ss;
    ss << "unable to write to " << dsave + "/total_count.txt";
    throw std::runtime_error(ss.str());
  }
}

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

void save_uni(const UnigramCounts &uni, const std::string &fout) {
  OFStreamer<grams::Unigram> os(fout, uni.size());
  grams::Unigram msg;
  for (const auto &el : uni) {
    msg.set_str(el.first);
    msg.set_id(el.second.id);
    msg.set_weight(el.second.weight);
    os.write(msg);
  }
}

void save_bi(absl::flat_hash_map<Idd, u32> &bi, const std::string &fout) {
  auto v = sort_map(bi);
  bi.clear();
  OFStreamer<grams::Bigram> os(fout, v.size());
  grams::Bigram msg;
  for (const auto &el : v) {
    msg.set_id1(el.first.first);
    msg.set_id2(el.first.second);
    msg.set_weight(el.second);
    os.write(msg);
  }
}

void save_tri(absl::flat_hash_map<Iddd, u32> &tri, const std::string &fout) {
  auto v = sort_map(tri);
  tri.clear();
  OFStreamer<grams::Trigram> os(fout, v.size());
  grams::Trigram msg;
  for (const auto &el : v) {
    auto &t = el.first;
    msg.set_id1(std::get<0>(t));
    msg.set_id2(std::get<1>(t));
    msg.set_id3(std::get<2>(t));
    msg.set_weight(el.second);
    os.write(msg);
  }
}

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

void Lemmer::save(const std::string &dsave) {
  {
    OFStreamer<grams::LemId> os(dsave + "lemid.bin", lemid.size());
    grams::LemId msg;
    for (const auto &el : lemid) {
      msg.set_str(el.first);
      msg.set_id(el.second);
      os.write(msg);
    }
  }

  {
    OFStreamer<grams::Phrase> os(dsave + "lems.bin", lems.size());
    for (const auto &lids : lems) {
      grams::Phrase msg;
      for (auto lid : lids) {
        msg.add_ids(lid);
      }
      if (!msg.ids().empty())
        os.write(msg);
    }
  }
}

void lemmatize(const std::string &dsave) {
  Lemmer lm;
  auto funi{dsave + "/uni.bin"};
  lm.lems.resize(read_total<grams::Unigram>(funi));

  Baalbek::language::processor lingproc;
  lingproc.AddLanguageModule(0, new Baalbek::language::Russian());

  std::string corpus;
  std::vector<u32> ids;
  auto apply_chunk = [&]() {
    Baalbek::document doc;
    doc.add_str(corpus).set_codepage(codepages::codepage_utf8);
    doc = lingproc.NormalizeEncoding(doc);
    // лемматизируем все уникальные слова
    auto doci = lingproc.MakeDocumentImage(doc);

    std::vector<u32>::iterator it = ids.begin();
    for (const auto &w : doci) {
      std::vector<u32> terms;
      terms.reserve(w.size());
      for (const auto &term : w) {
        absl::string_view view(term.data(), term.size());
        auto p = lm.lemid.try_emplace(view, lm.lemid.size() + 1);
        terms.push_back(p.first->second);
      }
      lm.lems.at(*it - 1) = std::move(terms);
      ++it;
    }
  };

  size_t i = 1;
  auto fn = [&](grams::Unigram *m) {
    corpus.append(m->str());
    corpus.append(" ");
    ids.push_back(m->id());
    if (i % 10'000 == 0) {
      apply_chunk();
      corpus.clear();
      ids.clear();
    }
    ++i;
  };

  read_apply<grams::Unigram>(funi, fn);

  // finally
  apply_chunk();
  lm.save(dsave);
}

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

auto load_lems(const std::string &fname) -> std::vector<std::vector<u32>> {
  std::vector<std::vector<u32>> lems;
  lems.reserve(read_total<grams::Phrase>(fname));

  auto load_lems = [&](grams::Phrase *ph) {
    std::vector<u32> ids{ph->ids().begin(), ph->ids().end()};
    lems.emplace_back(std::move(ids));
  };
  read_apply<grams::Phrase>(fname, load_lems);

  return lems;
}

void merge_unifreq(absl::flat_hash_map<u32, u32> &uni,
                   const std::string &dsave) {
  if (uni.size() != read_total<grams::LemId>(dsave + "lemid.bin")) {
    throw std::runtime_error("uni size does't match lemid size");
  }

  auto fout = dsave + "lemfreq.bin";
  OFStreamer<grams::LemFreq> os(fout, uni.size());

  auto fn = [&](grams::LemId *m) {
    grams::LemFreq um;
    um.set_str(m->str());
    um.set_id(m->id());
    auto it = uni.find(m->id());
    um.set_weight(it->second);
    os.write(um);
  };
  read_apply<grams::LemId>(dsave + "lemid.bin", fn);

  uni.clear();
}

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

void bigram_stat(const std::string &dsave) {
  absl::flat_hash_map<Idd, u32> bis;
  auto dout = dsave + "/bi_parts/";
  system_exec("mkdir -p " + dout);

  u32 docid = 1, chunk = 1;
  auto save_chunk = [&]() {
    auto fout = dout + std::to_string(chunk) + "_bi.bin";
    save_bi(bis, fout);
    chunk++;
  };

  auto fn = [&](const Phrase::Reader &r) {
    const auto &ids = r.getIds();
    if (ids.size() == 0) { // end of document
      if (docid % 100 == 0) {
        std::cout << "\r" << docid << ": " << bis.size() << std::flush;
      }
      if (docid % 40'000 == 0) {
        save_chunk();
      }
      docid++;
    }

    for (auto prev = ids.begin(), it = prev + 1; it < ids.end(); prev = it++) {
      auto p = bis.try_emplace(std::make_pair(*prev, *it), 0);
      p.first->second++;
    }
  };
  read_fn<Phrase>(dsave + "/corpus.bin", fn);

  save_chunk();
  merge_files<grams::Bigram>(glob(dout, "bi.bin"), dsave + "/bi.bin");
}

KMerge<grams::Lem2AndWords, Lem2AndWordsMore>
extend_bigrams(const std::string &dsave,
               const std::vector<std::vector<u32>> &lems) {
  auto fn = [&](const grams::Bigram &bim, std::queue<grams::Lem2AndWords> &q) {
    const auto &prev = lems.at(bim.id1() - 1);
    const auto &cur = lems.at(bim.id2() - 1);
    for (auto lid1 : prev) {
      for (auto lid2 : cur) {
        grams::Lem2AndWords msg;
        msg.set_lid1(lid1);
        msg.set_lid2(lid2);
        msg.set_wid1(bim.id1());
        msg.set_wid2(bim.id2());
        msg.set_count(bim.weight());
        q.emplace(std::move(msg));
      }
    }
  };

  auto fbi = dsave + "/bi.bin";
  auto tr = Transformer<grams::Bigram, grams::Lem2AndWords>(fbi, fn);
  using sorter_t = ExternalSorter<decltype(tr), Lem2AndWordsMore>;
  auto sorter = sorter_t(dsave + "/extended2_parts", 80'000'000);
  auto merger = sorter.sort_unstable(tr);

  return merger;
}

auto build_lem_weights(const std::string &funi,
                       const std::vector<std::vector<u32>> &lems) {
  absl::flat_hash_map<u32, double> lid_w;
  auto fn = [&](grams::Unigram *m) {
    // для каждого лемматизированного слова подсчитывает и сохраняет вес
    const auto &terms = lems.at(m->id() - 1);
    auto w = static_cast<double>(m->weight()) / terms.size();
    // чем больше омонимов, тем меньше вес
    for (auto lid : terms) {
      auto p = lid_w.try_emplace(lid, 0);
      p.first->second += w;
    }
  };
  read_apply<grams::Unigram>(funi, fn);
  return lid_w;
}

void group_lem2(const std::string &dsave, double threshold) {
  auto lems = load_lems(dsave + "/lems.bin");
  auto lid_w = build_lem_weights(dsave + "/uni.bin", lems);
  auto merger = extend_bigrams(dsave, lems);

  // auto paths = glob(dsave + "/extended2_parts", ".bin");
  // auto merger = KMerge<grams::Lem2AndWords, Lem2AndWordsMore>(paths);

  OFStreamer<grams::Lem2Group> os(dsave + "/extended2.bin");
  grams::Lem2Group msg;

  double weight = 0;
  auto write_one = [&]() {
    if (msg.cases_size() == 0) {
      return;
    }

    auto it1 = lid_w.find(msg.lid1());
    auto it2 = lid_w.find(msg.lid2());

    if (it1->second == 0 || it2->second == 0) {
      msg.set_weight(0);
    } else {
      auto temp = lid_w.size() * (weight - threshold);
      msg.set_weight(std::max(0., (temp / it1->second) / it2->second));
    }

    if (msg.weight() > 0) {
      os.write(msg);
    }
    msg.Clear();
    weight = 0;
  };

  for (auto it = merger.begin(); it != merger.end(); ++it) {
    if (msg.lid1() != it->lid1() || msg.lid2() != it->lid2()) {
      write_one();
    }
    auto cs = msg.add_cases();
    cs->set_wid1(it->wid1());
    cs->set_wid2(it->wid2());
    cs->set_count(it->count());

    const auto &prev = lems.at(it->wid1() - 1);
    const auto &cur = lems.at(it->wid2() - 1);
    auto times = prev.size() * cur.size();
    weight += static_cast<double>(it->count()) / times;

    msg.set_lid1(it->lid1());
    msg.set_lid2(it->lid2());
  }

  // finally
  write_one();
}

void bifreq_stat(const std::string &dsave) {
  auto lems = load_lems(dsave + "lems.bin");
  absl::flat_hash_set<u32> uniset;
  absl::flat_hash_set<Idd> biset;
  absl::flat_hash_map<u32, u32> uni;
  absl::flat_hash_map<Idd, u32> bi;

  auto fnf = [&](grams::Lem2Group *m) {
    bi.try_emplace({m->lid1(), m->lid2()}, 0);
  };
  read_apply<grams::Lem2Group>(dsave + "/extended2.bin", fnf);

  u32 docid = 1;
  auto fn = [&](const Phrase::Reader &r) {
    const auto &ids = r.getIds();
    if (ids.size() == 0) { // end of document
      increment(uni, uniset);
      increment(bi, biset);
      if (docid % 100 == 0) {
        std::cout << "\r" << docid << ": " << uni.size() << " " << bi.size()
                  << std::flush;
      }
      docid++;
    }

    for (auto it = ids.begin(); it != ids.end(); ++it) {
      for (auto rid : lems.at(*it - 1)) {
        uniset.insert(rid);
        if (it == ids.begin()) {
          continue;
        }
        auto prev = it - 1;
        for (auto lid : lems.at(*prev - 1)) {
          auto p = std::make_pair(lid, rid);
          if (bi.find(p) != bi.end())
            biset.emplace(p);
        }
      }
    }
  };
  read_fn<Phrase>(dsave + "/corpus.bin", fn);

  // check validity
  for (const auto &el : bi) {
    if (el.second == 0)
      throw std::runtime_error("bi: doc count is zero");
  }

  save_bi(bi, dsave + "/bifreq.bin");
  merge_unifreq(uni, dsave);
}

// filter by docfreq
void filter_bilems(const std::string &dsave, u32 th1, double th2) {
  absl::flat_hash_map<Idd, u32> freqs;
  auto rbif = [&](grams::Bigram *m) {
    freqs.try_emplace({m->id1(), m->id2()}, m->weight());
  };
  read_apply<grams::Bigram>(dsave + "/bifreq.bin", rbif);

  using sorter_t = ExternalSorter<IFStreamer<grams::Lem2Group>,
                                  LemGroupLess<grams::Lem2Group>>;
  auto sorter = sorter_t(dsave + "/bifiltered_parts", 20'000'000);
  auto is = IFStreamer<grams::Lem2Group>(dsave + "/extended2.bin");
  auto merger = sorter.sort_unstable(is);

  OFStreamer<grams::Lem2Group> os(dsave + "/bifiltered.bin");
  for (auto it = merger.begin(); it != merger.end(); ++it) {
    auto fit = freqs.find({it->lid1(), it->lid2()});
    if (fit->second > th1 && it->weight() > th2) {
      os.write(*it);
    }
  }
}

/////////////////////////////////////////////////////////////////////////////
//                                                                         //
/////////////////////////////////////////////////////////////////////////////

absl::flat_hash_set<Idd> load_filtered_bigrams(const std::string &dsave) {
  absl::flat_hash_set<Idd> biwids;
  auto fn = [&](grams::Lem2Group *lg) {
    for (auto &cs : lg->cases()) {
      biwids.emplace(cs.wid1(), cs.wid2());
    }
  };
  read_apply<grams::Lem2Group>(dsave + "/bifiltered.bin", fn);
  return biwids;
}

// gramcat tri.bin | rg "( 4\t| 244\t| 28547\t)"
void trigram_stat(const std::string &dsave) {
  const auto biwids = load_filtered_bigrams(dsave);
  const auto dout = dsave + "/tri_parts/";
  system_exec("mkdir -p " + dout);

  absl::flat_hash_map<Iddd, u32> triples;
  u32 chunk = 1, docid = 1;
  auto save_chunk = [&]() {
    auto fout = dout + std::to_string(chunk) + "_tri.bin";
    save_tri(triples, fout);
    chunk++;
  };

  auto fn = [&](const Phrase::Reader &r) {
    const auto &wids = r.getIds();
    if (wids.size() == 0) { // end of document
      if (docid % 100 == 0) {
        std::cout << "\r" << docid << ": " << triples.size() << std::flush;
      }
      if (docid % 40'000 == 0) {
        save_chunk();
      }
      docid++;
    }

    bool found = false;
    for (auto it1 = wids.begin(), it2 = it1 + 1; it2 < wids.end();
         it1 = it2++) {
      if (biwids.find({*it1, *it2}) == biwids.end()) {
        found = false;
        continue;
      }
      if (not found && it1 != wids.begin()) {
        auto p = triples.try_emplace({*(it1 - 1), *it1, *it2}, 0);
        p.first->second++;
      }
      auto next = it2 + 1;
      if (next != wids.end()) {
        auto p = triples.try_emplace({*it1, *it2, *next}, 0);
        p.first->second++;
      }
      found = true;
    }
  };
  read_fn<Phrase>(dsave + "/corpus.bin", fn);

  save_chunk();
  merge_files<grams::Trigram>(glob(dout, "tri.bin"), dsave + "/tri.bin");
}

KMerge<grams::Lem3AndWords, Lem3AndWordsMore>
extend_trigrams(const std::string &dsave,
                const std::vector<std::vector<u32>> &lems) {
  auto fn = [&](const grams::Trigram &tim, std::queue<grams::Lem3AndWords> &q) {
    const auto &prev = lems.at(tim.id1() - 1);
    const auto &cur = lems.at(tim.id2() - 1);
    const auto &next = lems.at(tim.id3() - 1);
    for (auto lid1 : prev) {
      for (auto lid2 : cur) {
        for (auto lid3 : next) {
          grams::Lem3AndWords msg;
          msg.set_lid1(lid1);
          msg.set_lid2(lid2);
          msg.set_lid3(lid3);
          msg.set_wid1(tim.id1());
          msg.set_wid2(tim.id2());
          msg.set_wid3(tim.id3());
          msg.set_count(tim.weight());
          q.emplace(std::move(msg));
        }
      }
    }
  };

  auto fbi = dsave + "/tri.bin";
  auto tr = Transformer<grams::Trigram, grams::Lem3AndWords>(fbi, fn);
  using sorter_t = ExternalSorter<decltype(tr), Lem3AndWordsMore>;
  auto sorter = sorter_t(dsave + "/extended3_parts", 80'000'000);
  auto merger = sorter.sort_unstable(tr);

  return merger;
}

void group_lem3(const std::string &dsave, double threshold) {
  auto lems = load_lems(dsave + "/lems.bin");
  auto lid_w = build_lem_weights(dsave + "/uni.bin", lems);
  auto merger = extend_trigrams(dsave, lems);

  // auto paths = glob(dall + "/extended3_parts", ".bin");
  // auto merger = KMerge<grams::Lem3AndWords, Lem3AndWordsMore>(paths);

  OFStreamer<grams::Lem3Group> os(dsave + "/extended3.bin");
  grams::Lem3Group msg;

  double weight = 0;
  auto write_one = [&]() {
    if (msg.cases_size() == 0) {
      return;
    }

    auto it1 = lid_w.find(msg.lid1());
    auto it2 = lid_w.find(msg.lid2());
    auto it3 = lid_w.find(msg.lid3());

    if (it1->second == 0 || it2->second == 0 || it3->second == 0) {
      msg.set_weight(0);
    } else {
      auto temp = lid_w.size() * (weight - threshold);
      temp = (temp / it1->second) / it2->second;
      msg.set_weight(std::max(0., lid_w.size() * temp / it3->second));
    }

    if (msg.weight() > 0) {
      os.write(msg);
    }
    msg.Clear();
    weight = 0;
  };

  for (auto it = merger.begin(); it != merger.end(); ++it) {
    if (msg.lid1() != it->lid1() || msg.lid2() != it->lid2() ||
        msg.lid3() != it->lid3()) {
      write_one();
    }
    auto cs = msg.add_cases();
    cs->set_wid1(it->wid1());
    cs->set_wid2(it->wid2());
    cs->set_wid3(it->wid3());
    cs->set_count(it->count());

    const auto &prev = lems.at(it->wid1() - 1);
    const auto &cur = lems.at(it->wid2() - 1);
    const auto &next = lems.at(it->wid3() - 1);
    auto times = prev.size() * cur.size() * next.size();
    weight += static_cast<double>(it->count()) / times;

    msg.set_lid1(it->lid1());
    msg.set_lid2(it->lid2());
    msg.set_lid3(it->lid3());
  }

  // finally
  write_one();
}

absl::flat_hash_map<Iddd, u32> //
load_extended_trilems(const std::string &dsave) {
  absl::flat_hash_map<Iddd, u32> lids;
  auto fn = [&](grams::Lem3Group *lg) {
    lids.try_emplace(std::make_tuple(lg->lid1(), lg->lid2(), lg->lid3()), 0);
  };
  read_apply<grams::Lem3Group>(dsave + "/extended3.bin", fn);
  return lids;
}

void trifreq_stat(const std::string &dsave) {
  auto lems = load_lems(dsave + "lems.bin");
  absl::flat_hash_set<Iddd> triset;
  auto tri = load_extended_trilems(dsave);

  u32 docid = 1;
  auto fn = [&](const Phrase::Reader &r) {
    const auto &ids = r.getIds();
    if (ids.size() == 0) { // end of document
      increment(tri, triset);
      if (docid % 100 == 0) {
        std::cout << "\r" << docid << ": " << tri.size() << std::flush;
      }
      docid++;
    }

    for (auto lit = ids.begin(), cit = lit + 1, rit = cit + 1; rit < ids.end();
         lit = cit, cit = rit++) {
      for (auto lid : lems.at(*lit - 1)) {
        for (auto cid : lems.at(*cit - 1)) {
          for (auto rid : lems.at(*rit - 1)) {
            auto t = std::make_tuple(lid, cid, rid);
            if (tri.find(t) != tri.end()) {
              triset.emplace(t);
            }
          }
        }
      }
    }
  };
  read_fn<Phrase>(dsave + "/corpus.bin", fn);
  printf("\n");

  // check validity
  for (const auto &el : tri) {
    if (el.second == 0)
      throw std::runtime_error("tri: doc count is zero");
  }

  save_tri(tri, dsave + "/trifreq.bin");
}

void filter_trilems(const std::string &dsave, u32 th1, double th2) {
  absl::flat_hash_map<Iddd, u32> freqs;
  auto fn = [&](grams::Trigram *m) {
    freqs.try_emplace({m->id1(), m->id2(), m->id3()}, m->weight());
  };
  read_apply<grams::Trigram>(dsave + "/trifreq.bin", fn);

  std::vector<grams::Lem3Group> v;
  auto fn1 = [&](grams::Lem3Group *m) {
    auto fit = freqs.find({m->lid1(), m->lid2(), m->lid3()});
    if (fit->second > th1 && m->weight() > th2) {
      v.emplace_back(*m);
    }
  };
  read_apply<grams::Lem3Group>(dsave + "/extended3.bin", fn1);

  LemGroupLess<grams::Lem3Group> cmp;
  std::sort(v.begin(), v.end(), [&](auto &l, auto &r) { return cmp(r, l); });
  OFStreamer<grams::Lem3Group> os(dsave + "/trifiltered.bin");
  for (const auto &el : v) {
    os.write(el);
  }
}

} // namespace cllc
