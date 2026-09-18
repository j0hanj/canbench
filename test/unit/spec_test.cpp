#include "spec/spec.hpp"

#include <catch2/catch_test_macros.hpp>

using canbench::check_spec;
using canbench::Dbc;
using canbench::LogFile;
using canbench::parse_dbc;
using canbench::parse_log;
using canbench::parse_spec;
using canbench::RuleKind;

TEST_CASE("parses present/absent/range", "[spec]") {
  auto spec = parse_spec(
      "# a comment\n"
      "\n"
      "present 123\n"
      "absent 666\n"
      "range EngineSpeed 0 8000\n");
  REQUIRE(spec.errors.empty());
  REQUIRE(spec.rules.size() == 3);
  CHECK(spec.rules[0].kind == RuleKind::kPresent);
  CHECK(spec.rules[0].id == 0x123);
  CHECK(spec.rules[1].kind == RuleKind::kAbsent);
  CHECK(spec.rules[1].id == 0x666);
  CHECK(spec.rules[2].kind == RuleKind::kRange);
  CHECK(spec.rules[2].signal == "EngineSpeed");
  CHECK(spec.rules[2].lo == 0);
  CHECK(spec.rules[2].hi == 8000);
}

TEST_CASE("parses period", "[spec]") {
  auto spec = parse_spec("period 123 0.08 0.12\n");
  REQUIRE(spec.rules.size() == 1);
  CHECK(spec.rules[0].kind == RuleKind::kPeriod);
  CHECK(spec.rules[0].id == 0x123);
  CHECK(spec.rules[0].lo == 0.08);
  CHECK(spec.rules[0].hi == 0.12);
}

TEST_CASE("period rule checks the gaps between sends", "[spec]") {
  auto log = parse_log(
      "(0.00) can0 123#00\n"
      "(0.10) can0 123#00\n"
      "(0.20) can0 123#00\n"
      "(0.21) can0 123#00\n");  // this gap is way too short

  auto ok = check_spec(log, nullptr, parse_spec("period 123 0.05 0.15\n").rules);
  CHECK_FALSE(ok[0].passed);  // the 0.01s gap trips it

  auto lenient = check_spec(log, nullptr, parse_spec("period 123 0.0 1.0\n").rules);
  CHECK(lenient[0].passed);
}

TEST_CASE("period rule with fewer than two sends passes vacuously", "[spec]") {
  auto log = parse_log("(0.0) can0 123#00\n");
  auto results = check_spec(log, nullptr, parse_spec("period 123 0.05 0.15\n").rules);
  CHECK(results[0].passed);
}

TEST_CASE("bad lines are collected, not fatal", "[spec]") {
  auto spec = parse_spec(
      "present\n"           // missing id
      "range OnlyOneNum 5\n"
      "present 123\n");
  CHECK(spec.rules.size() == 1);
  REQUIRE(spec.errors.size() == 2);
  CHECK(spec.errors[0].find("line 1") != std::string::npos);
}

TEST_CASE("present/absent against a log", "[spec]") {
  auto log = parse_log(
      "(1.0) can0 123#DEADBEEF\n"
      "(1.1) can0 456#00\n");
  auto spec = parse_spec("present 123\nabsent 123\npresent 999\n");
  auto results = check_spec(log, nullptr, spec.rules);

  REQUIRE(results.size() == 3);
  CHECK(results[0].passed);       // 123 present, and it is
  CHECK_FALSE(results[1].passed); // 123 absent, but it's there
  CHECK_FALSE(results[2].passed); // 999 present, but it's not
}

TEST_CASE("range rule decodes through the dbc", "[spec]") {
  auto log = parse_log(
      "(1.0) can0 123#10278200\n"    // EngineSpeed=2500rpm CoolantTemp=90
      "(1.1) can0 123#409C8200\n");  // EngineSpeed=10000rpm - over 8000
  auto dbc = parse_dbc(
      "BO_ 291 EngineData: 4 ECU\n"
      " SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] \"rpm\" Dash\n"
      " SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|215] \"degC\" Dash\n");

  auto ok = check_spec(log, &dbc, parse_spec("range CoolantTemp -40 215\n").rules);
  CHECK(ok[0].passed);

  auto bad = check_spec(log, &dbc, parse_spec("range EngineSpeed 0 8000\n").rules);
  CHECK_FALSE(bad[0].passed);
}

TEST_CASE("range rule without a dbc fails cleanly", "[spec]") {
  LogFile log;
  auto results = check_spec(log, nullptr, parse_spec("range Foo 0 1\n").rules);
  REQUIRE(results.size() == 1);
  CHECK_FALSE(results[0].passed);
}

TEST_CASE("range rule for a signal the dbc doesn't have", "[spec]") {
  Dbc dbc;
  LogFile log;
  auto results = check_spec(log, &dbc, parse_spec("range Ghost 0 1\n").rules);
  CHECK_FALSE(results[0].passed);
}

TEST_CASE("a signal that never appears in the log passes vacuously", "[spec]") {
  auto log = parse_log("(1.0) can0 999#00\n");  // some other id entirely
  auto dbc = parse_dbc(
      "BO_ 291 EngineData: 4 ECU\n"
      " SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] \"rpm\" Dash\n");
  auto results = check_spec(log, &dbc, parse_spec("range EngineSpeed 0 1\n").rules);
  CHECK(results[0].passed);
}
