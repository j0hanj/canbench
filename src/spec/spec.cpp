#include "spec/spec.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace canbench {

namespace {

std::string_view trim(std::string_view s) {
  auto ws = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && ws(s.front())) s.remove_prefix(1);
  while (!s.empty() && ws(s.back())) s.remove_suffix(1);
  return s;
}

std::vector<std::string_view> split(std::string_view line) {
  std::vector<std::string_view> toks;
  std::size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    std::size_t start = i;
    while (i < line.size() && !std::isspace(static_cast<unsigned char>(line[i]))) ++i;
    if (i > start) toks.push_back(line.substr(start, i - start));
  }
  return toks;
}

std::optional<std::uint32_t> parse_hex_id(std::string_view s) {
  if (s.empty()) return std::nullopt;
  std::string buf(s);
  char* end = nullptr;
  unsigned long v = std::strtoul(buf.c_str(), &end, 16);
  if (end != buf.c_str() + buf.size()) return std::nullopt;
  return static_cast<std::uint32_t>(v);
}

std::optional<double> parse_num(std::string_view s) {
  std::string buf(s);
  char* end = nullptr;
  double v = std::strtod(buf.c_str(), &end);
  if (end != buf.c_str() + buf.size()) return std::nullopt;
  return v;
}

// the message/signal this name lives on, or nullptr if no message in the
// dbc has a signal called that.
const Signal* find_signal(const Dbc& dbc, const std::string& name, std::uint32_t* id_out) {
  for (const auto& [id, msg] : dbc.messages) {
    for (const auto& sig : msg.signals) {
      if (sig.name == name) {
        *id_out = id;
        return &sig;
      }
    }
  }
  return nullptr;
}

std::string hex_id(std::uint32_t id) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "0x%X", id);
  return buf;
}

}  // namespace

SpecFile parse_spec(std::string_view text) {
  SpecFile out;
  std::size_t lineno = 0;
  std::size_t pos = 0;

  while (pos <= text.size()) {
    auto nl = text.find('\n', pos);
    std::string_view raw =
        text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
    pos = (nl == std::string_view::npos) ? text.size() + 1 : nl + 1;
    ++lineno;

    std::string_view line = trim(raw);
    if (line.empty() || line.front() == '#') continue;

    auto toks = split(line);
    bool ok = false;

    if (toks.size() == 2 && (toks[0] == "present" || toks[0] == "absent")) {
      if (auto id = parse_hex_id(toks[1])) {
        out.rules.push_back(
            {toks[0] == "present" ? RuleKind::kPresent : RuleKind::kAbsent, *id, "", 0, 0});
        ok = true;
      }
    } else if (toks.size() == 4 && toks[0] == "range") {
      auto lo = parse_num(toks[2]);
      auto hi = parse_num(toks[3]);
      if (lo && hi) {
        Rule r;
        r.kind = RuleKind::kRange;
        r.signal = std::string(toks[1]);
        r.lo = *lo;
        r.hi = *hi;
        out.rules.push_back(r);
        ok = true;
      }
    } else if (toks.size() == 4 && toks[0] == "period") {
      auto id = parse_hex_id(toks[1]);
      auto lo = parse_num(toks[2]);
      auto hi = parse_num(toks[3]);
      if (id && lo && hi) {
        Rule r;
        r.kind = RuleKind::kPeriod;
        r.id = *id;
        r.lo = *lo;
        r.hi = *hi;
        out.rules.push_back(r);
        ok = true;
      }
    }

    if (!ok) {
      out.errors.push_back("line " + std::to_string(lineno) + ": can't parse '" +
                           std::string(line) + "'");
    }
  }

  return out;
}

std::optional<SpecFile> read_spec(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return std::nullopt;
  std::ostringstream ss;
  ss << in.rdbuf();
  return parse_spec(ss.str());
}

std::vector<RuleResult> check_spec(const LogFile& log, const Dbc* dbc,
                                   const std::vector<Rule>& rules) {
  std::vector<RuleResult> results;

  for (const Rule& rule : rules) {
    if (rule.kind == RuleKind::kPresent || rule.kind == RuleKind::kAbsent) {
      int count = 0;
      for (const auto& e : log.entries)
        if (e.frame.id == rule.id) ++count;

      bool want_present = (rule.kind == RuleKind::kPresent);
      bool passed = want_present ? count > 0 : count == 0;
      std::string detail = "id " + hex_id(rule.id) + " seen " + std::to_string(count) +
                           " time" + (count == 1 ? "" : "s");
      results.push_back({rule, passed, detail});
      continue;
    }

    if (rule.kind == RuleKind::kPeriod) {
      std::vector<double> ts;
      for (const auto& e : log.entries)
        if (e.frame.id == rule.id) ts.push_back(e.ts);

      char buf[128];
      if (ts.size() < 2) {
        std::snprintf(buf, sizeof(buf), "id %s seen fewer than twice, nothing to check period on",
                      hex_id(rule.id).c_str());
        results.push_back({rule, true, buf});
        continue;
      }

      bool passed = true;
      double worst = 0;
      for (std::size_t i = 1; i < ts.size(); ++i) {
        double gap = ts[i] - ts[i - 1];
        if (gap < rule.lo || gap > rule.hi) {
          passed = false;
          worst = gap;
        }
      }

      if (passed) {
        std::snprintf(buf, sizeof(buf), "id %s gaps stayed in [%g, %g]s over %zu sends",
                      hex_id(rule.id).c_str(), rule.lo, rule.hi, ts.size());
      } else {
        std::snprintf(buf, sizeof(buf), "id %s had a gap of %gs, outside [%g, %g]",
                      hex_id(rule.id).c_str(), worst, rule.lo, rule.hi);
      }
      results.push_back({rule, passed, buf});
      continue;
    }

    // range
    if (!dbc) {
      results.push_back({rule, false, "no .dbc given, can't decode '" + rule.signal + "'"});
      continue;
    }
    std::uint32_t id = 0;
    const Signal* sig = find_signal(*dbc, rule.signal, &id);
    if (!sig) {
      results.push_back({rule, false, "no signal named '" + rule.signal + "' in the dbc"});
      continue;
    }

    bool passed = true;
    int checked = 0;
    double worst = 0;
    for (const auto& e : log.entries) {
      if (e.frame.id != id) continue;
      double v = sig->decode(e.frame);
      ++checked;
      if (v < rule.lo || v > rule.hi) {
        passed = false;
        worst = v;
      }
    }

    char buf[128];
    if (checked == 0) {
      std::snprintf(buf, sizeof(buf), "%s never appeared in the log - nothing to check",
                    rule.signal.c_str());
    } else if (passed) {
      std::snprintf(buf, sizeof(buf), "%s stayed in [%g, %g] over %d frame%s",
                    rule.signal.c_str(), rule.lo, rule.hi, checked, checked == 1 ? "" : "s");
    } else {
      std::snprintf(buf, sizeof(buf), "%s hit %g, outside [%g, %g]", rule.signal.c_str(), worst,
                    rule.lo, rule.hi);
    }
    results.push_back({rule, passed, buf});
  }

  return results;
}

}  // namespace canbench
