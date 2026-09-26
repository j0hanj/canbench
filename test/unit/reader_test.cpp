#include "log/reader.hpp"

#include <catch2/catch_test_macros.hpp>

using canbench::guess_format;
using canbench::LogFormat;

TEST_CASE("extension picks the format, case-insensitively", "[reader]") {
  CHECK(guess_format("drive.asc") == LogFormat::kAsc);
  CHECK(guess_format("drive.ASC") == LogFormat::kAsc);
  CHECK(guess_format("drive.Asc") == LogFormat::kAsc);
  CHECK(guess_format("drive.log") == LogFormat::kCandump);
  CHECK(guess_format("drive.LOG") == LogFormat::kCandump);
}

TEST_CASE("anything else defaults to candump", "[reader]") {
  CHECK(guess_format("noextension") == LogFormat::kCandump);
  CHECK(guess_format("") == LogFormat::kCandump);
  CHECK(guess_format("weird.asc.log") == LogFormat::kCandump);  // last extension wins
  CHECK(guess_format("asc") == LogFormat::kCandump);            // too short to be ".asc"
}
