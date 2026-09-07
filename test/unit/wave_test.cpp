#include "wave/wave.hpp"

#include "frame/frame.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using canbench::annotated_timeline;
using canbench::Bit;
using canbench::bit_timeline;
using canbench::Field;
using canbench::Frame;
using canbench::parse_short;
using canbench::wave;

namespace {

std::vector<std::string> lines(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == '\n') { out.push_back(cur); cur.clear(); }
    else cur += c;
  }
  out.push_back(cur);
  return out;
}

}  // namespace

TEST_CASE("annotated timeline lines up with bit_timeline", "[wave]") {
  auto f = parse_short("123#DEADBEEF");
  REQUIRE(f);
  auto ann = annotated_timeline(*f);
  auto flat = bit_timeline(*f);

  REQUIRE(ann.size() == flat.size());
  for (std::size_t i = 0; i < ann.size(); ++i)
    CHECK(ann[i].level == flat[i]);
}

TEST_CASE("timeline starts with SOF and ends with intermission", "[wave]") {
  auto f = parse_short("100#00");
  REQUIRE(f);
  auto ann = annotated_timeline(*f);

  CHECK(ann.front().field == Field::kSof);
  CHECK(ann.front().level == Bit::kDominant);
  CHECK(ann.front().stuffed == false);

  // last 3 bits are the interframe space, all recessive
  for (std::size_t i = ann.size() - 3; i < ann.size(); ++i) {
    CHECK(ann[i].field == Field::kIfs);
    CHECK(ann[i].level == Bit::kRecessive);
  }
}

TEST_CASE("a zero-heavy frame forces stuff bits", "[wave]") {
  // id 0, eight 0x00 bytes -> long dominant runs, has to stuff
  auto f = parse_short("0#0000000000000000");
  REQUIRE(f);
  auto ann = annotated_timeline(*f);
  int stuffed = std::count_if(ann.begin(), ann.end(),
                              [](const auto& wb) { return wb.stuffed; });
  CHECK(stuffed > 0);
  // stuff bits only ever land in SOF..CRC, never the fixed tail
  for (const auto& wb : ann)
    if (wb.stuffed)
      CHECK((wb.field == Field::kSof || wb.field == Field::kId ||
             wb.field == Field::kControl || wb.field == Field::kDlc ||
             wb.field == Field::kData || wb.field == Field::kCrc));
}

TEST_CASE("wave() draws the expected rows", "[wave]") {
  auto f = parse_short("123#DEADBEEF");
  REQUIRE(f);
  auto text = wave(*f);
  auto rows = lines(text);

  REQUIRE(rows.size() == 6);  // summary, ruler, rec, dom, stuff, legend
  CHECK(rows[1].find("CRC") != std::string::npos);
  CHECK(rows[1].find("DATA") != std::string::npos);
  CHECK(rows[4].find("stuff") != std::string::npos);

  // one '^' in the stuff row per stuffed bit
  auto ann = annotated_timeline(*f);
  int stuffed = std::count_if(ann.begin(), ann.end(),
                              [](const auto& wb) { return wb.stuffed; });
  int carets = std::count(rows[4].begin(), rows[4].end(), '^');
  CHECK(carets == stuffed);
}
