#include "log/asc.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using canbench::parse_asc;

namespace {

constexpr std::string_view kSample =
    "date Thu Jan 1 00:00:00.000 am 1970\n"
    "base hex  timestamps absolute\n"
    "no internal events logged\n"
    "\n"
    "   0.000000 1  123             Rx   d 4 DE AD BE EF\n"
    "   0.010200 1  3B1             Rx   d 1 00\n"
    "   0.021500 2  200             Rx   r 0\n"
    "   0.033000 1  1F334455x       Rx   d 2 11 22\n"
    "End TriggerBlock\n";

}  // namespace

TEST_CASE("reads every frame line, skips the header noise", "[asc]") {
  auto log = parse_asc(kSample);
  REQUIRE(log.entries.size() == 4);
  CHECK(log.errors.empty());

  CHECK(log.entries[0].ts == 0.0);
  CHECK(log.entries[0].bus == "1");
  CHECK(log.entries[0].frame.id == 0x123);
  CHECK(log.entries[0].frame.dlc == 4);
  CHECK(log.entries[0].frame.data[0] == 0xDE);

  CHECK(log.entries[2].bus == "2");
  CHECK(log.entries[2].frame.rtr);

  CHECK(log.entries[3].frame.extended);
  CHECK(log.entries[3].frame.id == 0x1F334455);
}

TEST_CASE("extended id without the x suffix but out of 11-bit range is an error", "[asc]") {
  // learned this lesson the hard way on the candump side - don't quietly
  // promote an out-of-range id, reject it
  auto log = parse_asc("0.0 1  800             Rx   d 1 00\n");
  CHECK(log.entries.empty());
  REQUIRE(log.errors.size() == 1);
}

TEST_CASE("a malformed frame line is an error, not silently skipped", "[asc]") {
  auto log = parse_asc("0.0 1  123             Rx   d 4 DE AD BE\n");  // says 4, gives 3
  CHECK(log.entries.empty());
  REQUIRE(log.errors.size() == 1);
  CHECK(log.errors[0].why == std::string("not enough data bytes"));
}

TEST_CASE("empty and header-only input yields nothing, no errors", "[asc]") {
  auto log = parse_asc("date Thu Jan 1 00:00:00.000 am 1970\nbase hex\n\n");
  CHECK(log.entries.empty());
  CHECK(log.errors.empty());
}
