#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/str_cat.h>
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <zlib/contrib/minizip/unzip.h>

#include "../colloc.hpp"
#include "../streamer.hpp"
#include "../tools.hpp"
#include "grams.pb.h"
#include "moonycode/codes.h"
#include "mtc/zmap.h"

namespace cllc {

void system_exec(const std::string &command) {
  std::ostringstream ss;
  int status = system(command.c_str());
  if (status < 0) {
    ss << "Error: " << strerror(errno);
    throw std::runtime_error(ss.str());
  } else if (!WIFEXITED(status)) {
    ss << "Error " << command;
    throw std::runtime_error(ss.str());
  }
}

void listFiles(const std::string &path,
               std::function<void(const std::string &)> fn, size_t from,
               size_t limit, const std::string &ending) {

  auto hasEnding = [](std::string const &fullString,
                      std::string const &ending) {
    if (fullString.length() >= ending.length()) {
      return (0 == fullString.compare(fullString.length() - ending.length(),
                                      ending.length(), ending));
    } else {
      return false;
    }
  };

  size_t counter = 0;
  const std::function<void(const std::string &)> loop_dir =
      [&](const std::string &cur_path) {
        if (auto dir = opendir(cur_path.c_str())) {
          while (auto f = readdir(dir)) {
            if (f->d_name[0] == '.')
              continue;
            if (f->d_type == DT_DIR || f->d_type == DT_UNKNOWN)
              loop_dir(cur_path + f->d_name + "/");
            if (f->d_type == DT_REG || f->d_type == DT_UNKNOWN) {
              auto fname = cur_path + f->d_name;
              if (!hasEnding(fname, ending))
                continue;
              counter++;
              if (counter <= from) {
                continue;
              }
              if (limit && counter > from + limit) {
                closedir(dir);
                return;
              }
              fn(fname);
            }
          }
          closedir(dir);
        }
      };

  loop_dir(path);
}

std::vector<std::string> glob(const std::string &dir,
                              const std::string &ending) {
  std::vector<std::string> files;
  listFiles(
      dir + "/",
      [&](const std::string &fname) { //
        files.push_back(fname);
      },
      0, 0, ending);
  return files;
}

class zipfile {
  unzFile handle;

  zipfile(const zipfile &) = delete;

public:
  zipfile(unzFile h = nullptr) : handle(h) {}
  zipfile(zipfile &&z) : handle(z.handle) { z.handle = nullptr; }
  zipfile &operator=(zipfile &&z) {
    if (handle != nullptr)
      unzClose(handle);
    if ((handle = z.handle) != nullptr)
      z.handle = nullptr;
    return *this;
  }
  ~zipfile() {
    if (handle != nullptr)
      unzClose(handle);
  }
  auto get() const -> unzFile { return handle; }
  bool operator==(std::nullptr_t p) const { return handle == p; }
  bool operator!=(std::nullptr_t p) const { return !(*this == p); }
};

auto GetDocsContents(const std::string &fin) -> std::vector<std::vector<char>> {
  zipfile srczip = unzOpen(fin.c_str());
  unz_global_info global;
  std::vector<std::vector<char>> buffers;

  if (srczip == nullptr)
    throw std::invalid_argument("zip file passed was not opened");

  if (unzGetGlobalInfo(srczip.get(), &global) != UNZ_OK)
    throw std::invalid_argument("could not read file global info");

  // file list loop
  for (auto i = 0; i != global.number_entry; ++i) {
    unz_file_info f_info;
    char f_name[1024];

    if (unzGetCurrentFileInfo(srczip.get(), &f_info, f_name, sizeof(f_name),
                              NULL, 0, NULL, 0) != UNZ_OK)
      throw std::invalid_argument("could not read file info");

    if (unzOpenCurrentFile(srczip.get()) != UNZ_OK)
      throw std::invalid_argument("could not open compressed file");

    std::vector<char> buff;
    for (;;) {
      char rdbuff[8192];
      auto cbread = unzReadCurrentFile(srczip.get(), rdbuff, sizeof(rdbuff));

      if (cbread < 0)
        throw std::invalid_argument("error decompressing zip");

      if (cbread > 0)
        buff.insert(buff.end(), rdbuff, rdbuff + cbread);
      else
        break;
    }

    buffers.emplace_back(std::move(buff));
    unzCloseCurrentFile(srczip.get());
    unzGoToNextFile(srczip.get());
  }

  return std::move(buffers);
}

Baalbek::document LoadXml(const char *xml, size_t len) {
  Baalbek::document doc;

  format::xml()
      .set_codepage(codepages::codepage_utf8)
      .set_map_tags([&](const Baalbek::document::mkup::key &key)
                        -> Baalbek::document::mkup::key {
        std::string str(key.get_charstr());

        return Baalbek::document::mkup::key(0U);
      })
      .block_output({"*.a", "*.a.*", "*.xmlns", "*.xmlns:*", "*.image.*",
                     "*.binary", "*.binary.*", "binary",
                     "FictionBook.description", "FictionBook.xmlns*", "*image",
                     "*image.*"})
      .Load(doc, xml, len);

  return std::move(doc);
}

void to_zmap(const std::string &dsave, const std::string &version) {
  absl::flat_hash_map<u32, std::string> uni, unilem;

  absl::flat_hash_map<Idd, u32> bicnt;
  auto fnb = [&](grams::Bigram *m) {
    bicnt.try_emplace({m->id1(), m->id2()}, m->weight());
  };
  read_apply<grams::Bigram>(dsave + "/bifreq.bin", fnb);

  // (lid1, lid2) -> (doc_count, (wid1, wid2))
  absl::flat_hash_map<Idd, std::pair<u32, Idd>> bifreqs;
  auto fn1 = [&](grams::Lem2Group *lg) {
    unilem.try_emplace(lg->lid1(), "");
    unilem.try_emplace(lg->lid2(), "");
    auto cs = lg->cases();
    auto mit = std::max_element(cs.begin(), cs.end(), [](auto &l, auto &r) {
      return l.count() < r.count();
    });
    uni.try_emplace(mit->wid1(), "");
    uni.try_emplace(mit->wid2(), "");
    auto p = std::make_pair(lg->lid1(), lg->lid2());
    auto it = bicnt.find(p);
    bifreqs.try_emplace(
        std::make_pair(lg->lid1(), lg->lid2()),
        std::make_pair(it->second, std::make_pair(mit->wid1(), mit->wid2())));
  };
  read_apply<grams::Lem2Group>(dsave + "/bifiltered.bin", fn1);

  absl::flat_hash_map<Iddd, u32> tricnt;
  auto fnt = [&](grams::Trigram *m) {
    tricnt.try_emplace({m->id1(), m->id2(), m->id3()}, m->weight());
  };
  read_apply<grams::Trigram>(dsave + "/trifreq.bin", fnt);

  // (lid1, lid2, lid3) -> (doc_count, (wid1, wid2, wid3))
  absl::flat_hash_map<Iddd, std::pair<u32, Iddd>> trifreqs;
  auto fn2 = [&](grams::Lem3Group *lg) {
    unilem.try_emplace(lg->lid1(), "");
    unilem.try_emplace(lg->lid2(), "");
    unilem.try_emplace(lg->lid3(), "");
    auto cs = lg->cases();
    auto mit = std::max_element(cs.begin(), cs.end(), [](auto &l, auto &r) {
      return l.count() < r.count();
    });
    uni.try_emplace(mit->wid1(), "");
    uni.try_emplace(mit->wid2(), "");
    uni.try_emplace(mit->wid3(), "");
    auto mids = std::make_tuple(mit->wid1(), mit->wid2(), mit->wid3());
    auto t = std::make_tuple(lg->lid1(), lg->lid2(), lg->lid3());
    auto it = tricnt.find(t);
    trifreqs.try_emplace(t, std::make_pair(it->second, std::move(mids)));
  };
  read_apply<grams::Lem3Group>(dsave + "/trifiltered.bin", fn2);

  auto set_uni = [&](const grams::Unigram *msg) {
    auto it = uni.find(msg->id());
    if (it != uni.end())
      it->second = msg->str();
  };
  cllc::read_apply<grams::Unigram>(dsave + "/uni.bin", set_uni);

  auto set_unilem = [&](const grams::LemId *msg) {
    auto it = unilem.find(msg->id());
    if (it != unilem.end())
      it->second = msg->str();
  };
  cllc::read_apply<grams::LemId>(dsave + "/lemid.bin", set_unilem);

  /////////////////////////сохраним результат/////////////////////////////

  auto termstat = mtc::zmap();
  u32 nuni = 0, nbi = 0;

  auto fn = [&](const grams::LemFreq *msg) {
    if (msg->weight() > 2) {
      auto val = mtc::zmap();
      val.set_word32("dc", msg->weight());
      termstat.set_zmap(msg->str(), val);
      nuni++;
    }
  };
  read_apply<grams::LemFreq>(dsave + "/lemfreq.bin", fn);

  for (const auto &el : bifreqs) {
    auto it1 = unilem.find(el.first.first);
    auto it2 = unilem.find(el.first.second);
    auto key = absl::StrCat(it1->second, it2->second);

    auto val = mtc::zmap();
    val.set_word32("dc", el.second.first);
    auto &wids = el.second.second;
    auto it3 = uni.find(wids.first);
    auto it4 = uni.find(wids.second);

    auto w1 = codepages::mbcstowide(codepages::codepage_utf8,
                                    it3->second.data(), it3->second.size());
    auto w2 = codepages::mbcstowide(codepages::codepage_utf8,
                                    it4->second.data(), it4->second.size());
    if (w1.length() < 3 || w2.length() < 3) {
      // printf("%s %s\n", it3->second.data(), it4->second.data());
      continue;
    }

    auto txt = absl::StrCat(it3->second, " ", it4->second);
    val.set_charstr("txt", std::move(txt));

    if (it1->second.empty() || it2->second.empty() || it3->second.empty() ||
        it4->second.empty() || el.second.first == 0) {
      throw std::runtime_error("bi: wrong data values");
    }

    termstat.set_zmap(key, val);
    nbi++;
  }

  for (const auto &el : trifreqs) {
    auto &t = el.first;
    auto it1 = unilem.find(std::get<0>(t));
    auto it2 = unilem.find(std::get<1>(t));
    auto it3 = unilem.find(std::get<2>(t));
    auto key = absl::StrCat(it1->second, it2->second, it3->second);

    auto val = mtc::zmap();
    val.set_word32("dc", el.second.first);
    auto &wids = el.second.second;
    auto it4 = uni.find(std::get<0>(wids));
    auto it5 = uni.find(std::get<1>(wids));
    auto it6 = uni.find(std::get<2>(wids));
    auto txt = absl::StrCat(it4->second, " ", it5->second, " ", it6->second);
    val.set_charstr("txt", std::move(txt));

    if (it1->second.empty() || it2->second.empty() || it3->second.empty() ||
        it4->second.empty() || it5->second.empty() || it6->second.empty() ||
        el.second.first == 0) {
      printf("%u %u %u\n", std::get<0>(t), std::get<1>(t), std::get<2>(t));
      std::printf("1.%lu 2.%u 3.%s 4.%s 5.%s\n", key.size(), el.second.first,
                  it4->second.data(), it5->second.data(), it6->second.data());
      throw std::runtime_error("tri: wrong data values");
    }

    termstat.set_zmap(key, val);
  }

  // load total number of documents
  std::ifstream total_count_file;
  total_count_file.open(dsave + "/total_count.txt");
  size_t total_count = 0;
  if (total_count_file.is_open()) {
    total_count_file >> total_count;
    total_count_file.close();
  } else {
    std::stringstream ss;
    ss << "could't open " << dsave + "/total_count.txt";
    throw std::runtime_error(ss.str());
  }

  termstat.set_charstr("version", version);
  termstat.set_int64("total_count", total_count); // 345272
  FILE *output = fopen((dsave + "/stat_" + version + ".map").c_str(), "wb");

  ::Serialize(output, "*** Global term statistics ***", 30);
  termstat.Serialize(output);
  fclose(output);

  printf("uni: %u bi: %u tri: %lu\n", nuni, nbi, trifreqs.size());
}

std::string get_data_type(const char *fname) {
  int fd = open(fname, O_RDONLY);

  std::ostringstream ss;
  if (fd < 0) {
    ss << "could't open file " << fname << ", error: " << strerror(errno);
    throw std::runtime_error(ss.str());
  }

  google::protobuf::io::FileInputStream fin(fd);

  auto parse = google::protobuf::util::ParseDelimitedFromZeroCopyStream;

  grams::Header h;
  bool keep = parse(&h, &fin, nullptr);

  fin.Close();
  close(fd);

  if (keep)
    return h.msg_type();
  else
    return "";
}

} // namespace cllc
