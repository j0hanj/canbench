// a fake bus. no timing, no wires - just "which node's frame goes out next."
//
// give it a handful of nodes, each with a queue of frames it wants to send.
// every round, every node with something left contends, arbitration_cmp picks
// the winner, that frame goes out and comes off the front of its queue. keep
// going till every queue is empty. that's the whole simulation for now - real
// bit timing, actual collisions, error counters, all later.

#ifndef CANBENCH_BUS_HPP
#define CANBENCH_BUS_HPP

#include <string>
#include <vector>

#include "frame/frame.hpp"

namespace canbench {

struct Node {
  std::string name;
  std::vector<Frame> queue;  // frames waiting to send, front sends first
};

struct Transmission {
  std::string node;  // which node won that round
  Frame frame;
};

// runs the whole thing to completion and returns the order frames went out
// in. nodes are taken by value since their queues get eaten as we go.
std::vector<Transmission> run_bus(std::vector<Node> nodes);

}  // namespace canbench

#endif
