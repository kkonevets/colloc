#pragma once

#include <absl/strings/string_view.h>
#include <dirent.h>
#include <fnmatch.h>
#include <mail-search/tests/fant-client/zipfile.hpp>

#include "baalbek/babylon/lingproc.hpp"
#include "baalbek/xmltool/xml.hpp"

namespace cllc {

// выполняет системную команду
void system_exec(const std::string &command);

// печатает лемму в шестнадцатеричном формате
inline void print_term(absl::string_view term, int width = 50) {
  for (auto p : term) {
    printf("%02x", (unsigned char)p);
    width -= 2;
  }
  printf("%*s", width, " ");
}

// пепебирает рекупчивно файлы в директории @path, применяя к ним функцию @fn,
// файлы выбираются по порядку с @from в количестве @limit (если limit=0, значит
// выбираются все файлы начиная с @from), выбираются только файлы,
// окончивающиеся на @ending
void listFiles(const std::string &path,
               std::function<void(const std::string &)> fn, size_t from = 0,
               size_t limit = 0, const std::string &ending = ".zip");

// вызывает listFiles и возвращает список найденных файлов
std::vector<std::string> glob(const std::string &dir,
                              const std::string &ending);

Baalbek::document LoadXml(const char *xml, size_t len);

void to_zmap(const std::string &dsave, const std::string &version);
auto GetDocsContents(const std::string &fname)
    -> std::vector<std::vector<char>>;

std::string get_data_type(const char *fname);

} // namespace cllc
