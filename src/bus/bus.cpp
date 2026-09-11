#include "bus/bus.hpp"

#include <optional>

namespace canbench {

namespace {

// index of the node that wins this round, or nullopt if every queue is
// empty. ties (same arbitration field) go to whoever's listed first - same
// as two real nodes racing, someone has to win an arbitrary coin flip.
std::optional<std::size_t> pick_winner(const std::vector<Node>& nodes) {
  std::optional<std::size_t> best;
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].queue.empty()) continue;
    if (!best || arbitration_cmp(nodes[i].queue.front(), nodes[*best].queue.front()) < 0)
      best = i;
  }
  return best;
}

}  // namespace

std::vector<Transmission> run_bus(std::vector<Node> nodes) {
  std::vector<Transmission> log;
  while (auto winner = pick_winner(nodes)) {
    Node& n = nodes[*winner];
    log.push_back({n.name, n.queue.front()});
    n.queue.erase(n.queue.begin());
  }
  return log;
}

}  // namespace canbench
