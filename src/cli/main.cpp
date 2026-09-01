// canbench cli. only `decode` does anything real yet.

#include <iostream>
#include <string_view>
#include <vector>

#include "frame/frame.hpp"

namespace {

int usage(std::ostream& os) {
  os << "usage: canbench <cmd> [args]\n"
        "  decode <frame>   parse one frame like 123#DEADBEEF and print it\n"
        "  -v / --version\n"
        "  -h / --help\n"
        "later: dump, signals, sim, fault, check\n";
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

  std::cerr << "canbench: dunno what '" << cmd << "' is\n";
  usage(std::cerr);
  return 2;
}
