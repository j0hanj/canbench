#include "bus/bus.hpp"

#include <catch2/catch_test_macros.hpp>

using canbench::BusState;
using canbench::Node;
using canbench::parse_short;
using canbench::QueuedFrame;
using canbench::run_bus;
using canbench::WireError;

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

TEST_CASE("a faulty send carries the real error the receiver caught", "[bus]") {
  auto r = run_bus({node("ecu", {qf("100#DEADBEEF", true)})});
  REQUIRE(r.log.size() == 1);
  CHECK(r.log[0].faulty);
  CHECK(r.log[0].wire_error != WireError::kNone);
}

TEST_CASE("a clean send has no wire error", "[bus]") {
  auto r = run_bus({node("ecu", {qf("100#DEADBEEF", false)})});
  REQUIRE(r.log.size() == 1);
  CHECK_FALSE(r.log[0].faulty);
  CHECK(r.log[0].wire_error == WireError::kNone);
}

TEST_CASE("a bus-off node recovers after 128 other frames go by", "[bus]") {
  // ecu goes off on its 32nd fault, then has 2 clean frames left in queue.
  // abs supplies exactly 128 frames of "other bus activity" - enough for
  // ecu to recover and get its last 2 out.
  std::vector<QueuedFrame> ecu_queue;
  for (int i = 0; i < 32; ++i) ecu_queue.push_back(qf("100#DEADBEEF", true));
  ecu_queue.push_back(qf("100#00"));
  ecu_queue.push_back(qf("100#01"));

  std::vector<QueuedFrame> abs_queue;
  for (int i = 0; i < 128; ++i) abs_queue.push_back(qf("200#00"));

  auto r = run_bus({node("ecu", ecu_queue), node("abs", abs_queue)});

  const auto* ecu = &r.nodes[0];
  CHECK(ecu->sent == 34);          // every frame it had queued got out eventually
  CHECK(ecu->state == BusState::kActive);
  CHECK(ecu->counters.tec < 128);  // back below passive, not still smarting from the 32 faults

  // somewhere in the log it actually was bus-off, and somewhere after that
  // it's back to active - recovery has to really happen, not just "ecu
  // never really needed it"
  bool was_off = false, active_after_off = false;
  for (const auto& t : r.log) {
    if (t.node != "ecu") continue;
    if (t.state == BusState::kOff) was_off = true;
    else if (was_off && t.state == BusState::kActive) active_after_off = true;
  }
  CHECK(was_off);
  CHECK(active_after_off);
}

TEST_CASE("a bus-off node with nobody else to hear stays off", "[bus]") {
  // same setup, but abs only has 10 frames - nowhere near the 128 ecu needs
  std::vector<QueuedFrame> ecu_queue;
  for (int i = 0; i < 32; ++i) ecu_queue.push_back(qf("100#DEADBEEF", true));
  ecu_queue.push_back(qf("100#00"));

  std::vector<QueuedFrame> abs_queue;
  for (int i = 0; i < 10; ++i) abs_queue.push_back(qf("200#00"));

  auto r = run_bus({node("ecu", ecu_queue), node("abs", abs_queue)});

  const auto* ecu = &r.nodes[0];
  CHECK(ecu->state == BusState::kOff);
  CHECK(ecu->sent == 32);  // the last clean frame never got out
  CHECK(ecu->sent < ecu->queued);
}
