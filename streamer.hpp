//!
//! @file streamer.hpp
//! Сериализация/десериализация последовательных сообщений в формате protobuf на
//! диск/с диска
//!

#pragma once

#include <capnp/message.h>
#include <capnp/serialize-packed.h>
#include <cstdint>
#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/message.h>
#include <google/protobuf/util/delimited_message_util.h>
#include <queue>
#include <sstream>
#include <unistd.h>

#include "grams.capnp.h"
#include <grams.pb.h>

namespace cllc {

// потоковое чтение сообщений из protobuf файла
template <class M> class IFStreamer {
  static constexpr auto parse =
      google::protobuf::util::ParseDelimitedFromZeroCopyStream;
  using FileInputStream = google::protobuf::io::FileInputStream;
  int fd;
  std::unique_ptr<FileInputStream> stream;

public:
  using value_type = M;
  IFStreamer(const std::string &fname, std::uint64_t *total = nullptr)
      : fd{open(fname.c_str(), O_RDONLY)},
        stream{std::make_unique<FileInputStream>(fd)} {
    std::ostringstream ss;

    if (fd < 0) {
      ss << "could't open file " << fname << ", error: " << strerror(errno);
      throw std::runtime_error(ss.str());
    }

    grams::Header h;
    if (parse(&h, stream.get(), nullptr)) {
      if (h.msg_type() != M::GetDescriptor()->name()) {
        ss << fname << ": file type " << h.msg_type() << " does not match "
           << M::GetDescriptor()->name();
        throw std::runtime_error(ss.str());
      }
    } else {
      ss << fname << ": could't read file header";
      throw std::runtime_error(ss.str());
    }

    if (total != nullptr)
      *total = h.total();
  }

  IFStreamer() = delete;
  IFStreamer(const IFStreamer &) = delete;
  IFStreamer(IFStreamer &&rhs) : fd{rhs.fd}, stream{std::move(rhs.stream)} {}

  ~IFStreamer() { Close(); }

  bool read(M &msg) { return parse(&msg, stream.get(), nullptr); }
  void Close() {
    if (stream != nullptr) {
      stream->Close();
      stream = nullptr;
      close(fd);
    }
  }
};

// потоковая запись сообщений в protobuf файл
template <class M> class OFStreamer {
  static constexpr auto serialize =
      google::protobuf::util::SerializeDelimitedToZeroCopyStream;
  using FileOutputStream = google::protobuf::io::FileOutputStream;
  int fd;
  std::unique_ptr<FileOutputStream> stream;
  std::string fname;

public:
  using value_type = M;

  OFStreamer(const std::string &fname, std::uint64_t total = 0)
      : fd{open(fname.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600)},
        stream{std::make_unique<FileOutputStream>(fd)}, fname{fname} {
    std::ostringstream ss;
    if (fd < 0) {
      ss << "could't open file " << fname << ", error: " << strerror(errno);
      throw std::runtime_error(ss.str());
    }

    grams::Header h;
    h.set_msg_type(M::GetDescriptor()->name());
    h.set_total(total);

    if (!serialize(h, stream.get())) {
      ss << fname << ": header writing failed";
      throw std::runtime_error(ss.str());
    }
  }

  OFStreamer() = delete;
  OFStreamer(const OFStreamer &) = delete;
  OFStreamer(OFStreamer &&rhs) : fd{rhs.fd}, stream{std::move(rhs.stream)} {}
  OFStreamer &operator=(OFStreamer &&rhs) {
    fd = rhs.fd;
    fname = std::move(fname);
    stream = std::move(rhs.stream);
    return *this;
  }

  ~OFStreamer() { Close(); }

  void write(const M &msg) {
    if (!serialize(msg, stream.get())) {
      std::ostringstream ss;
      ss << fname << ": writing failed";
      throw std::runtime_error(ss.str());
    }
  }

  void Close() {
    if (stream != nullptr) {
      stream->Close();
      stream = nullptr;
      close(fd);
    }
  }
};

// преобразует входящий поток в другой входящий, добавляя в него элементы
// функцией fn(), число элементов в обоих потоках и типы элементов могут
// отличаться
template <class I, class O> class Transformer {
  std::queue<O> q;
  bool keep = true;
  std::unique_ptr<IFStreamer<I>> is;
  std::function<void(const I &, std::queue<O> &)> fn;

public:
  using value_type = O;

  Transformer(std::unique_ptr<IFStreamer<I>> is,
              std::function<void(const I &, std::queue<O> &q)> fn)
      : is{std::move(is)}, fn{fn} {}

  Transformer(const std::string &fname,
              std::function<void(const I &, std::queue<O> &q)> fn)
      : is{std::make_unique<IFStreamer<I>>(fname)}, fn{fn} {}

  bool read(O &msg) {
    if (q.empty() && keep) {
      I bim;
      keep = is->read(bim);
      if (keep) {
        fn(bim, q);
      }
    }

    if (!q.empty()) {
      msg = std::move(q.front());
      q.pop();
      return true;
    }
    return false;
  }
};

template <class M> size_t read_total(const std::string &fname) {
  size_t total = 0;
  IFStreamer<M> is(fname, &total);
  return total;
}

template <class M, class F> void read_apply(const std::string &fname, F fn) {
  IFStreamer<M> is(fname);
  bool keep = true;
  M msg;
  while (keep) {
    msg.Clear();
    keep = is.read(msg);
    if (keep)
      fn(&msg);
  }
}

template <class M, class F> void read_fn(const std::string &fname, F fn) {
  int fd = open(fname.c_str(), O_RDONLY); // need RAII

  if (fd < 0) {
    std::ostringstream ss;
    ss << "could't open file " << fname << ", error: " << strerror(errno);
    throw std::runtime_error(ss.str());
  }

  kj::FdInputStream fdStream(fd);
  kj::BufferedInputStreamWrapper bufferedStream(fdStream);
  while (bufferedStream.tryGetReadBuffer() != nullptr) {
    capnp::PackedMessageReader reader(bufferedStream);
    fn(reader.getRoot<M>());
  }

  close(fd);
}

} // namespace cllc
