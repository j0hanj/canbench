// the fake bus, now with faults. every round, every node with something left
// contends, arbitration_cmp picks the winner, that frame goes out. mark a
// queued frame `faulty` to simulate it getting corrupted on the wire (bad
// crc, dropped ack, whatever - we don't care which, just that everyone
// detects it): the sender takes a transmit error, everyone else still on the
// bus takes a receive error, and errors.hpp's rules push their counters
// around. a node whose TEC goes past 255 goes bus-off and stops contending -
// its remaining queued frames never go out.

#ifndef CANBENCH_BUS_HPP
#define CANBENCH_BUS_HPP

#include <string>
#include <vector>

#include "bus/errors.hpp"
#include "frame/frame.hpp"

namespace canbench {

struct QueuedFrame {
  Frame frame;
  bool faulty = false;  // this send gets treated as corrupted on the wire
};

struct Node {
  std::string name;
  std::vector<QueuedFrame> queue;  // sends front-first
};

struct Transmission {
  std::string node;
  Frame frame;
  bool faulty = false;
  ErrorCounters counters;  // sender's counters right after this send
  BusState state;          // sender's state right after this send
};

// per-node tally once the sim stops: how much it asked to send vs how much
// actually went out (the gap is frames stuck behind a bus-off), and its
// final error state.
struct NodeSummary {
  std::string name;
  int queued = 0;
  int sent = 0;
  ErrorCounters counters;
  BusState state = BusState::kActive;
};

struct BusResult {
  std::vector<Transmission> log;
  std::vector<NodeSummary> nodes;  // same order as the input
};

// runs every node's queue against each other till everyone's either empty
// or bus-off. nodes are taken by value since their queues get eaten as we go.
BusResult run_bus(std::vector<Node> nodes);

}  // namespace canbench

#endif
