#include "log/log.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace canbench {

namespace {

std::string_view trim(std::string_view s) {
  auto is_ws = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && is_ws(s.front())) s.remove_prefix(1);
  while (!s.empty() && is_ws(s.back())) s.remove_suffix(1);
  return s;
}

// pull the three whitespace-separated chunks out of a line.
// returns false if there aren't exactly enough of them.
bool split3(std::string_view line, std::string_view& a, std::string_view& b,
           std::string_view& c) {
  auto next = [&](std::string_view& rest) -> std::string_view {
    rest = trim(rest);
    auto sp = rest.find_first_of(" \t");
    std::string_view tok = rest.substr(0, sp);
    rest = (sp == std::string_view::npos) ? std::string_view{} : rest.substr(sp);
    return tok;
  };
  std::string_view rest = line;
  a = next(rest);
  b = next(rest);
  c = trim(rest);  // frame token is the whole tail, no spaces expected in it
  return !a.empty() && !b.empty() && !c.empty();
}

// "(1650000000.123456)" -> 1650000000.123456
std::optional<double> parse_ts(std::string_view tok) {
  if (tok.size() < 3 || tok.front() != '(' || tok.back() != ')')
    return std::nullopt;
  tok.remove_prefix(1);
  tok.remove_suffix(1);
  // want a plain decimal number, no junk. strtod tells us where it stopped.
  std::string buf(tok);
  char* end = nullptr;
  double v = std::strtod(buf.c_str(), &end);
  if (end != buf.c_str() + buf.size()) return std::nullopt;
  return v;
}

}  // namespace

double LogFile::start_ts() const {
  return entries.empty() ? 0.0 : entries.front().ts;
}

double LogFile::duration() const {
  if (entries.size() < 2) return 0.0;
  return entries.back().ts - entries.front().ts;
}

LogFile parse_log(std::string_view text) {
  LogFile out;
  std::size_t lineno = 0;
  std::size_t pos = 0;

  while (pos <= text.size()) {
    auto nl = text.find('\n', pos);
    std::string_view raw =
        text.substr(pos, nl == std::string_view::npos ? std::string_view::npos
                                                      : nl - pos);
    pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
    ++lineno;

    std::string_view line = trim(raw);
    if (line.empty() || line.front() == '#') continue;

    std::string_view ts_tok, bus_tok, frame_tok;
    if (!split3(line, ts_tok, bus_tok, frame_tok)) {
      out.errors.push_back({lineno, std::string(line), "not 3 fields"});
      continue;
    }

    auto ts = parse_ts(ts_tok);
    if (!ts) {
      out.errors.push_back({lineno, std::string(line), "bad timestamp"});
      continue;
    }

    auto frame = parse_short(frame_tok);
    if (!frame) {
      out.errors.push_back({lineno, std::string(line), "bad frame"});
      continue;
    }

    out.entries.push_back({*ts, std::string(bus_tok), *frame});
  }

  return out;
}

std::optional<LogFile> read_log(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_log(ss.str());
}

}  // namespace canbench
