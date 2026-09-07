#include "dbc/dbc.hpp"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <utility>

namespace canbench {

namespace {

constexpr std::uint32_t kExtFlag = 0x80000000u;  // dbc sets this on 29-bit ids

std::string_view trim(std::string_view s) {
  auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && ws(s.front())) s.remove_prefix(1);
  while (!s.empty() && ws(s.back())) s.remove_suffix(1);
  return s;
}

// BO_ 256 EngineData: 8 ECU
std::optional<Message> parse_bo(std::string_view line) {
  // sscanf wants a null-terminated buffer
  std::string buf(line);
  unsigned long id = 0;
  char name[128] = {0};
  int dlc = 0;
  // name runs up to the ':' - "%[^:]" grabs it, then we trim
  if (std::sscanf(buf.c_str(), " BO_ %lu %127[^:]: %d", &id, name, &dlc) != 3)
    return std::nullopt;

  Message m;
  std::uint32_t raw = static_cast<std::uint32_t>(id);
  m.extended = (raw & kExtFlag) != 0;
  m.id = raw & ~kExtFlag;
  m.name = std::string(trim(name));
  m.dlc = dlc;
  return m;
}

//  SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] "rpm" Dash
//  SG_ Mux m0 : ...        <- multiplexed, we keep the signal, drop the m0
std::optional<Signal> parse_sg(std::string_view line) {
  std::string buf(line);
  const char* p = buf.c_str();

  // skip leading ws + "SG_"
  while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
  if (std::strncmp(p, "SG_", 3) != 0) return std::nullopt;
  p += 3;

  // name = first token
  while (*p && std::isspace(static_cast<unsigned char>(*p))) ++p;
  const char* nstart = p;
  while (*p && !std::isspace(static_cast<unsigned char>(*p)) && *p != ':') ++p;
  Signal s;
  s.name.assign(nstart, p);

  // everything up to ':' is optional mux info - skip it
  const char* colon = std::strchr(p, ':');
  if (!colon) return std::nullopt;
  p = colon + 1;

  int start = 0, len = 0, order = 1;
  char sign = '+';
  double factor = 1.0, offset = 0.0, lo = 0.0, hi = 0.0;
  char unit[64] = {0};
  int got = std::sscanf(p, " %d|%d@%d%c (%lf,%lf) [%lf|%lf] \"%63[^\"]\"",
                        &start, &len, &order, &sign, &factor, &offset, &lo, &hi,
                        unit);
  if (got < 6) return std::nullopt;  // need at least through (factor,offset)

  s.start_bit = start;
  s.length = len;
  s.little_endian = (order == 1);
  s.is_signed = (sign == '-');
  s.factor = factor;
  s.offset = offset;
  s.min = lo;
  s.max = hi;
  s.unit = unit;
  return s;
}

}  // namespace

std::int64_t Signal::decode_raw(const Frame& f) const {
  std::uint64_t raw = 0;

  if (little_endian) {
    // Intel: bit `start_bit` is the LSB, bits climb through the bytes.
    for (int i = 0; i < length; ++i) {
      int bit = start_bit + i;
      int byte = bit / 8;
      if (byte >= kMaxData) break;
      std::uint64_t v = (f.data[byte] >> (bit % 8)) & 1u;
      raw |= v << i;
    }
  } else {
    // Motorola: start_bit is the MSB, and the walk sawtooths - within a byte
    // you count down to bit 0, then jump to bit 7 of the next byte (+15).
    int pos = start_bit;
    for (int i = 0; i < length; ++i) {
      int byte = pos / 8;
      int bit = pos % 8;
      if (byte >= kMaxData) break;
      std::uint64_t v = (f.data[byte] >> bit) & 1u;
      raw = (raw << 1) | v;  // MSB first
      pos += (bit == 0) ? 15 : -1;
    }
  }

  std::int64_t out = static_cast<std::int64_t>(raw);
  if (is_signed && length > 0 && length < 64) {
    std::uint64_t sign_bit = 1ull << (length - 1);
    if (raw & sign_bit) out = static_cast<std::int64_t>(raw) - (1ll << length);
  }
  return out;
}

double Signal::decode(const Frame& f) const {
  return static_cast<double>(decode_raw(f)) * factor + offset;
}

const Message* Dbc::find(std::uint32_t id) const {
  auto it = messages.find(id);
  return it == messages.end() ? nullptr : &it->second;
}

Dbc parse_dbc(std::string_view text) {
  Dbc dbc;
  Message* current = nullptr;

  std::size_t pos = 0;
  while (pos <= text.size()) {
    auto nl = text.find('\n', pos);
    std::string_view raw = text.substr(
        pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;

    std::string_view line = trim(raw);
    if (line.empty()) continue;

    if (line.starts_with("BO_ ")) {
      if (auto m = parse_bo(line)) {
        auto [it, _] = dbc.messages.insert_or_assign(m->id, std::move(*m));
        current = &it->second;
      } else {
        current = nullptr;
      }
    } else if (line.starts_with("SG_ ")) {
      if (current) {
        if (auto s = parse_sg(line)) current->signals.push_back(std::move(*s));
      }
    } else {
      // BU_, CM_, VAL_, blank continuation lines, ... - not our problem yet
      current = nullptr;
    }
  }

  return dbc;
}

std::optional<Dbc> read_dbc(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_dbc(ss.str());
}

}  // namespace canbench
