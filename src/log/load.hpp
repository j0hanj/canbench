// bus load: how much of the wire a log was busy. every frame costs however
// many bits it takes on the wire (stuffing, crc, eof, ifs and all - straight
// from bit_timeline), and that total gets divided by how many bits the bus
// could have carried in the same time at a given bitrate.
//
// it's an estimate, not a measurement: arbitration/error frames and the
// real gaps between frames aren't in the log, so this is only what the
// logged frames themselves cost.

#ifndef CANBENCH_LOAD_HPP
#define CANBENCH_LOAD_HPP

#include <optional>

#include "log/log.hpp"

namespace canbench {

struct BusLoad {
  double frame_bits = 0;     // total wire bits of every frame in the log
  double seconds = 0;        // log duration
  std::optional<double> percent;  // nullopt if the log spans no time
};

BusLoad bus_load(const LogFile& log, double bitrate = 500000.0);

}  // namespace canbench

#endif
