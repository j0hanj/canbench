// reads a Vector ASC log - the .asc CANoe/CANalyzer spits out. second log
// format alongside candump's .log, same LogFile/LogEntry shape either way so
// dump/signals/check don't care which one they got.
//
// a frame line looks like:
//   0.001000 1  123             Rx   d 4 DE AD BE EF
//   0.002500 1  1F334455x       Rx   d 2 11 22
//   0.004000 2  200             Rx   r 0
// that's timestamp, channel, id (extended ids get a trailing 'x' - that's
// the actual convention, unlike candump's "count the hex digits" trick),
// Rx/Tx (we don't care which), d/r (data or remote), dlc, then dlc data
// bytes for a data frame.
//
// everything else in the file - the `date`/`base` header, blank lines,
// `Begin/End Triggerblock`, internal-event noise - doesn't start with a
// number, so we just skip lines whose first token isn't a timestamp. a line
// that *does* start with a number but doesn't parse as a frame after that
// is a real error and goes in .errors like log.cpp does.

#ifndef CANBENCH_ASC_HPP
#define CANBENCH_ASC_HPP

#include <optional>
#include <string>
#include <string_view>

#include "log/log.hpp"

namespace canbench {

LogFile parse_asc(std::string_view text);
std::optional<LogFile> read_asc(const std::string& path);

}  // namespace canbench

#endif
