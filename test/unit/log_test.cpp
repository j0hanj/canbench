#include "log/log.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string_view>

using canbench::parse_log;

namespace {

constexpr std::string_view kSample =
    "(1650000000.000000) can0 123#DEADBEEF\n"
    "(1650000000.010200) can0 3B1#00\n"
    "(1650000000.021500) can0 200#R\n"
    "(1650000000.033000) can0 1F334455#1122\n"
    "(1650000000.100000) can1 456#1122334455667788\n";

}  // namespace

TEST_CASE("reads every line of a clean log", "[log]") {
  auto log = parse_log(kSample);
  REQUIRE(log.entries.size() == 5);
  CHECK(log.errors.empty());

  CHECK(log.entries[0].ts == 1650000000.0);
  CHECK(log.entries[0].bus == "can0");
  CHECK(log.entries[0].frame.id == 0x123);
  CHECK(log.entries[0].frame.dlc == 4);

  CHECK(log.entries[2].frame.rtr);
  CHECK(log.entries[3].frame.extended);
  CHECK(log.entries[4].bus == "can1");
}

TEST_CASE("relative time helpers", "[log]") {
  auto log = parse_log(kSample);
  CHECK(log.start_ts() == 1650000000.0);
  CHECK(log.duration() == 0.1);
}

TEST_CASE("blank lines and # comments are skipped", "[log]") {
  auto log = parse_log(
      "# a candump log\n"
      "\n"
      "   \n"
      "(1.0) vcan0 7DF#0201050000000000\n");
  CHECK(log.entries.size() == 1);
  CHECK(log.errors.empty());
  CHECK(log.entries[0].bus == "vcan0");
}

TEST_CASE("bad lines are collected, parsing continues", "[log]") {
  auto log = parse_log(
      "(1.0) can0 123#DEADBEEF\n"
      "garbage without fields\n"
      "(nope) can0 123#00\n"
      "(2.0) can0 12G#00\n"
      "(3.0) can0 321#CAFE\n");
  REQUIRE(log.entries.size() == 2);
  REQUIRE(log.errors.size() == 3);
  CHECK(log.errors[0].line == 2);
  CHECK(log.errors[1].why == "bad timestamp");
  CHECK(log.errors[2].why == "bad frame");
}

TEST_CASE("empty input is fine", "[log]") {
  auto log = parse_log("");
  CHECK(log.entries.empty());
  CHECK(log.errors.empty());
  CHECK(log.start_ts() == 0.0);
  CHECK(log.duration() == 0.0);
}
