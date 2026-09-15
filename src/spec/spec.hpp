// the last item on the todo list: point this at a log (and a .dbc, if you
// want signal-range rules) and a little spec file, get a pass/fail.
//
// spec file is one rule per line, `#` comments, blank lines ignored:
//
//   present 123        - id 0x123 has to show up somewhere in the log
//   absent 666         - id 0x666 must never show up
//   range EngineSpeed 0 8000   - every EngineSpeed value in the log has to
//                                land inside [0, 8000] (needs a .dbc so we
//                                know how to decode EngineSpeed)
//
// ids are hex, no "0x". that's the whole language for now - no timing rules,
// no "signal X implies signal Y", just presence/absence/range.

#ifndef CANBENCH_SPEC_HPP
#define CANBENCH_SPEC_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "dbc/dbc.hpp"
#include "log/log.hpp"

namespace canbench {

enum class RuleKind { kPresent, kAbsent, kRange };

struct Rule {
  RuleKind kind;
  std::uint32_t id = 0;    // present/absent
  std::string signal;      // range
  double lo = 0, hi = 0;   // range
};

struct SpecFile {
  std::vector<Rule> rules;
  std::vector<std::string> errors;  // lines that didn't parse, "line N: ..."
};

SpecFile parse_spec(std::string_view text);
std::optional<SpecFile> read_spec(const std::string& path);

struct RuleResult {
  Rule rule;
  bool passed;
  std::string detail;  // human-readable "why", pass or fail
};

// dbc can be null - only needed for `range` rules, present/absent don't care.
std::vector<RuleResult> check_spec(const LogFile& log, const Dbc* dbc,
                                   const std::vector<Rule>& rules);

}  // namespace canbench

#endif
