#include "log/asc.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace canbench {

namespace {

constexpr std::uint32_t kStdIdMax = 0x7FF;
constexpr std::uint32_t kExtIdMax = 0x1FFFFFFF;

std::string_view trim(std::string_view s) {
  auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && ws(s.front())) s.remove_prefix(1);
  while (!s.empty() && ws(s.back())) s.remove_suffix(1);
  return s;
}

std::vector<std::string_view> split(std::string_view line) {
  std::vector<std::string_view> toks;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    std::size_t start = i;
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i > start) toks.push_back(line.substr(start, i - start));
  }
  return toks;
}

std::optional<double> parse_num(std::string_view s) {
  std::string buf(s);
  char* end = nullptr;
  double v = std::strtod(buf.c_str(), &end);
  if (end != buf.c_str() + buf.size() || buf.empty()) return std::nullopt;
  return v;
}

std::optional<std::uint32_t> parse_hex(std::string_view s) {
  if (s.empty() || s.size() > 8) return std::nullopt;
  std::uint32_t v = 0;
  for (char c : s) {
    unsigned char uc = static_cast<unsigned char>(c);
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (std::tolower(uc) >= 'a' && std::tolower(uc) <= 'f') d = std::tolower(uc) - 'a' + 10;
    else return std::nullopt;
    v = (v << 4) | static_cast<std::uint32_t>(d);
  }
  return v;
}

std::optional<int> parse_int(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::string buf(s);
  char* end = nullptr;
  long v = std::strtol(buf.c_str(), &end, 10);
  if (end != buf.c_str() + buf.size()) return std::nullopt;
  return static_cast<int>(v);
}

}  // namespace

LogFile parse_asc(std::string_view text) {
  LogFile out;
  std::size_t lineno = 0;
  std::size_t pos = 0;

  while (pos <= text.size()) {
    auto nl = text.find('\n', pos);
    std::string_view raw =
        text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
    ++lineno;

    std::string_view line = trim(raw);
    if (line.empty()) continue;

    auto toks = split(line);
    auto ts = parse_num(toks[0]);
    if (!ts) continue;  // not a frame line - header, comment, triggerblock noise

    auto fail = [&](const char* why) {
      out.errors.push_back({lineno, std::string(line), why});
    };

    if (toks.size() < 5) { fail("too few fields for a frame line"); continue; }

    std::string_view channel = toks[1];
    std::string_view id_tok = toks[2];
    bool extended = false;
    if (id_tok.size() > 1 && (id_tok.back() == 'x' || id_tok.back() == 'X')) {
      extended = true;
      id_tok.remove_suffix(1);
    }
    auto id = parse_hex(id_tok);
    if (!id) { fail("bad id"); continue; }
    if (*id > (extended ? kExtIdMax : kStdIdMax)) { fail("id out of range for its width"); continue; }

    std::string_view kind = toks[4];

    Frame f;
    f.id = *id;
    f.extended = extended;

    if (kind == "r" || kind == "R") {
      f.rtr = true;
      if (toks.size() >= 6) {
        auto dlc = parse_int(toks[5]);
        if (dlc && *dlc >= 0 && *dlc <= kMaxData) f.dlc = static_cast<std::uint8_t>(*dlc);
      }
    } else if (kind == "d" || kind == "D") {
      if (toks.size() < 6) { fail("data frame with no dlc"); continue; }
      auto dlc = parse_int(toks[5]);
      if (!dlc || *dlc < 0 || *dlc > kMaxData) { fail("bad dlc"); continue; }
      if (toks.size() < 6u + static_cast<std::size_t>(*dlc)) { fail("not enough data bytes"); continue; }
      f.dlc = static_cast<std::uint8_t>(*dlc);
      bool bad_byte = false;
      for (int i = 0; i < *dlc; ++i) {
        auto b = parse_hex(toks[6 + i]);
        if (!b || *b > 0xFF) { bad_byte = true; break; }
        f.data[i] = static_cast<std::uint8_t>(*b);
      }
      if (bad_byte) { fail("bad data byte"); continue; }
    } else {
      fail("expected 'd' or 'r'");
      continue;
    }

    out.entries.push_back({*ts, std::string(channel), f});
  }

  return out;
}

std::optional<LogFile> read_asc(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_asc(ss.str());
}

}  // namespace canbench
