// canbench cli. `decode` does one frame, `dump`/`signals`/`check` read a log -
// candump .log or vector .asc, picked by extension.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "bus/bus.hpp"
#include "dbc/dbc.hpp"
#include "frame/frame.hpp"
#include "frame/wire.hpp"
#include "log/reader.hpp"
#include "spec/spec.hpp"
#include "wave/wave.hpp"

namespace {

int usage(std::ostream& os) {
  os << "usage: canbench <cmd> [args]\n"
        "  decode <frame>   parse one frame like 123#DEADBEEF and print it\n"
        "  dump <file.log|file.asc>  list every frame (candump .log or vector .asc)\n"
        "  signals <file.log|.asc> <file.dbc>   decode named signals from a log\n"
        "  wave <frame>     draw one frame as an ascii square wave\n"
        "  arb <frame>...   sort frames into bus arbitration order\n"
        "  sim <node>:<frame>[!][,<frame>[!]...] ...   run a fake bus with error\n"
        "      counters + bus-off. '!' after a frame injects a fault on it\n"
        "  check <file.log|.asc> <file.spec> [file.dbc]   run a log against a spec\n"
        "  inject <frame> [bit]   flip wire bits and see which error a receiver trips\n"
        "      (no bit = sweep every bit of the frame)\n"
        "  -v / --version\n"
        "  -h / --help\n";
  return 0;
}

int decode(std::string_view text) {
  auto f = canbench::parse_short(text);
  if (!f) {
    std::cerr << "canbench: can't parse '" << text << "'\n";
    return 1;
  }
  auto in = canbench::crc_input_bits(*f);
  std::cout << canbench::describe(*f) << '\n'
            << "crc15   0x" << std::hex << canbench::crc15(in) << std::dec << '\n'
            << "on-wire " << canbench::to_string(canbench::bit_timeline(*f)) << '\n';
  return 0;
}

int dump(std::string_view path) {
  auto log = canbench::read_any_log(std::string(path));
  if (!log) {
    std::cerr << "canbench: can't open '" << path << "'\n";
    return 1;
  }

  double t0 = log->start_ts();
  std::set<std::uint32_t> ids;
  for (const auto& e : log->entries) {
    ids.insert(e.frame.id);
    char rel[16];
    std::snprintf(rel, sizeof(rel), "%9.6f", e.ts - t0);
    std::cout << rel << "  " << e.bus << "  " << canbench::describe(e.frame)
              << '\n';
  }

  std::cout << "-- " << log->entries.size() << " frames, "
            << ids.size() << " unique ids, " << log->duration() << "s span";
  if (!log->errors.empty()) std::cout << ", " << log->errors.size() << " bad lines";
  std::cout << '\n';
  for (const auto& err : log->errors)
    std::cerr << "  line " << err.line << ": " << err.why << " -> " << err.text
              << '\n';
  return 0;
}

int signals(std::string_view log_path, std::string_view dbc_path) {
  auto log = canbench::read_any_log(std::string(log_path));
  if (!log) {
    std::cerr << "canbench: can't open '" << log_path << "'\n";
    return 1;
  }
  auto dbc = canbench::read_dbc(std::string(dbc_path));
  if (!dbc) {
    std::cerr << "canbench: can't open '" << dbc_path << "'\n";
    return 1;
  }

  double t0 = log->start_ts();
  int decoded = 0, unknown = 0;
  for (const auto& e : log->entries) {
    const auto* msg = dbc->find(e.frame.id);
    if (!msg) {
      ++unknown;
      continue;
    }
    ++decoded;
    char line[96];
    std::snprintf(line, sizeof(line), "%9.6f  0x%X %s", e.ts - t0, e.frame.id,
                  msg->name.c_str());
    std::cout << line << '\n';
    for (const auto& sig : msg->signals) {
      std::snprintf(line, sizeof(line), "    %-20s %12.3f %s", sig.name.c_str(),
                    sig.decode(e.frame), sig.unit.c_str());
      std::cout << line << '\n';
    }
  }

  std::cout << "-- " << decoded << " frames decoded, " << unknown
            << " with no message in the dbc\n";
  return 0;
}

int wave(std::string_view text) {
  auto f = canbench::parse_short(text);
  if (!f) {
    std::cerr << "canbench: can't parse '" << text << "'\n";
    return 1;
  }
  std::cout << canbench::wave(*f) << '\n';
  return 0;
}

int arb(const std::vector<std::string_view>& texts) {
  std::vector<canbench::Frame> frames;
  for (auto t : texts) {
    auto f = canbench::parse_short(t);
    if (!f) {
      std::cerr << "canbench: can't parse '" << t << "'\n";
      return 1;
    }
    frames.push_back(*f);
  }

  // stable so frames with the same arbitration field keep the order given
  std::stable_sort(frames.begin(), frames.end(),
                   [](const canbench::Frame& a, const canbench::Frame& b) {
                     return canbench::arbitration_cmp(a, b) < 0;
                   });

  std::cout << "arbitration order (first one gets the bus):\n";
  for (std::size_t i = 0; i < frames.size(); ++i)
    std::cout << "  " << (i + 1) << "  " << canbench::describe(frames[i]) << '\n';
  return 0;
}

// "ecu:100#DEADBEEF,200#00!" -> a Node named "ecu" with those two frames,
// the second one flagged as faulty (trailing '!' = gets corrupted on the wire)
std::optional<canbench::Node> parse_node(std::string_view text) {
  auto colon = text.find(':');
  if (colon == std::string_view::npos) return std::nullopt;

  canbench::Node n;
  n.name = std::string(text.substr(0, colon));
  std::string_view rest = text.substr(colon + 1);
  while (!rest.empty()) {
    auto comma = rest.find(',');
    std::string_view tok = rest.substr(0, comma);
    canbench::QueuedFrame qf;
    if (!tok.empty() && tok.back() == '!') {
      qf.faulty = true;
      tok.remove_suffix(1);
    }
    auto f = canbench::parse_short(tok);
    if (!f) return std::nullopt;
    qf.frame = *f;
    n.queue.push_back(qf);
    rest = (comma == std::string_view::npos) ? std::string_view{} : rest.substr(comma + 1);
  }
  return n;
}

const char* state_name(canbench::BusState s) {
  switch (s) {
    case canbench::BusState::kActive: return "active";
    case canbench::BusState::kPassive: return "PASSIVE";
    case canbench::BusState::kOff: return "BUS-OFF";
  }
  return "?";
}

int sim(const std::vector<std::string_view>& texts) {
  std::vector<canbench::Node> nodes;
  for (auto t : texts) {
    auto n = parse_node(t);
    if (!n) {
      std::cerr << "canbench: bad node '" << t
                << "', want name:frame[!][,frame[!]...] ('!' = inject a fault)\n";
      return 1;
    }
    nodes.push_back(std::move(*n));
  }

  auto result = canbench::run_bus(std::move(nodes));
  std::cout << "bus order (" << result.log.size() << " frames sent):\n";
  for (std::size_t i = 0; i < result.log.size(); ++i) {
    const auto& t = result.log[i];
    char line[128];
    std::snprintf(line, sizeof(line), "  %2zu  %-6s %-42s tec=%-4d rec=%-4d %s",
                  i + 1, t.node.c_str(), canbench::describe(t.frame).c_str(),
                  t.counters.tec, t.counters.rec, state_name(t.state));
    std::cout << line;
    if (t.faulty) std::cout << "  [FAULT]";
    std::cout << '\n';
  }

  std::cout << "--\n";
  for (const auto& n : result.nodes) {
    std::cout << "  " << n.name << "  sent " << n.sent << "/" << n.queued
              << "  tec=" << n.counters.tec << " rec=" << n.counters.rec << "  "
              << state_name(n.state);
    if (n.sent < n.queued) std::cout << "  (" << (n.queued - n.sent) << " never sent)";
    std::cout << '\n';
  }
  return 0;
}

const char* field_name(canbench::Field f) {
  switch (f) {
    case canbench::Field::kSof: return "SOF";
    case canbench::Field::kId: return "id";
    case canbench::Field::kControl: return "control";
    case canbench::Field::kDlc: return "dlc";
    case canbench::Field::kData: return "data";
    case canbench::Field::kCrc: return "crc";
    case canbench::Field::kCrcDelim: return "crc delim";
    case canbench::Field::kAck: return "ack slot";
    case canbench::Field::kAckDelim: return "ack delim";
    case canbench::Field::kEof: return "eof";
    case canbench::Field::kIfs: return "ifs";
  }
  return "?";
}

int inject(std::string_view text, std::optional<std::size_t> bit) {
  auto f = canbench::parse_short(text);
  if (!f) {
    std::cerr << "canbench: can't parse '" << text << "'\n";
    return 1;
  }
  auto sweep = canbench::sweep_single_flips(*f);

  if (bit) {
    if (*bit >= sweep.size()) {
      std::cerr << "canbench: bit " << *bit << " is past the end (frame is " << sweep.size()
                << " bits on the wire)\n";
      return 1;
    }
    const auto& r = sweep[*bit];
    std::cout << canbench::describe(*f) << '\n'
              << "flip      wire bit " << r.bit << " (" << field_name(r.field)
              << (r.stuffed ? ", stuff bit" : "") << "), "
              << (r.was == canbench::Bit::kDominant ? "0 -> 1" : "1 -> 0") << '\n';
    if (r.outcome.error == canbench::WireError::kNone) {
      std::cout << "receiver  no error" << (r.outcome.acked ? " (ack slot reads dominant = acked)" : "")
                << '\n';
    } else {
      std::cout << "receiver  " << canbench::error_name(r.outcome.error) << ", caught at wire bit "
                << r.outcome.error_at << '\n';
    }
    if (r.outcome.frame) std::cout << "sees      " << canbench::describe(*r.outcome.frame) << '\n';
    return 0;
  }

  // sweep: tally what each single-bit flip turned into, per field
  constexpr int kFields = 11;
  constexpr int kKinds = 5;  // none, stuff, form, crc, truncated
  std::array<std::array<int, kKinds>, kFields> tally{};
  std::array<int, kFields> bits{};
  for (const auto& r : sweep) {
    ++bits[static_cast<int>(r.field)];
    ++tally[static_cast<int>(r.field)][static_cast<int>(r.outcome.error)];
  }

  std::cout << sweep.size() << " single-bit flips of " << canbench::describe(*f) << "\n\n";
  std::printf("  %-10s %5s %7s %6s %5s %6s %5s\n", "field", "bits", "stuff", "form", "crc",
              "trunc", "ok");
  int undetected = 0;
  for (int i = 0; i < kFields; ++i) {
    if (bits[i] == 0) continue;
    const auto& t = tally[i];
    std::printf("  %-10s %5d %7d %6d %5d %6d %5d\n", field_name(static_cast<canbench::Field>(i)),
                bits[i], t[1], t[2], t[3], t[4], t[0]);
    undetected += t[0];
  }
  std::cout << "\n" << (sweep.size() - undetected) << "/" << sweep.size()
            << " flips caught by the receiver. stuff/form/crc/trunc = which error the flip\n"
               "tripped. 'ok' = nothing tripped (flipping the ack slot just reads as an ack, and\n"
               "the ifs after eof isn't looked at).\n";
  return 0;
}

int check(std::string_view log_path, std::string_view spec_path, std::string_view dbc_path) {
  auto log = canbench::read_any_log(std::string(log_path));
  if (!log) {
    std::cerr << "canbench: can't open '" << log_path << "'\n";
    return 2;
  }
  auto spec = canbench::read_spec(std::string(spec_path));
  if (!spec) {
    std::cerr << "canbench: can't open '" << spec_path << "'\n";
    return 2;
  }
  for (const auto& err : spec->errors) std::cerr << "canbench: " << err << '\n';

  std::optional<canbench::Dbc> dbc;
  if (!dbc_path.empty()) {
    dbc = canbench::read_dbc(std::string(dbc_path));
    if (!dbc) {
      std::cerr << "canbench: can't open '" << dbc_path << "'\n";
      return 2;
    }
  }

  auto results = canbench::check_spec(*log, dbc ? &*dbc : nullptr, spec->rules);
  int failed = 0;
  for (const auto& r : results) {
    std::cout << (r.passed ? "PASS" : "FAIL") << "  " << r.detail << '\n';
    if (!r.passed) ++failed;
  }

  std::cout << "-- " << (results.size() - failed) << "/" << results.size() << " rules passed\n";
  return failed == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string_view> args(argv + 1, argv + argc);
  if (args.empty()) {
    usage(std::cerr);
    return 2;
  }

  auto cmd = args[0];
  if (cmd == "-v" || cmd == "--version") {
    std::cout << "canbench 0.0.0\n";
    return 0;
  }
  if (cmd == "-h" || cmd == "--help") return usage(std::cout);
  if (cmd == "decode") {
    if (args.size() != 2) {
      std::cerr << "canbench: decode wants one frame arg\n";
      return 2;
    }
    return decode(args[1]);
  }
  if (cmd == "dump") {
    if (args.size() != 2) {
      std::cerr << "canbench: dump wants one .log file\n";
      return 2;
    }
    return dump(args[1]);
  }
  if (cmd == "signals") {
    if (args.size() != 3) {
      std::cerr << "canbench: signals wants a .log and a .dbc\n";
      return 2;
    }
    return signals(args[1], args[2]);
  }
  if (cmd == "wave") {
    if (args.size() != 2) {
      std::cerr << "canbench: wave wants one frame arg\n";
      return 2;
    }
    return wave(args[1]);
  }
  if (cmd == "arb") {
    if (args.size() < 3) {
      std::cerr << "canbench: arb wants at least two frames\n";
      return 2;
    }
    return arb({args.begin() + 1, args.end()});
  }
  if (cmd == "sim") {
    if (args.size() < 2) {
      std::cerr << "canbench: sim wants at least one name:frame[,frame...]\n";
      return 2;
    }
    return sim({args.begin() + 1, args.end()});
  }
  if (cmd == "inject") {
    if (args.size() != 2 && args.size() != 3) {
      std::cerr << "canbench: inject wants a frame and optionally a bit index\n";
      return 2;
    }
    std::optional<std::size_t> bit;
    if (args.size() == 3) {
      std::string b(args[2]);
      char* end = nullptr;
      unsigned long v = std::strtoul(b.c_str(), &end, 10);
      if (b.empty() || *end != '\0') {
        std::cerr << "canbench: '" << b << "' isn't a bit index\n";
        return 2;
      }
      bit = static_cast<std::size_t>(v);
    }
    return inject(args[1], bit);
  }
  if (cmd == "check") {
    if (args.size() != 3 && args.size() != 4) {
      std::cerr << "canbench: check wants a .log and a .spec, and optionally a .dbc\n";
      return 2;
    }
    return check(args[1], args[2], args.size() == 4 ? args[3] : std::string_view{});
  }

  std::cerr << "canbench: dunno what '" << cmd << "' is\n";
  usage(std::cerr);
  return 2;
}
