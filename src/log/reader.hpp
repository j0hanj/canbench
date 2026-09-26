// picks candump .log vs vector .asc by file extension so dump/signals/check
// don't have to care which reader actually produced their LogFile.

#ifndef CANBENCH_READER_HPP
#define CANBENCH_READER_HPP

#include <optional>
#include <string>

#include "log/log.hpp"

namespace canbench {

enum class LogFormat { kCandump, kAsc };

// case-insensitive, just looks at the last 4 chars. anything that isn't
// ".asc" is treated as candump - that's the format everything started with.
LogFormat guess_format(const std::string& path);

std::optional<LogFile> read_any_log(const std::string& path);

}  // namespace canbench

#endif
