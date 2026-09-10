// canbench cli. `decode` does one frame, `dump` reads a candump .log.

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "dbc/dbc.hpp"
#include "frame/frame.hpp"
#include "log/log.hpp"
#include "wave/wave.hpp"

namespace {

int usage(std::ostream& os) {
  os << "usage: canbench <cmd> [args]\n"
        "  decode <frame>   parse one frame like 123#DEADBEEF and print it\n"
        "  dump <file.log>  read a candump .log and list every frame\n"
        "  signals <file.log> <file.dbc>   decode named signals from a log\n"
        "  wave <frame>     draw one frame as an ascii square wave\n"
        "  arb <frame>...   sort frames into bus arbitration order\n"
        "  -v / --version\n"
        "  -h / --help\n"
        "later: sim, fault, check\n";
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
  auto log = canbench::read_log(std::string(path));
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
  auto log = canbench::read_log(std::string(log_path));
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

  std::cerr << "canbench: dunno what '" << cmd << "' is\n";
  usage(std::cerr);
  return 2;
}
