#pragma once

#include "grams.pb.h"

namespace cllc {

/////////////////////////////////////////////////////////////////////////////
//                                 messages                                //
/////////////////////////////////////////////////////////////////////////////

struct BigramMore {
  inline bool operator()(const grams::Bigram &l, const grams::Bigram &r) const {
    return std::make_tuple(l.id1(), l.id2()) >
           std::make_tuple(r.id1(), r.id2());
  }
};

struct BigramEq {
  inline bool operator()(const grams::Bigram &l, const grams::Bigram &r) const {
    return (l.id1() == r.id1()) && (l.id2() == r.id2());
  }
};

struct TrigramMore {
  inline bool operator()(const grams::Trigram &l,
                         const grams::Trigram &r) const {
    return std::make_tuple(l.id1(), l.id2(), l.id3()) >
           std::make_tuple(r.id1(), r.id2(), r.id3());
  }
};

struct TrigramEq {
  inline bool operator()(const grams::Trigram &l,
                         const grams::Trigram &r) const {
    return (l.id1() == r.id1()) && (l.id2() == r.id2()) && (l.id3() == r.id3());
  }
};

struct Lem2AndWordsMore {
  inline bool operator()(const grams::Lem2AndWords &l,
                         const grams::Lem2AndWords &r) const {
    return std::make_tuple(l.lid1(), l.lid2()) >
           std::make_tuple(r.lid1(), r.lid2());
  }
};

struct Lem3AndWordsMore {
  inline bool operator()(const grams::Lem3AndWords &l,
                         const grams::Lem3AndWords &r) const {
    return std::make_tuple(l.lid1(), l.lid2(), l.lid3()) >
           std::make_tuple(r.lid1(), r.lid2(), r.lid3());
  }
};

template <class T> struct LemGroupLess {
  inline bool operator()(const T &l, const T &r) const {
    return l.weight() < r.weight();
  }
};

} // namespace cllc
