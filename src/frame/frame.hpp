// classic CAN frame (2.0A/B). not CAN-FD.
//
// Frame is just the contents. the functions below turn one into the actual
// bits that'd go on the wire - crc, bit stuffing, all the fixed fields.
// no sockets, no hardware, just frames and bits.

#ifndef CANBENCH_FRAME_HPP
#define CANBENCH_FRAME_HPP

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace canbench {

inline constexpr int kMaxData = 8;

// one bit on the bus. CAN is wired-AND so dominant (0) beats recessive (1).
enum class Bit : std::uint8_t { kDominant = 0, kRecessive = 1 };

struct Frame {
  std::uint32_t id = 0;   // 11 bits normally, 29 if extended, right-aligned
  bool extended = false;
  bool rtr = false;       // remote frame - asks for data, carries none
  std::uint8_t dlc = 0;   // data length, 0..8
  std::array<std::uint8_t, kMaxData> data{};

  int data_len() const;   // 0 for a remote frame, else dlc (capped at 8)
};

// parse the candump shorthand that socketcan tools spit out:
//   123#DEADBEEF        normal id, 4 bytes
//   1F334455#1122       29-bit id (more than 3 hex digits), 2 bytes
//   200#R  / 200#R3     remote frame, optional dlc
// id is hex. returns nullopt if it's garbage.
std::optional<Frame> parse_short(std::string_view text);

// quick human-readable dump, e.g. "id=0x123 std data dlc=4 [DE AD BE EF]"
std::string describe(const Frame& f);

// the bits the crc covers: SOF + id/control + data, no stuffing yet.
std::vector<Bit> crc_input_bits(const Frame& f);

// CAN's 15-bit crc, poly 0x4599, run MSB first.
std::uint16_t crc15(const std::vector<Bit>& bits);

// which part of the frame a bit belongs to. control = SRR/IDE/RTR/r1/r0,
// lumped together since they're all single bits nobody looks at individually.
enum class Field : std::uint8_t {
  kSof, kId, kControl, kDlc, kData, kCrc,
  kCrcDelim, kAck, kAckDelim, kEof, kIfs
};

struct WireBit {
  Bit level;
  Field field;
  bool stuffed = false;  // true if this is an inserted stuff bit
};

// same sequence as bit_timeline() but each bit tagged with its field and
// whether the stuffing rule inserted it. this is what the waveform view uses.
std::vector<WireBit> annotated_timeline(const Frame& f);

// the whole frame on the wire: SOF through end-of-frame, with stuff bits
// added in across the SOF..crc part. dominant=0, recessive=1.
std::vector<Bit> bit_timeline(const Frame& f);

// bits as a string of '0'/'1'
std::string to_string(const std::vector<Bit>& bits);

}  // namespace canbench

#endif
