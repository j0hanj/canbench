// the receiving end. bit_timeline() turns a Frame into wire bits; this goes the
// other way - takes a wire bit sequence, un-stuffs it, walks the fields, and
// either hands back the Frame or says which error a real receiver would trip.
//
// that makes bit-level fault injection possible: flip one bit of a frame's
// timeline, decode it, see what breaks. the errors CAN receivers can detect:
//
//   stuff error - six equal bits in a row inside the stuffed part
//   form error  - a fixed-form bit is wrong (SOF not dominant, CRC/ACK
//                 delimiter or EOF not recessive, SRR dominant on an ext frame)
//   crc error   - the crc we computed over what we read != the one on the wire
//   truncated   - ran out of bits mid-frame (usually a flip that changed the
//                 dlc, so we went looking for bytes that aren't there)
//
// not modelled: bit error (the transmitter sees a different level than it
// drove - that's a sender-side check) and ack error (nobody pulled the ack
// slot low - also the sender's problem). we just report whether the ack slot
// came back dominant. also simplified: any dominant EOF bit counts as a form
// error, but real receivers let a dominant *last* EOF bit slide (overload).

#ifndef CANBENCH_WIRE_HPP
#define CANBENCH_WIRE_HPP

#include <cstddef>
#include <optional>
#include <vector>

#include "frame/frame.hpp"

namespace canbench {

enum class WireError { kNone, kStuff, kForm, kCrc, kTruncated };

struct WireDecode {
  WireError error = WireError::kNone;
  std::size_t error_at = 0;    // wire bit index where it got caught
  std::optional<Frame> frame;  // set once the fields parsed, even on a crc error
  bool acked = false;          // ack slot came back dominant
};

WireDecode decode_wire(const std::vector<Bit>& wire);

const char* error_name(WireError e);

// one row per bit of the frame's timeline: what happens if just that bit gets
// flipped on the wire.
struct FlipResult {
  std::size_t bit = 0;
  Field field = Field::kSof;
  bool stuffed = false;
  Bit was = Bit::kRecessive;
  WireDecode outcome;
};

std::vector<FlipResult> sweep_single_flips(const Frame& f);

}  // namespace canbench

#endif
