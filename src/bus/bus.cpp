#include "bus/bus.hpp"

#include <optional>

namespace canbench {

namespace {

// how many other frames a bus-off node has to hear before it's allowed back
// on. the real number (128 occurrences of 11 recessive bits in a row) is
// about elapsed bus-idle time, which this round-based sim doesn't model -
// "128 frames went by" is the closest honest stand-in. a node that goes off
// with nobody else left to send anything just stays off for the rest of the
// run, same as the old "no recovery at all" behavior.
constexpr int kBusOffRecoveryFrames = 128;

// index of the node that wins this round, or nullopt if nobody's left to
// send (empty queue or bus-off). ties go to whoever's listed first.
std::optional<std::size_t> pick_winner(const std::vector<Node>& nodes,
                                       const std::vector<ErrorCounters>& counters) {
  std::optional<std::size_t> best;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].queue.empty()) continue;
    if (classify(counters[i]) == BusState::kOff) continue;
    if (!best) { best = i; continue; }
    const Frame& a = nodes[i].queue.front().frame;
    const Frame& b = nodes[*best].queue.front().frame;
    if (arbitration_cmp(a, b) < 0) best = i;
  }
  return best;
}

}  // namespace

BusResult run_bus(std::vector<Node> nodes) {
  std::vector<ErrorCounters> counters(nodes.size());
  std::vector<int> sent(nodes.size(), 0);
  std::vector<int> off_streak(nodes.size(), 0);  // frames heard while bus-off

  BusResult result;
  while (auto winner_opt = pick_winner(nodes, counters)) {
    std::size_t winner = *winner_opt;
    QueuedFrame qf = nodes[winner].queue.front();
    nodes[winner].queue.erase(nodes[winner].queue.begin());
    ++sent[winner];

    WireError wire_error = WireError::kNone;
    if (qf.faulty) {
      // actually corrupt a bit and let the decoder say what broke, rather
      // than just assuming something did
      wire_error = corrupt_one_bit(qf.frame).error;
      note_tx_error(counters[winner]);
    } else {
      note_tx_ok(counters[winner]);
    }

    // everyone else still on the bus hears this send too
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (i == winner) continue;
      if (classify(counters[i]) == BusState::kOff) continue;
      if (qf.faulty) note_rx_error(counters[i]);
      else note_rx_ok(counters[i]);
    }

    result.log.push_back({nodes[winner].name, qf.frame, qf.faulty, wire_error, counters[winner],
                          classify(counters[winner])});

    // bus-off recovery: every node still off counts this frame toward its
    // 128. once it gets there, it's back to error-active and can contend
    // again next round.
    for (std::size_t i = 0; i < nodes.size(); ++i) {
      if (classify(counters[i]) != BusState::kOff) continue;
      if (++off_streak[i] >= kBusOffRecoveryFrames) {
        recover(counters[i]);
        off_streak[i] = 0;
      }
    }
  }

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    result.nodes.push_back({nodes[i].name, sent[i] + static_cast<int>(nodes[i].queue.size()),
                            sent[i], counters[i], classify(counters[i])});
  }
  return result;
}

}  // namespace canbench
