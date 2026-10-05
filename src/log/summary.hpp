// per-id overview of a log: how many times each id went out and how often.
// the median gap is what you'd eyeball for "does this node send at a steady
// rate" before writing a period rule for it.

#ifndef CANBENCH_SUMMARY_HPP
#define CANBENCH_SUMMARY_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "log/log.hpp"

namespace canbench {

struct IdSummary {
  std::uint32_t id = 0;
  bool extended = false;
  std::size_t count = 0;
  std::optional<double> median_gap;  // seconds between sends, nullopt if <2 sends
};

// sorted by id. gaps are measured in log order, same as period rules.
std::vector<IdSummary> summarize_ids(const LogFile& log);

}  // namespace canbench

#endif
