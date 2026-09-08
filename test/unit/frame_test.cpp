#include "frame/frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using canbench::arbitration_cmp;
using canbench::Bit;
using canbench::bit_timeline;
using canbench::crc15;
using canbench::crc_input_bits;
using canbench::Frame;
using canbench::parse_short;
using canbench::to_string;

namespace {

std::vector<Bit> bytes_to_bits(const std::vector<std::uint8_t>& bytes) {
  std::vector<Bit> bits;
  for (std::uint8_t byte : bytes)
    for (int i = 7; i >= 0; --i)
      bits.push_back(((byte >> i) & 1u) ? Bit::kRecessive : Bit::kDominant);
  return bits;
}

int longest_run(const std::vector<Bit>& bits) {
  int best = 0, run = 0;
  Bit last = Bit::kRecessive;
  for (Bit b : bits) {
    run = (b == last) ? run + 1 : 1;
    last = b;
    best = std::max(best, run);
  }
  return best;
}

}  // namespace

TEST_CASE("parse a normal data frame", "[frame]") {
  auto f = parse_short("123#DEADBEEF");
  REQUIRE(f);
  CHECK(f->id == 0x123);
  CHECK_FALSE(f->extended);
  CHECK(f->dlc == 4);
  CHECK(f->data[0] == 0xDE);
  CHECK(f->data[3] == 0xEF);
}

TEST_CASE("wide id -> extended", "[frame]") {
  auto f = parse_short("1F334455#1122");
  REQUIRE(f);
  CHECK(f->extended);
  CHECK(f->id == 0x1F334455);
  CHECK(f->dlc == 2);
}

TEST_CASE("remote frame with a dlc", "[frame]") {
  auto f = parse_short("200#R3");
  REQUIRE(f);
  CHECK(f->rtr);
  CHECK(f->dlc == 3);
  CHECK(f->data_len() == 0);  // remote frames carry nothing
}

TEST_CASE("garbage input gets rejected", "[frame]") {
  CHECK_FALSE(parse_short("123"));       // no #
  CHECK_FALSE(parse_short("12G#00"));    // bad hex
  CHECK_FALSE(parse_short("123#DEA"));   // half a byte
  CHECK_FALSE(parse_short("800#00"));    // 0x800 doesn't fit in 11 bits
}

TEST_CASE("crc covers SOF then the id", "[frame]") {
  Frame f;
  f.id = 0x123;  // 001 0010 0011
  auto bits = crc_input_bits(f);
  REQUIRE(bits.size() >= 12);
  CHECK(bits[0] == Bit::kDominant);
  CHECK(to_string(std::vector<Bit>(bits.begin() + 1, bits.begin() + 12)) == "00100100011");
}

TEST_CASE("crc15 matches the known check value", "[frame]") {
  // crc-15/can of ascii "123456789" is 0x059E, that's the standard test vector
  CHECK(crc15(bytes_to_bits({'1','2','3','4','5','6','7','8','9'})) == 0x059E);
}

TEST_CASE("all-dominant crc is zero", "[frame]") {
  CHECK(crc15(std::vector<Bit>(20, Bit::kDominant)) == 0);
}

TEST_CASE("lower id wins the bus", "[frame]") {
  auto lo = parse_short("100#00");
  auto hi = parse_short("200#00");
  REQUIRE(lo);
  REQUIRE(hi);
  CHECK(arbitration_cmp(*lo, *hi) < 0);
  CHECK(arbitration_cmp(*hi, *lo) > 0);
  CHECK(arbitration_cmp(*lo, *lo) == 0);
}

TEST_CASE("data frame beats the matching remote frame", "[frame]") {
  auto data = parse_short("123#00");
  auto remote = parse_short("123#R");
  REQUIRE(data);
  REQUIRE(remote);
  CHECK(arbitration_cmp(*data, *remote) < 0);  // RTR dominant on the data frame
}

TEST_CASE("standard frame beats an extended frame with the same base", "[frame]") {
  // std id 0x123, extended id with the same top 11 bits (0x123 << 18)
  auto std_f = parse_short("123#00");
  auto ext_f = parse_short("048C0000#00");  // 0x048C0000 >> 18 == 0x123
  REQUIRE(std_f);
  REQUIRE(ext_f);
  REQUIRE(ext_f->extended);
  CHECK((ext_f->id >> 18) == 0x123u);
  CHECK(arbitration_cmp(*std_f, *ext_f) < 0);
}

TEST_CASE("stuffing breaks up long runs", "[frame]") {
  // id 0, no data -> tons of dominant bits in the header, has to get stuffed
  Frame f;
  auto span = crc_input_bits(f);
  auto wire = bit_timeline(f);
  CHECK(wire.size() > span.size() + 13);  // 13 fixed tail bits + at least one stuff bit
  // ignore the 11 recessive eof/intermission bits at the end
  std::vector<Bit> stuffed(wire.begin(), wire.end() - 13);
  CHECK(longest_run(stuffed) <= 5);
}
