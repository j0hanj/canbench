#include "dbc/dbc.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <string_view>

using canbench::Frame;
using canbench::parse_dbc;
using canbench::Signal;
using Catch::Matchers::WithinAbs;

namespace {

Frame frame_with(std::initializer_list<std::uint8_t> bytes) {
  Frame f;
  int i = 0;
  for (std::uint8_t b : bytes) f.data[i++] = b;
  f.dlc = static_cast<std::uint8_t>(bytes.size());
  return f;
}

Signal intel(int start, int len, bool sgn = false, double factor = 1.0,
             double offset = 0.0) {
  Signal s;
  s.start_bit = start;
  s.length = len;
  s.little_endian = true;
  s.is_signed = sgn;
  s.factor = factor;
  s.offset = offset;
  return s;
}

Signal motorola(int start, int len, bool sgn = false) {
  Signal s = intel(start, len, sgn);
  s.little_endian = false;
  return s;
}

}  // namespace

TEST_CASE("intel signal spans two bytes, low byte first", "[dbc]") {
  auto f = frame_with({0x34, 0x12});
  CHECK(intel(0, 16).decode_raw(f) == 0x1234);
}

TEST_CASE("intel signal starting mid-frame", "[dbc]") {
  auto f = frame_with({0x00, 0x00, 0x64});
  CHECK(intel(16, 8).decode_raw(f) == 100);
}

TEST_CASE("motorola walks MSB-first with the byte-boundary jump", "[dbc]") {
  // start bit 7 = MSB of byte 0. 16 bits -> byte0 then byte1, big-endian.
  auto f = frame_with({0x12, 0x34});
  CHECK(motorola(7, 16).decode_raw(f) == 0x1234);
}

TEST_CASE("motorola partial field takes the top bits of the byte", "[dbc]") {
  auto f = frame_with({0xA5});  // 1010 0101
  CHECK(motorola(7, 4).decode_raw(f) == 0xA);
}

TEST_CASE("signed signals sign-extend", "[dbc]") {
  CHECK(intel(0, 8, true).decode_raw(frame_with({0xFF})) == -1);
  CHECK(intel(0, 8, true).decode_raw(frame_with({0x80})) == -128);
  CHECK(intel(0, 8, true).decode_raw(frame_with({0x7F})) == 127);
}

TEST_CASE("factor and offset get applied", "[dbc]") {
  auto f = frame_with({0x64});  // 100
  CHECK_THAT(intel(0, 8, false, 1.0, -40.0).decode(f), WithinAbs(60.0, 1e-9));
  CHECK_THAT(intel(0, 8, false, 0.25, 0.0).decode(f), WithinAbs(25.0, 1e-9));
}

TEST_CASE("parse a small dbc", "[dbc]") {
  constexpr std::string_view text =
      "VERSION \"\"\n"
      "\n"
      "BO_ 291 EngineData: 4 ECU\n"
      " SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] \"rpm\" Dash\n"
      " SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|215] \"degC\" Dash\n"
      "\n"
      "BO_ 1024 Brake: 2 ABS\n"
      " SG_ Pressure : 7|16@0+ (0.1,0) [0|6553.5] \"bar\" Dash\n"
      "CM_ \"a comment line we ignore\";\n";

  auto dbc = parse_dbc(text);
  REQUIRE(dbc.messages.size() == 2);

  const auto* eng = dbc.find(291);
  REQUIRE(eng != nullptr);
  CHECK(eng->name == "EngineData");
  CHECK(eng->dlc == 4);
  REQUIRE(eng->signals.size() == 2);
  CHECK(eng->signals[0].name == "EngineSpeed");
  CHECK(eng->signals[0].length == 16);
  CHECK(eng->signals[0].little_endian);
  CHECK_THAT(eng->signals[0].factor, WithinAbs(0.25, 1e-9));
  CHECK(eng->signals[1].offset == -40.0);

  const auto* brk = dbc.find(1024);
  REQUIRE(brk != nullptr);
  CHECK_FALSE(brk->signals[0].little_endian);  // @0 -> motorola
}

TEST_CASE("extended id gets its high bit stripped", "[dbc]") {
  // 0x90000123 = ext flag (0x80000000) | 0x10000123
  auto dbc = parse_dbc("BO_ 2415919395 Wide: 8 ECU\n");
  REQUIRE(dbc.messages.size() == 1);
  const auto* m = dbc.find(0x10000123);
  REQUIRE(m != nullptr);
  CHECK(m->extended);
}

TEST_CASE("real message decode end to end", "[dbc]") {
  auto dbc = parse_dbc(
      "BO_ 291 EngineData: 4 ECU\n"
      " SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] \"rpm\" Dash\n"
      " SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|215] \"degC\" Dash\n");
  const auto* m = dbc.find(291);
  REQUIRE(m != nullptr);

  // 2500 rpm -> raw 10000 -> 0x2710 -> little-endian bytes 10 27
  // 90 degC   -> raw 130   -> 0x82
  Frame f = frame_with({0x10, 0x27, 0x82, 0x00});
  CHECK_THAT(m->signals[0].decode(f), WithinAbs(2500.0, 1e-9));
  CHECK_THAT(m->signals[1].decode(f), WithinAbs(90.0, 1e-9));
}
