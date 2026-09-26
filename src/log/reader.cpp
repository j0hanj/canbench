#include "log/reader.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>

#include "log/asc.hpp"

namespace canbench {

namespace {

bool ends_with_ci(const std::string& s, std::string_view suffix) {
  if (s.size() < suffix.size()) return false;
  return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin(), [](char a, char b) {
    return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
  });
}

}  // namespace

LogFormat guess_format(const std::string& path) {
  return ends_with_ci(path, ".asc") ? LogFormat::kAsc : LogFormat::kCandump;
}

std::optional<LogFile> read_any_log(const std::string& path) {
  return guess_format(path) == LogFormat::kAsc ? read_asc(path) : read_log(path);
}

}  // namespace canbench
