// CAN's fault confinement rules - every node keeps a transmit and receive
// error counter (TEC/REC) and climbs through three states as they grow:
//
//   error-active   - normal. everyone starts here.
//   error-passive  - tec or rec >= 128. still on the bus, still sends/reads
//                    frames, just quieter about complaining when something's
//                    wrong (real nodes send passive vs active error flags -
//                    we don't model the flag itself, just the state).
//   bus-off        - tec > 255. the node stops transmitting, full stop.
//                    a real node needs 128 occurrences of 11 recessive bits
//                    in a row to come back online - we don't model recovery,
//                    once you're off you're off for the rest of the sim.
//
// this is the simplified version of ISO 11898-1's rules: real TEC/REC deltas
// depend on which of several error types happened and there's a special
// "rec was already high, drop it to 119-127 instead of decrementing" case.
// none of that here - a good tx is -1, a bad tx is +8, a good rx is -1, a
// bad rx is +1. close enough to see the shape of it: a node that keeps
// sending bad frames climbs toward bus-off, a healthy one stays near zero.

#ifndef CANBENCH_ERRORS_HPP
#define CANBENCH_ERRORS_HPP

#include <cstdint>

namespace canbench {

struct ErrorCounters {
  int tec = 0;
  int rec = 0;
};

enum class BusState : std::uint8_t { kActive, kPassive, kOff };

BusState classify(const ErrorCounters& c);

// the four things that can happen to a node on any given frame
void note_tx_ok(ErrorCounters& c);     // it sent one cleanly
void note_tx_error(ErrorCounters& c);  // its own frame came back bad
void note_rx_ok(ErrorCounters& c);     // it heard someone else's good frame
void note_rx_error(ErrorCounters& c);  // it heard a bad frame on the bus

}  // namespace canbench

#endif
