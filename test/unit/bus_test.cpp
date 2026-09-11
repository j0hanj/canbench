#include "bus/bus.hpp"

#include <catch2/catch_test_macros.hpp>

using canbench::Node;
using canbench::parse_short;
using canbench::run_bus;

namespace {

Node node(std::string name, std::initializer_list<const char*> frames) {
  Node n;
  n.name = std::move(name);
  for (auto text : frames) n.queue.push_back(*parse_short(text));
  return n;
}

}  // namespace

TEST_CASE("lowest id across nodes goes first", "[bus]") {
  auto log = run_bus({
      node("dash", {"300#00"}),
      node("ecu", {"100#00"}),
      node("abs", {"200#00"}),
  });

  REQUIRE(log.size() == 3);
  CHECK(log[0].node == "ecu");
  CHECK(log[1].node == "abs");
  CHECK(log[2].node == "dash");
}

TEST_CASE("a node's own queue stays in order", "[bus]") {
  // ecu sends 500 then 100 - even though 100 would win outright, it can't
  // jump ahead of ecu's own 500 since that's already at the front
  auto log = run_bus({node("ecu", {"500#00", "100#00"}), node("abs", {"200#00"})});

  REQUIRE(log.size() == 3);
  CHECK(log[0].node == "abs");    // 200 beats 500
  CHECK(log[1].node == "ecu");    // ecu's 500 finally gets through
  CHECK(log[2].node == "ecu");    // then its 100
}

TEST_CASE("everyone eventually gets to send everything", "[bus]") {
  auto log = run_bus({
      node("a", {"100#00", "101#00"}),
      node("b", {"100#01"}),
  });
  CHECK(log.size() == 3);
}

TEST_CASE("empty bus sends nothing", "[bus]") {
  CHECK(run_bus({}).empty());
  CHECK(run_bus({node("idle", {})}).empty());
}
