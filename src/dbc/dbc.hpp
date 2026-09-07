// a small .dbc reader. enough of it to turn raw CAN bytes into real numbers.
//
// a .dbc file describes what every message on the bus means. the two lines
// that matter here:
//
//   BO_ 256 EngineData: 8 ECU
//    SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] "rpm" Dash
//
// BO_ is a message (id, name, length). SG_ is one signal packed into it:
// start bit, bit length, byte order (@1 = little-endian/Intel, @0 =
// big-endian/Motorola), sign (+ unsigned, - signed), then (factor,offset)
// so physical = raw * factor + offset, then [min|max] and a unit string.
//
// everything else in the file (BU_, CM_, VAL_, attributes...) is ignored for
// now. multiplexed signals are parsed but the mux switch is ignored.

#ifndef CANBENCH_DBC_HPP
#define CANBENCH_DBC_HPP

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "frame/frame.hpp"

namespace canbench {

struct Signal {
  std::string name;
  int start_bit = 0;         // dbc numbering: LSB pos for Intel, MSB pos for Motorola
  int length = 0;            // bits
  bool little_endian = true; // @1 Intel vs @0 Motorola
  bool is_signed = false;    // trailing - vs +
  double factor = 1.0;
  double offset = 0.0;
  double min = 0.0;
  double max = 0.0;
  std::string unit;

  // pull this signal's raw bits out of a frame and apply factor/offset.
  double decode(const Frame& f) const;

  // the raw integer before scaling - sign-extended if is_signed.
  std::int64_t decode_raw(const Frame& f) const;
};

struct Message {
  std::uint32_t id = 0;      // 11 or 29 bit, high bit already stripped
  bool extended = false;
  std::string name;
  int dlc = 0;
  std::vector<Signal> signals;
};

struct Dbc {
  std::map<std::uint32_t, Message> messages;   // keyed by id

  const Message* find(std::uint32_t id) const;
};

// parse .dbc text. lenient - skips lines it doesn't understand. returns a
// Dbc even if it's empty; nullopt is reserved for "couldn't read the file".
Dbc parse_dbc(std::string_view text);
std::optional<Dbc> read_dbc(const std::string& path);

}  // namespace canbench

#endif
