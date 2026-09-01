# notes

just a log so i remember what i did.

## day 1

got the skeleton going - cmake, a cli that only does `decode` right now, catch2
for tests.

wrote the frame layer:
- `Frame` struct + `parse_short()` for the `123#DEADBEEF` shorthand (and the
  `1F334455#1122` extended form, and `200#R3` remote frames)
- `crc_input_bits()` builds the SOF/id/control/data bit sequence
- `crc15()` - shift register crc, poly 0x4599. checked it against the known
  crc-15/can value for "123456789" which is 0x059E, matches
- `bit_timeline()` runs the stuffing rule over that and tacks on the fixed
  delimiter/ack/eof bits

the stuffing loop tripped me up for a bit - after you insert a stuff bit it
counts as bit 1 of the next run, not 0. reset `run = 1` not 0.

haven't run the catch2 tests, no cmake on my laptop yet (`brew install cmake`).
just did `clang++ -std=c++20` on frame.cpp + main.cpp to make sure it builds and
`decode` prints something sane.

## next

- read a real candump `.log` (`(1650000000.123456) can0 123#DEADBEEF`)
- start the dbc parser so i can get rpm/speed out instead of raw bytes
- then the fake bus, which is where fault injection + error counters live
