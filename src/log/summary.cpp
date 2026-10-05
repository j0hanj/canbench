#include "log/summary.hpp"

#include <algorithm>
#include <map>

namespace canbench {

std::vector<IdSummary> summarize_ids(const LogFile& log) {
  std::map<std::uint32_t, std::vector<double>> times;
  std::map<std::uint32_t, bool> ext;
  for (const auto& e : log.entries) {
    times[e.frame.id].push_back(e.ts);
    ext[e.frame.id] = e.frame.extended;
  }

  std::vector<IdSummary> out;
  for (auto& [id, ts] : times) {
    IdSummary s;
    s.id = id;
    s.extended = ext[id];
    s.count = ts.size();

    if (ts.size() >= 2) {
      std::vector<double> gaps;
      for (std::size_t i = 1; i < ts.size(); ++i) gaps.push_back(ts[i] - ts[i - 1]);
      std::sort(gaps.begin(), gaps.end());
      std::size_t n = gaps.size();
      s.median_gap = (n % 2 == 1) ? gaps[n / 2] : (gaps[n / 2 - 1] + gaps[n / 2]) / 2.0;
    }
    out.push_back(s);
  }
  return out;
}

}  // namespace canbench
