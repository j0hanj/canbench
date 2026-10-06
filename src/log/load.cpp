#include "log/load.hpp"

#include "frame/frame.hpp"

namespace canbench {

BusLoad bus_load(const LogFile& log, double bitrate) {
  BusLoad out;
  for (const auto& e : log.entries)
    out.frame_bits += static_cast<double>(bit_timeline(e.frame).size());
  out.seconds = log.duration();
  if (out.seconds > 0 && bitrate > 0)
    out.percent = 100.0 * out.frame_bits / (out.seconds * bitrate);
  return out;
}

}  // namespace canbench
