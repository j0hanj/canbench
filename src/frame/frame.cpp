#include "frame/frame.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace canbench {

namespace {

constexpr std::uint32_t kStdIdMax = 0x7FF;        // 11 bits
constexpr std::uint32_t kExtIdMax = 0x1FFFFFFF;   // 29 bits
constexpr std::uint16_t kCrcPoly = 0x4599;
constexpr int kStuffAfter = 5;                    // stuff bit after 5 equal bits

Bit bit_of(unsigned v) { return v ? Bit::kRecessive : Bit::kDominant; }

// push the low `n` bits of `v`, high bit first
void push_bits(std::vector<Bit>& out, std::uint32_t v, int n) {
  for (int i = n - 1; i >= 0; --i) out.push_back(bit_of((v >> i) & 1u));
}

std::optional<int> hex_val(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
  return std::nullopt;
}

std::optional<std::uint32_t> parse_hex(std::string_view s) {
  if (s.empty() || s.size() > 8) return std::nullopt;
  std::uint32_t v = 0;
  for (char c : s) {
    auto d = hex_val(c);
    if (!d) return std::nullopt;
    v = (v << 4) | static_cast<std::uint32_t>(*d);
  }
  return v;
}

}  // namespace

int Frame::data_len() const {
  if (rtr) return 0;
  return std::min<int>(dlc, kMaxData);
}

std::optional<Frame> parse_short(std::string_view text) {
  auto hash = text.find('#');
  if (hash == std::string_view::npos) return std::nullopt;

  std::string_view id_text = text.substr(0, hash);
  std::string_view rest = text.substr(hash + 1);
  if (rest.starts_with('#')) return std::nullopt;  // that's the CAN-FD form, skip

  auto id = parse_hex(id_text);
  if (!id) return std::nullopt;

  Frame f;
  f.id = *id;
  // socketcan treats a >3-digit id as extended. also anything that won't fit in 11 bits.
  f.extended = id_text.size() > 3 || *id > kStdIdMax;
  if (f.id > (f.extended ? kExtIdMax : kStdIdMax)) return std::nullopt;

  if (!rest.empty() && (rest.front() == 'R' || rest.front() == 'r')) {
    f.rtr = true;
    rest.remove_prefix(1);
    if (rest.empty()) return f;
    if (rest.size() != 1) return std::nullopt;
    auto d = hex_val(rest.front());
    if (!d || *d > kMaxData) return std::nullopt;
    f.dlc = static_cast<std::uint8_t>(*d);
    return f;
  }

  if (rest.size() % 2 != 0 || rest.size() > 2 * kMaxData) return std::nullopt;
  int n = static_cast<int>(rest.size() / 2);
  for (int i = 0; i < n; ++i) {
    auto hi = hex_val(rest[2 * i]);
    auto lo = hex_val(rest[2 * i + 1]);
    if (!hi || !lo) return std::nullopt;
    f.data[i] = static_cast<std::uint8_t>((*hi << 4) | *lo);
  }
  f.dlc = static_cast<std::uint8_t>(n);
  return f;
}

std::string describe(const Frame& f) {
  char buf[16];
  std::string out = "id=0x";
  std::snprintf(buf, sizeof(buf), "%X", f.id);
  out += buf;
  out += f.extended ? " ext " : " std ";
  out += f.rtr ? "remote" : "data";
  out += " dlc=" + std::to_string(f.dlc);
  if (!f.rtr) {
    out += " [";
    for (int i = 0; i < f.data_len(); ++i) {
      std::snprintf(buf, sizeof(buf), "%02X", f.data[i]);
      if (i) out += ' ';
      out += buf;
    }
    out += "]";
  }
  return out;
}

std::vector<Bit> crc_input_bits(const Frame& f) {
  std::vector<Bit> b;
  b.push_back(Bit::kDominant);  // start of frame

  if (!f.extended) {
    push_bits(b, f.id & kStdIdMax, 11);
    b.push_back(bit_of(f.rtr));    // RTR
    b.push_back(Bit::kDominant);   // IDE = 0
    b.push_back(Bit::kDominant);   // r0
  } else {
    push_bits(b, (f.id >> 18) & kStdIdMax, 11);  // top 11 id bits
    b.push_back(Bit::kRecessive);  // SRR
    b.push_back(Bit::kRecessive);  // IDE = 1
    push_bits(b, f.id & 0x3FFFF, 18);            // bottom 18 id bits
    b.push_back(bit_of(f.rtr));    // RTR
    b.push_back(Bit::kDominant);   // r1
    b.push_back(Bit::kDominant);   // r0
  }

  push_bits(b, f.dlc & 0xF, 4);
  for (int i = 0; i < f.data_len(); ++i) push_bits(b, f.data[i], 8);
  return b;
}

std::vector<Bit> arbitration_bits(const Frame& f) {
  std::vector<Bit> b;
  if (!f.extended) {
    push_bits(b, f.id & kStdIdMax, 11);
    b.push_back(bit_of(f.rtr));    // RTR
    b.push_back(Bit::kDominant);   // IDE = 0, still contested against extended
  } else {
    push_bits(b, (f.id >> 18) & kStdIdMax, 11);
    b.push_back(Bit::kRecessive);  // SRR - sits where a std frame's RTR is
    b.push_back(Bit::kRecessive);  // IDE = 1
    push_bits(b, f.id & 0x3FFFF, 18);
    b.push_back(bit_of(f.rtr));    // RTR
  }
  return b;
}

int arbitration_cmp(const Frame& a, const Frame& b) {
  auto ba = arbitration_bits(a);
  auto bb = arbitration_bits(b);
  std::size_t n = std::min(ba.size(), bb.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (ba[i] != bb[i])
      return (ba[i] == Bit::kDominant) ? -1 : 1;  // dominant wins
  }
  // equal all the way through the shorter field - that's the std frame, and it
  // has already won every contested bit, so it takes the bus.
  if (ba.size() != bb.size()) return ba.size() < bb.size() ? -1 : 1;
  return 0;
}

std::uint16_t crc15(const std::vector<Bit>& bits) {
  // textbook shift-register crc. one bit at a time.
  std::uint16_t crc = 0;
  for (Bit b : bits) {
    std::uint16_t in = static_cast<std::uint16_t>(b == Bit::kRecessive) ^ ((crc >> 14) & 1u);
    crc = static_cast<std::uint16_t>((crc << 1) & 0x7FFF);
    if (in) crc ^= kCrcPoly;
  }
  return crc & 0x7FFF;
}

namespace {

// SOF..data..crc as (bit, field) pairs, before stuffing. mirrors
// crc_input_bits() but keeps track of which field each bit came from.
std::vector<WireBit> tagged_span(const Frame& f) {
  std::vector<WireBit> s;
  auto put = [&](Bit b, Field field) { s.push_back({b, field, false}); };
  auto put_n = [&](std::uint32_t v, int n, Field field) {
    for (int i = n - 1; i >= 0; --i) put(bit_of((v >> i) & 1u), field);
  };

  put(Bit::kDominant, Field::kSof);

  if (!f.extended) {
    put_n(f.id & kStdIdMax, 11, Field::kId);
    put(bit_of(f.rtr), Field::kControl);
    put(Bit::kDominant, Field::kControl);   // IDE = 0
    put(Bit::kDominant, Field::kControl);   // r0
  } else {
    put_n((f.id >> 18) & kStdIdMax, 11, Field::kId);
    put(Bit::kRecessive, Field::kControl);  // SRR
    put(Bit::kRecessive, Field::kControl);  // IDE = 1
    put_n(f.id & 0x3FFFF, 18, Field::kId);
    put(bit_of(f.rtr), Field::kControl);
    put(Bit::kDominant, Field::kControl);   // r1
    put(Bit::kDominant, Field::kControl);   // r0
  }

  put_n(f.dlc & 0xF, 4, Field::kDlc);
  for (int i = 0; i < f.data_len(); ++i) put_n(f.data[i], 8, Field::kData);
  put_n(crc15(crc_input_bits(f)), 15, Field::kCrc);
  return s;
}

}  // namespace

std::vector<WireBit> annotated_timeline(const Frame& f) {
  std::vector<WireBit> span = tagged_span(f);

  std::vector<WireBit> wire;
  wire.reserve(span.size() + 16);
  Bit last = Bit::kRecessive;  // nothing before SOF, so no run going
  int run = 0;
  for (const WireBit& wb : span) {
    if (wb.level == last) {
      ++run;
    } else {
      last = wb.level;
      run = 1;
    }
    wire.push_back(wb);
    if (run == kStuffAfter) {
      // drop in the opposite bit. same field as the run it broke up, and it
      // counts as the start of a new run.
      last = (wb.level == Bit::kDominant) ? Bit::kRecessive : Bit::kDominant;
      run = 1;
      wire.push_back({last, wb.field, true});
    }
  }

  // the tail isn't stuffed. crc delim, ack slot+delim, 7 eof, 3 intermission.
  // ack stays recessive here since there's no other node to pull it down.
  wire.push_back({Bit::kRecessive, Field::kCrcDelim, false});
  wire.push_back({Bit::kRecessive, Field::kAck, false});
  wire.push_back({Bit::kRecessive, Field::kAckDelim, false});
  for (int i = 0; i < 7; ++i) wire.push_back({Bit::kRecessive, Field::kEof, false});
  for (int i = 0; i < 3; ++i) wire.push_back({Bit::kRecessive, Field::kIfs, false});
  return wire;
}

std::vector<Bit> bit_timeline(const Frame& f) {
  std::vector<Bit> out;
  for (const WireBit& wb : annotated_timeline(f)) out.push_back(wb.level);
  return out;
}

std::string to_string(const std::vector<Bit>& bits) {
  std::string out;
  out.reserve(bits.size());
  for (Bit b : bits) out += (b == Bit::kRecessive) ? '1' : '0';
  return out;
}

}  // namespace canbench
