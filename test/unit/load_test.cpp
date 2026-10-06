#include "log/load.hpp"

#include "frame/frame.hpp"
#include "log/log.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using canbench::bit_timeline;
using canbench::bus_load;
using canbench::parse_log;
using canbench::parse_short;
using Catch::Matchers::WithinAbs;

TEST_CASE("load is frame bits over bits the bus could carry", "[load]") {
  // two frames, one second apart -> 1s of bus time at 500 kbit/s
  auto log = parse_log("(0.0) can0 123#00\n(1.0) can0 123#00\n");
  auto load = bus_load(log, 500000.0);
  double one = static_cast<double>(bit_timeline(*parse_short("123#00")).size());
  CHECK_THAT(load.frame_bits, WithinAbs(2 * one, 1e-9));
  REQUIRE(load.percent);
  CHECK_THAT(*load.percent, WithinAbs(100.0 * 2 * one / 500000.0, 1e-9));
}

TEST_CASE("a log that spans no time has no load figure", "[load]") {
  auto load = bus_load(parse_log("(0.0) can0 123#00\n"));
  CHECK_FALSE(load.percent.has_value());
}

TEST_CASE("empty log is zero load", "[load]") {
  auto load = bus_load(parse_log(""));
  CHECK(load.frame_bits == 0);
  CHECK_FALSE(load.percent.has_value());
}
