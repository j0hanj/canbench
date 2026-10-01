#include "frame/wire.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using canbench::Bit;
using canbench::bit_timeline;
using canbench::corrupt_one_bit;
using canbench::decode_wire;
using canbench::Field;
using canbench::Frame;
using canbench::parse_short;
using canbench::sweep_single_flips;
using canbench::WireError;

namespace {

// a spread of frames - std/ext, remote, every dlc, stuff-heavy and stuff-free
std::vector<Frame> sample_frames() {
  const char* texts[] = {
      "123#DEADBEEF", "7FF#FFFFFFFFFFFFFFFF", "000#0000000000000000", "555#AA55AA55",
      "100#",         "1F334455#1122",        "1FFFFFFF#00",         "00000000#FFFFFF",
      "200#R",        "200#R3",               "18DAF110#0201050000000000",
      "3B1#00",       "7DF#0201050000000000", "0AA#0123456789ABCDEF",
  };
  std::vector<Frame> out;
  for (const char* t : texts) {
    auto f = parse_short(t);
    REQUIRE(f);
    out.push_back(*f);
  }
  return out;
}

bool same_frame(const Frame& a, const Frame& b) {
  if (a.id != b.id || a.extended != b.extended || a.rtr != b.rtr || a.dlc != b.dlc) return false;
  for (int i = 0; i < a.data_len(); ++i)
    if (a.data[i] != b.data[i]) return false;
  return true;
}

}  // namespace

TEST_CASE("decode_wire round-trips what bit_timeline encodes", "[wire]") {
  for (const Frame& f : sample_frames()) {
    auto d = decode_wire(bit_timeline(f));
    INFO("id=" << f.id << " ext=" << f.extended << " rtr=" << f.rtr << " dlc=" << int(f.dlc));
    REQUIRE(d.error == WireError::kNone);
    REQUIRE(d.frame);
    CHECK(same_frame(*d.frame, f));
    CHECK_FALSE(d.acked);  // nobody's on the bus to pull the ack slot down
  }
}

TEST_CASE("flipping the ack slot reads as acked, not as an error", "[wire]") {
  auto f = *parse_short("123#DEADBEEF");
  auto wire = bit_timeline(f);
  // ack slot is the second bit of the 13-bit tail: crc delim, ack, ack delim, 7 eof, 3 ifs
  wire[wire.size() - 12] = Bit::kDominant;
  auto d = decode_wire(wire);
  CHECK(d.error == WireError::kNone);
  CHECK(d.acked);
}

TEST_CASE("a recessive SOF is a form error", "[wire]") {
  auto wire = bit_timeline(*parse_short("123#DE"));
  wire[0] = Bit::kRecessive;
  auto d = decode_wire(wire);
  CHECK(d.error == WireError::kForm);
  CHECK(d.error_at == 0);
}

TEST_CASE("six equal bits in the stuffed part is a stuff error", "[wire]") {
  // id 0 -> SOF plus a long dominant run, so the sender stuffs after the 5th.
  // knock that stuff bit down to dominant and the receiver sees six in a row.
  auto f = *parse_short("000#00");
  auto wire = bit_timeline(f);
  REQUIRE(wire[5] == Bit::kRecessive);  // the stuff bit after SOF + 4 id zeros
  wire[5] = Bit::kDominant;
  auto d = decode_wire(wire);
  CHECK(d.error == WireError::kStuff);
  CHECK(d.error_at == 5);
}

TEST_CASE("flipping a data bit trips the crc", "[wire]") {
  auto f = *parse_short("123#DEADBEEF");
  auto sweep = sweep_single_flips(f);
  int checked = 0;
  for (const auto& r : sweep) {
    if (r.field != Field::kData || r.stuffed) continue;
    ++checked;
    // it's either the crc or (if the flip made a run of 6) a stuff error -
    // never a clean decode
    CHECK(r.outcome.error != WireError::kNone);
  }
  CHECK(checked == 32);
}

TEST_CASE("every single-bit flip is caught, except where it can't be", "[wire]") {
  // the ack slot flipping is just an ack, and nothing after the eof matters to
  // a receiver. everything from SOF through the last EOF bit must be caught.
  for (const Frame& f : sample_frames()) {
    auto sweep = sweep_single_flips(f);
    for (const auto& r : sweep) {
      if (r.field == Field::kAck || r.field == Field::kIfs) continue;
      INFO("id=" << f.id << " ext=" << f.extended << " bit=" << r.bit);
      CHECK(r.outcome.error != WireError::kNone);
    }
  }
}

TEST_CASE("a flip that changes the dlc doesn't crash the decoder", "[wire]") {
  // going hunting for bytes that aren't there has to end in an error, not a
  // read past the end
  auto f = *parse_short("100#00");
  for (const auto& r : sweep_single_flips(f))
    if (r.field == Field::kDlc) CHECK(r.outcome.error != WireError::kNone);
}

TEST_CASE("truncated input is reported, not read past", "[wire]") {
  auto wire = bit_timeline(*parse_short("123#DEADBEEF"));  // 81 bits, last 3 are ifs
  for (std::size_t keep : {0u, 1u, 12u, 40u, 70u, 77u}) {
    std::vector<Bit> cut(wire.begin(), wire.begin() + keep);
    auto d = decode_wire(cut);
    CHECK(d.error != WireError::kNone);
  }

  // dropping just the interframe space is fine - a receiver is done at eof
  std::vector<Bit> no_ifs(wire.begin(), wire.end() - 3);
  CHECK(decode_wire(no_ifs).error == WireError::kNone);
}

TEST_CASE("corrupt_one_bit always trips a real error, whichever bit it picks", "[wire]") {
  // this is what the bus sim's '!' actually calls now - every frame in the
  // spread (including the data-less remote ones) has to come back broken, no
  // matter which random bit got hit. run each one a bunch of times since the
  // pick changes call to call.
  for (const Frame& f : sample_frames())
    for (int i = 0; i < 20; ++i) CHECK(corrupt_one_bit(f).error != WireError::kNone);
}

TEST_CASE("corrupt_one_bit's picks land on more than one kind of error", "[wire]") {
  // not pinning down a seed or an exact distribution - just making sure this
  // doesn't quietly turn into "always picks bit 0" or some other non-random
  // regression. 200 flips of a frame with plenty of bits to choose from
  // should turn up at least two different outcomes.
  auto f = *parse_short("123#DEADBEEF");
  std::vector<WireError> seen;
  for (int i = 0; i < 200; ++i) {
    WireError e = corrupt_one_bit(f).error;
    if (std::find(seen.begin(), seen.end(), e) == seen.end()) seen.push_back(e);
  }
  CHECK(seen.size() > 1);
}
