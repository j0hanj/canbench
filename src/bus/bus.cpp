#include "bus/bus.hpp"

#include <optional>

namespace canbench {

namespace {

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

  BusResult result;
  while (auto winner_opt = pick_winner(nodes, counters)) {
    std::size_t winner = *winner_opt;
    QueuedFrame qf = nodes[winner].queue.front();
    nodes[winner].queue.erase(nodes[winner].queue.begin());
    ++sent[winner];

    if (qf.faulty) {
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

    result.log.push_back(
        {nodes[winner].name, qf.frame, qf.faulty, counters[winner], classify(counters[winner])});
  }

  for (std::size_t i = 0; i < nodes.size(); ++i) {
    result.nodes.push_back({nodes[i].name, sent[i] + static_cast<int>(nodes[i].queue.size()),
                            sent[i], counters[i], classify(counters[i])});
  }
  return result;
}

}  // namespace canbench
