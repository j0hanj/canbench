// draws a CAN frame as an ASCII square wave, the way you'd see it on a scope.
//
// idle bus is recessive (high). SOF pulls it dominant (low). the trace runs
// SOF -> id -> control -> dlc -> data -> crc -> delimiters -> ack -> eof, with
// stuff bits marked underneath. it's just the bit_timeline drawn with box
// characters - two rows for the high/low rails, a field ruler on top, and a
// row of '^' under every inserted stuff bit.

#ifndef CANBENCH_WAVE_HPP
#define CANBENCH_WAVE_HPP

#include <string>

#include "frame/frame.hpp"

namespace canbench {

// the full multi-line drawing, newline-separated, no trailing newline.
std::string wave(const Frame& f);

}  // namespace canbench

#endif
