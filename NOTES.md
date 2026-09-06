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

## day 2

candump `.log` reader - `src/log/`.

- `parse_log()` takes the whole file as text, splits each line into
  `(ts) bus frame`, reuses `parse_short()` for the frame part
- blank lines + `#` comments skipped. bad lines don't abort - they go in
  `LogFile::errors` with a line number and a reason, parser keeps going. felt
  better than bailing on the first typo in a 10k-line capture
- timestamp parse: strip the parens, `strtod`, then check it consumed the
  whole string so `(nope)` and `(1.0x)` get rejected instead of silently
  becoming 1.0
- `canbench dump file.log` prints each frame with its offset from the first
  timestamp, the bus, and `describe()` output, then a summary line
- the span print shows `0.0999999s` - float noise, don't care for now

tested against `test/corpus/drive.log`. catch2 tests in `log_test.cpp`, still
haven't run them (no cmake).

## next

- start the dbc parser so i can get rpm/speed out instead of raw bytes
- then the fake bus, which is where fault injection + error counters live
