//!
//! @file kmerge.hpp
//! Merge sorted protobuf files
//!

#pragma once
#ifndef INCLUDE_KMERGE_HPP_
#define INCLUDE_KMERGE_HPP_

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <queue>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "compare.hpp"
#include "grams.pb.h"
#include "streamer.hpp"
#include "tools.hpp"

namespace cllc {

/** @class KmergeIteratorSentinel
 *
 * Helper class indicating end of stream
 */
class KmergeIteratorSentinel {};

/** @class KMergeIterator
 *
 * An iterator doing actual work for merging files using priority queue.
 * @param readers File streams to merge
 */
template <class M, class Compare> class KMergeIterator {
  using _t = typename std::pair<M, size_t>;
  using value_type = M;

  struct QCmp {
    Compare comp{};
    inline bool operator()(const _t &l, const _t &r) {
      return comp(l.first, r.first);
    }
  };

  std::priority_queue<_t, std::vector<_t>, QCmp> q{QCmp()};
  std::vector<IFStreamer<M>> &readers;
  bool good;
  M msg;

  auto init_queue() -> bool {
    for (size_t i = 0; i < readers.size(); ++i) {
      auto &r = readers.at(i);
      if (r.read(msg)) {
        q.emplace(std::move(msg), i);
      }
    }

    return q.empty();
  }

public:
  explicit KMergeIterator(std::vector<IFStreamer<M>> &readers)
      : readers{readers} {
    good = !init_queue();
  }

  auto operator*() -> M & { return const_cast<M &>(q.top().first); }
  auto operator->() -> M * { return &const_cast<M &>(q.top().first); }

  auto operator++() -> KMergeIterator & {
    auto i = q.top().second;
    auto &r = readers.at(i);
    q.pop();
    if (r.read(msg)) {
      q.emplace(std::move(msg), i);
    }

    good = !q.empty() || !init_queue();

    return *this;
  }

  auto operator!=(const KmergeIteratorSentinel /*unused*/) const -> bool {
    return good;
  }
};

/** @class KMerge
 *
 * Iterator that merges multiple sorted iterators. Uses priority queue
 * for merging
 */
template <class M, class Compare> class KMerge {
  std::vector<IFStreamer<M>> readers;

public:
  explicit KMerge(const std::vector<std::string> &fnames) {
    for (const auto &fname : fnames) {
      readers.emplace_back(fname);
    }
  }

  auto begin() -> KMergeIterator<M, Compare> {
    return KMergeIterator<M, Compare>{readers};
  }
  auto end() -> KmergeIteratorSentinel { return {}; }
};

// ----------------------------------------------------------------------------
// ExternalSorter
// ----------------------------------------------------------------------------

/** @class ExternalSorter
 *
 *  Sorts file on disk using merge sort.
 *  First it splits file on parts, then sorts them (consuming `max_elems` at a
 *  time), saves parts on disk to `save_dir` and then merges those parts while
 *  lazy loading. Uses priority queue for merging (memory consumption is
 *  minimal).
 *
 *  @param save_dir Name of a directory to save parts in
 *  @param max_elems Maximum number of messages in a part file. The more
 *  memory is available the faster sorting. Default is 0, means unlimited
 */
template <class S, class Compare> class ExternalSorter {
  const std::string save_dir;
  std::size_t max_elems;
  unsigned int nChunks;

  using M = typename S::value_type;

  auto file_name(unsigned int n) -> std::string {
    return save_dir + "/" + std::to_string(n) + ".bin";
  }

  void sort_save(std::vector<M> &buf) {
    Compare cmp;
    std::sort(buf.begin(), buf.end(),
              [cmp](const M &a, const M &b) { return cmp(b, a); });

    auto fout = file_name(nChunks++);
    OFStreamer<M> os(fout, buf.size());
    for (const auto &msg : buf) {
      os.write(msg);
    }
  }

public:
  explicit ExternalSorter(const std::string &save_dir, size_t max_elems = 0)
      : save_dir(save_dir), max_elems(max_elems), nChunks(0) {
    system_exec("mkdir -p " + save_dir);
    system_exec("rm -rf " + save_dir + "/*");
  }

  /** @fn sort_unstable
   *
   *  @brief Sorts input stream
   *  @param is Input stream (e.g. file)
   *  @return A merging iterator that lazily loads data from sorted files
   */
  auto sort_unstable(S &is) -> KMerge<M, Compare> {
    std::vector<M> buf;
    buf.reserve(max_elems);
    bool keep = true;
    M msg;
    while (keep) {
      keep = is.read(msg);
      if (keep) {
        buf.emplace_back(std::move(msg));
        if (buf.size() == max_elems) {
          sort_save(buf);
          buf.clear();
        }
      }
    }

    if (!buf.empty()) {
      sort_save(buf);
    }

    std::vector<M>().swap(buf); // free memory

    std::vector<std::string> paths;
    for (size_t i = 0; i < nChunks; ++i) {
      paths.emplace_back(file_name(i));
    }

    return KMerge<M, Compare>(paths);
  }
};

template <class M, class Compare, class Eq>
void groupby_save(cllc::KMerge<M, Compare> &merger, const std::string &fout) {
  cllc::OFStreamer<M> os(fout);
  M prev;
  bool is_start = true;
  Eq iseq;
  for (auto it = merger.begin(); it != merger.end(); ++it) {
    if (is_start) {
      is_start = false;
      prev = std::move(*it);
    } else {
      if (iseq(*it, prev)) {
        prev.set_weight(it->weight() + prev.weight());
      } else {
        os.write(prev);
        prev = std::move(*it);
      }
    }
  }

  if (!is_start) // last one
    os.write(prev);
}

template <class M, class Compare, class Eq>
void merge_groupby_save(const std::vector<std::string> &paths,
                        const std::string &fout) {
  auto merger = cllc::KMerge<M, Compare>(paths);
  groupby_save<M, Compare, Eq>(merger, fout);
}

template <class M>
void merge_files(const std::vector<std::string> &paths,
                 const std::string &fout);

template <>
inline void merge_files<grams::Bigram>(const std::vector<std::string> &paths,
                                       const std::string &fout) {
  merge_groupby_save<grams::Bigram, BigramMore, BigramEq>(paths, fout);
}

template <>
inline void merge_files<grams::Trigram>(const std::vector<std::string> &paths,
                                        const std::string &fout) {
  merge_groupby_save<grams::Trigram, TrigramMore, TrigramEq>(paths, fout);
}

} // namespace cllc

#endif // INCLUDE_KMERGE_HPP_
