#include "log/summary.hpp"

#include "log/log.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using canbench::parse_log;
using canbench::summarize_ids;
using Catch::Matchers::WithinAbs;

TEST_CASE("counts sends per id, sorted by id", "[summary]") {
  auto log = parse_log(
      "(0.0) can0 200#00\n"
      "(0.1) can0 100#00\n"
      "(0.2) can0 200#00\n");
  auto s = summarize_ids(log);
  REQUIRE(s.size() == 2);
  CHECK(s[0].id == 0x100);
  CHECK(s[0].count == 1);
  CHECK(s[1].id == 0x200);
  CHECK(s[1].count == 2);
}

TEST_CASE("median gap is the middle of the gaps", "[summary]") {
  auto log = parse_log(
      "(0.00) can0 123#00\n"
      "(0.10) can0 123#00\n"
      "(0.20) can0 123#00\n"
      "(0.25) can0 123#00\n");  // gaps 0.10, 0.10, 0.05 -> median 0.10
  auto s = summarize_ids(log);
  REQUIRE(s[0].median_gap);
  CHECK_THAT(*s[0].median_gap, WithinAbs(0.10, 1e-9));
}

TEST_CASE("one send has no gap", "[summary]") {
  auto s = summarize_ids(parse_log("(0.0) can0 123#00\n"));
  REQUIRE(s.size() == 1);
  CHECK_FALSE(s[0].median_gap.has_value());
}

TEST_CASE("empty log gives nothing", "[summary]") {
  CHECK(summarize_ids(parse_log("")).empty());
}
