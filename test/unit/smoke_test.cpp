// nothing test, just makes sure the test build actually runs

#include <catch2/catch_test_macros.hpp>

TEST_CASE("tests run", "[smoke]") {
  REQUIRE(1 + 1 == 2);
}
