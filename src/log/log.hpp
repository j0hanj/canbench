// reads a candump .log file - the kind `candump -l` writes.
//
// each line looks like:
//   (1650000000.123456) can0 123#DEADBEEF
// that's (timestamp) interface frame, where the frame part is the same
// shorthand parse_short() already handles. blank lines and lines starting
// with '#' are ignored. anything else that won't parse goes in .errors and
// we keep going - one bad line shouldn't kill the whole read.

#ifndef CANBENCH_LOG_HPP
#define CANBENCH_LOG_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame/frame.hpp"

namespace canbench {

struct LogEntry {
  double ts = 0.0;       // seconds, straight from the (...) field
  std::string bus;       // "can0", "vcan0", whatever
  Frame frame;
};

struct LogError {
  std::size_t line = 0;  // 1-based line number in the source
  std::string text;      // the offending line, trimmed
  std::string why;       // short reason
};

struct LogFile {
  std::vector<LogEntry> entries;
  std::vector<LogError> errors;

  // relative time helpers - handy for printing / plotting later
  double start_ts() const;   // ts of the first entry, 0 if empty
  double duration() const;   // last ts - first ts, 0 if <2 entries
};

// parse candump .log text. never throws, bad lines land in .errors.
LogFile parse_log(std::string_view text);

// same thing but reads the file off disk first. nullopt if it can't open.
std::optional<LogFile> read_log(const std::string& path);

}  // namespace canbench

#endif
