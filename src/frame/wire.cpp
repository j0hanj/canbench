#include "frame/wire.hpp"

#include <random>

namespace canbench {

namespace {

constexpr int kStuffAfter = 5;

// reads wire bits with the stuffing rule undone. mirrors the encoder: nothing
// before SOF so no run going, and a stuff bit counts as the first bit of the
// next run.
class WireReader {
 public:
  explicit WireReader(const std::vector<Bit>& w) : w_(w) {}

  // next real (destuffed) bit. false = stuff error or out of bits, see error().
  bool next(Bit& out) {
    if (run_ == kStuffAfter && !take_stuff()) return false;
    if (pos_ >= w_.size()) return fail(WireError::kTruncated, pos_);
    Bit b = w_[pos_++];
    if (b == last_) {
      ++run_;
    } else {
      last_ = b;
      run_ = 1;
    }
    bits_.push_back(b);
    out = b;
    return true;
  }

  // the encoder stuffs after the last crc bit too if that finished a run of 5
  bool finish_stuffed_part() { return run_ != kStuffAfter || take_stuff(); }

  // fixed-form bits after the crc aren't stuffed - read them straight off
  bool next_raw(Bit& out) {
    if (pos_ >= w_.size()) return fail(WireError::kTruncated, pos_);
    out = w_[pos_++];
    return true;
  }

  std::size_t last_index() const { return pos_ - 1; }
  const std::vector<Bit>& bits() const { return bits_; }
  WireError error() const { return err_; }
  std::size_t error_at() const { return err_at_; }

 private:
  bool take_stuff() {
    if (pos_ >= w_.size()) return fail(WireError::kTruncated, pos_);
    Bit s = w_[pos_];
    if (s == last_) return fail(WireError::kStuff, pos_);  // sixth equal bit
    last_ = s;
    run_ = 1;
    ++pos_;
    return true;
  }

  bool fail(WireError e, std::size_t at) {
    err_ = e;
    err_at_ = at;
    return false;
  }

  const std::vector<Bit>& w_;
  std::size_t pos_ = 0;
  Bit last_ = Bit::kRecessive;
  int run_ = 0;
  std::vector<Bit> bits_;  // destuffed, for the crc
  WireError err_ = WireError::kNone;
  std::size_t err_at_ = 0;
};

}  // namespace

WireDecode decode_wire(const std::vector<Bit>& wire) {
  WireDecode out;
  WireReader r(wire);

  auto bail = [&]() {
    out.error = r.error();
    out.error_at = r.error_at();
    return out;
  };
  auto form_error = [&]() {
    out.error = WireError::kForm;
    out.error_at = r.last_index();
    return out;
  };
  auto read_n = [&](int n, std::uint32_t& v) {
    v = 0;
    for (int i = 0; i < n; ++i) {
      Bit b;
      if (!r.next(b)) return false;
      v = (v << 1) | (b == Bit::kRecessive ? 1u : 0u);
    }
    return true;
  };

  Bit b;
  if (!r.next(b)) return bail();
  if (b != Bit::kDominant) return form_error();  // SOF

  std::uint32_t id_a = 0;
  if (!read_n(11, id_a)) return bail();

  Bit rtr_or_srr, ide;
  if (!r.next(rtr_or_srr) || !r.next(ide)) return bail();

  Frame f;
  if (ide == Bit::kDominant) {
    f.extended = false;
    f.id = id_a;
    f.rtr = (rtr_or_srr == Bit::kRecessive);
    Bit r0;
    if (!r.next(r0)) return bail();  // reserved, either level is tolerated
  } else {
    if (rtr_or_srr != Bit::kRecessive) return form_error();  // SRR must be recessive
    f.extended = true;
    std::uint32_t id_b = 0;
    if (!read_n(18, id_b)) return bail();
    f.id = (id_a << 18) | id_b;
    Bit rtr, r1, r0;
    if (!r.next(rtr) || !r.next(r1) || !r.next(r0)) return bail();
    f.rtr = (rtr == Bit::kRecessive);
  }

  std::uint32_t dlc = 0;
  if (!read_n(4, dlc)) return bail();
  f.dlc = static_cast<std::uint8_t>(dlc);

  for (int i = 0; i < f.data_len(); ++i) {
    std::uint32_t byte = 0;
    if (!read_n(8, byte)) return bail();
    f.data[i] = static_cast<std::uint8_t>(byte);
  }

  std::vector<Bit> covered = r.bits();  // SOF..data, what the crc runs over

  std::uint32_t got_crc = 0;
  if (!read_n(15, got_crc)) return bail();
  if (!r.finish_stuffed_part()) return bail();

  out.frame = f;

  // crc mismatch is reported before the tail form checks - good enough here
  if (crc15(covered) != got_crc) {
    out.error = WireError::kCrc;
    out.error_at = r.last_index();
    return out;
  }

  Bit crc_delim, ack, ack_delim;
  if (!r.next_raw(crc_delim)) return bail();
  if (crc_delim != Bit::kRecessive) return form_error();
  if (!r.next_raw(ack)) return bail();
  out.acked = (ack == Bit::kDominant);
  if (!r.next_raw(ack_delim)) return bail();
  if (ack_delim != Bit::kRecessive) return form_error();

  for (int i = 0; i < 7; ++i) {  // EOF
    Bit e;
    if (!r.next_raw(e)) return bail();
    if (e != Bit::kRecessive) return form_error();
  }

  return out;
}

const char* error_name(WireError e) {
  switch (e) {
    case WireError::kNone: return "none";
    case WireError::kStuff: return "stuff error";
    case WireError::kForm: return "form error";
    case WireError::kCrc: return "crc error";
    case WireError::kTruncated: return "truncated";
  }
  return "?";
}

std::vector<FlipResult> sweep_single_flips(const Frame& f) {
  std::vector<WireBit> ann = annotated_timeline(f);
  std::vector<Bit> wire;
  wire.reserve(ann.size());
  for (const WireBit& wb : ann) wire.push_back(wb.level);

  std::vector<FlipResult> out;
  out.reserve(ann.size());
  for (std::size_t i = 0; i < wire.size(); ++i) {
    std::vector<Bit> flipped = wire;
    flipped[i] = (flipped[i] == Bit::kDominant) ? Bit::kRecessive : Bit::kDominant;
    out.push_back({i, ann[i].field, ann[i].stuffed, wire[i], decode_wire(flipped)});
  }
  return out;
}

WireDecode corrupt_one_bit(const Frame& f) {
  std::vector<WireBit> ann = annotated_timeline(f);

  std::vector<std::size_t> candidates;
  for (std::size_t i = 0; i < ann.size(); ++i)
    if (ann[i].field != Field::kAck && ann[i].field != Field::kIfs) candidates.push_back(i);

  std::vector<Bit> wire;
  wire.reserve(ann.size());
  for (const WireBit& wb : ann) wire.push_back(wb.level);

  static thread_local std::mt19937 rng(std::random_device{}());
  std::size_t pick = candidates[std::uniform_int_distribution<std::size_t>(
      0, candidates.size() - 1)(rng)];
  wire[pick] = (wire[pick] == Bit::kDominant) ? Bit::kRecessive : Bit::kDominant;

  return decode_wire(wire);
}

}  // namespace canbench
