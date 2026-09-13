#include "bus/bus.hpp"

#include <catch2/catch_test_macros.hpp>

using canbench::BusState;
using canbench::Node;
using canbench::parse_short;
using canbench::QueuedFrame;
using canbench::run_bus;

namespace {

QueuedFrame qf(const char* text, bool faulty = false) {
  return {*parse_short(text), faulty};
}

Node node(std::string name, std::vector<QueuedFrame> queue) {
  Node n;
  n.name = std::move(name);
  n.queue = std::move(queue);
  return n;
}

}  // namespace

TEST_CASE("lowest id across nodes goes first", "[bus]") {
  auto r = run_bus({
      node("dash", {qf("300#00")}),
      node("ecu", {qf("100#00")}),
      node("abs", {qf("200#00")}),
  });

  REQUIRE(r.log.size() == 3);
  CHECK(r.log[0].node == "ecu");
  CHECK(r.log[1].node == "abs");
  CHECK(r.log[2].node == "dash");
}

TEST_CASE("a node's own queue stays in order", "[bus]") {
  // ecu sends 500 then 100 - even though 100 would win outright, it can't
  // jump ahead of ecu's own 500 since that's already at the front
  auto r = run_bus({node("ecu", {qf("500#00"), qf("100#00")}), node("abs", {qf("200#00")})});

  REQUIRE(r.log.size() == 3);
  CHECK(r.log[0].node == "abs");  // 200 beats 500
  CHECK(r.log[1].node == "ecu");  // ecu's 500 finally gets through
  CHECK(r.log[2].node == "ecu");  // then its 100
}

TEST_CASE("everyone eventually gets to send everything", "[bus]") {
  auto r = run_bus({node("a", {qf("100#00"), qf("101#00")}), node("b", {qf("100#01")})});
  CHECK(r.log.size() == 3);
  for (const auto& n : r.nodes) CHECK(n.sent == n.queued);
}

TEST_CASE("empty bus sends nothing", "[bus]") {
  CHECK(run_bus({}).log.empty());
  CHECK(run_bus({node("idle", {})}).log.empty());
}

TEST_CASE("clean frames leave everyone at zero", "[bus]") {
  auto r = run_bus({node("a", {qf("100#00"), qf("100#01")}), node("b", {qf("200#00")})});
  for (const auto& n : r.nodes) {
    CHECK(n.counters.tec == 0);
    CHECK(n.counters.rec == 0);
    CHECK(n.state == BusState::kActive);
  }
}

TEST_CASE("a run of faults pushes the sender through passive to bus-off", "[bus]") {
  std::vector<QueuedFrame> bad;
  for (int i = 0; i < 34; ++i) bad.push_back(qf("100#DEADBEEF", true));
  auto r = run_bus({node("ecu", bad), node("abs", {qf("200#00"), qf("200#00")})});

  // 16 faults -> tec 128, passive. 32 faults -> tec 256, bus-off.
  REQUIRE(r.log.size() >= 32);
  bool saw_passive = false, saw_off = false;
  for (const auto& t : r.log) {
    if (t.node != "ecu") continue;
    if (t.state == BusState::kPassive) saw_passive = true;
    if (t.state == BusState::kOff) saw_off = true;
  }
  CHECK(saw_passive);
  CHECK(saw_off);

  const auto* ecu = &r.nodes[0];
  CHECK(ecu->state == BusState::kOff);
  CHECK(ecu->sent < ecu->queued);  // bus-off cut it off before all 34 went out

  // abs just watched a pile of bad frames go by - its rec climbed but it's
  // nowhere near passive itself
  const auto* abs = &r.nodes[1];
  CHECK(abs->counters.rec > 0);
  CHECK(abs->state == BusState::kActive);
}

TEST_CASE("a node's own sends never move its own rec", "[bus]") {
  // exactly 32 faults -> ecu goes bus-off right as its queue empties, so its
  // whole queue gets out and abs's own clean send is the last thing logged
  std::vector<QueuedFrame> bad;
  for (int i = 0; i < 32; ++i) bad.push_back(qf("100#00", true));
  auto r = run_bus({node("ecu", bad), node("abs", {qf("200#00")})});

  REQUIRE(r.log.size() == 33);
  CHECK(r.log.back().node == "abs");
  const auto* abs = &r.nodes[1];
  CHECK(abs->counters.tec == 0);  // rx events only touch rec, never tec
  CHECK(abs->state == BusState::kActive);
}
