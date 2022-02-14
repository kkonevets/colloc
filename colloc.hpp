#pragma once

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/strings/string_view.h>
#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <cstdlib>
#include <kj/io.h>
#include <string>

#include "baalbek/babylon/document.hpp"
#include "baalbek/babylon/languages/rus.hpp"
#include "baalbek/babylon/lingproc.hpp"

#include "grams.capnp.h"
#include "grams.pb.h"
#include "streamer.hpp"

using u32 = std::uint32_t;

namespace cllc {

using Idd = std::pair<u32, u32>;
using Iddd = std::tuple<u32, u32, u32>;

// идентификатор слова, счетчик его встечи, в скольких документах встретилось
template <class T> struct UniVal {
  u32 id = 0;
  T weight = 0;
  explicit UniVal(u32 id = 0, T weight = 0) : id{id}, weight{weight} {}
};

struct UnigramCounts : public absl::flat_hash_map<std::string, UniVal<u32>> {
  using msg_type = grams::Unigram;
  // вспомогательный вектор
  std::vector<widechar> wlower_;
  int fd;
  std::unique_ptr<kj::FdOutputStream> fdStream;
  std::unique_ptr<kj::BufferedOutputStreamWrapper> bufferedOut;

  UnigramCounts(const std::string &dsave);
  absl::optional<u32> update_word(const Baalbek::language::word &w);
  bool update(const Baalbek::language::docimage &doci);
  ~UnigramCounts() {
    bufferedOut->flush();
    close(fd);
  }
};

struct Lemmer {
  std::vector<std::vector<u32>> lems;
  absl::flat_hash_map<std::string, u32> lemid;

  void lemmatize();
  void save(const std::string &dsave);
};

template <class T>
static void increment(absl::flat_hash_map<T, u32> &m,
                      absl::flat_hash_set<T> &from) {
  for (auto &el : from) {
    auto p = m.try_emplace(el, 0);
    p.first->second++;
  }
  from.clear();
}

template <class T> static auto sort_map(const absl::flat_hash_map<T, u32> &m) {
  std::vector<std::pair<T, u32>> v{m.begin(), m.end()};
  std::sort(v.begin(), v.end());
  return v;
}

void save_uni(const UnigramCounts &uni, const std::string &fout);
void save_bi(absl::flat_hash_map<Idd, u32> &bi, const std::string &fout);
void save_tri(absl::flat_hash_map<Iddd, u32> &tri, const std::string &fout);

// Обрабатывает файлы в папке dcorpus и сохраняет результат в
// dsave, файлы берутся с порядкового номера from в количестве limit.
// Результатом является набор предложений, каждое слово которого это
// идентификатор, также рядом сохраняется соответствие {слово: идентификатор}
void convert(const std::string &dcorpus, const std::string &dsave, size_t from,
             size_t limit);

// Читает все уникальные слова из dsave, лемматизирует и сохраняет в dsave
// результат
void lemmatize(const std::string &dsave);

std::vector<std::vector<u32>> load_lems(const std::string &fname);

// Выделяет статистику по парам слов (не лемм!), подсчитывает частоты этих
// биграмм, делает это по кускам, которые помещаются в RAM
void bigram_stat(const std::string &dsave);
// Группирует биграммы по идентификаторам лемм. Делает это по кускам, сортирует
// их, а затем сливает в один файл. При слиянии отбирает те записи, у которых
// совместная частота встечи  wij лемм превышает порог, совместная частота
// рассчитывается с учетом омонимии (см. example.txt). При этом, попутно
// вычисляются вероятности совместной встречи лемм по формуле (Wij -
// threshold)/(Wi*Wj).
void group_lem2(const std::string &dsave, double threshold);
// Подсчитывает статистику по парам лемм по документам. Одна лемма считается
// один раз в одном документе. При этом опять пробигается по всему корпусу.
void bifreq_stat(const std::string &dsave);
// Отбирает те пары лемм, которые встречаются больше чем в th1 документах и у
// которых вероятностный порог больше th2
void filter_bilems(const std::string &dsave, u32 th1, double th2);

void trigram_stat(const std::string &dsave);
void group_lem3(const std::string &dsave, double threshold);
void trifreq_stat(const std::string &dsave);
void filter_trilems(const std::string &dsave, u32 th1, double th2);

} // namespace cllc
