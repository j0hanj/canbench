#include "bus/errors.hpp"

#include <algorithm>

namespace canbench {

BusState classify(const ErrorCounters& c) {
  if (c.tec > 255) return BusState::kOff;
  if (c.tec >= 128 || c.rec >= 128) return BusState::kPassive;
  return BusState::kActive;
}

void note_tx_ok(ErrorCounters& c) { c.tec = std::max(0, c.tec - 1); }
void note_tx_error(ErrorCounters& c) { c.tec += 8; }
void note_rx_ok(ErrorCounters& c) { c.rec = std::max(0, c.rec - 1); }
void note_rx_error(ErrorCounters& c) { c.rec += 1; }

}  // namespace canbench
