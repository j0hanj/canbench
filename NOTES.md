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

## day 3

.dbc parser + signal decode - `src/dbc/`. this is the fun one, raw bytes
finally turn into "2500 rpm".

only reading `BO_` (message) and `SG_` (signal) lines, skipping everything
else - `BU_`, `CM_`, `VAL_`, attributes. `parse_bo` is a one-line sscanf,
`parse_sg` needed a hand-rolled front (the name, and an optional `m0` mux
token before the `:`) then sscanf for the
`start|len@order sign (factor,offset) [min|max] "unit"` tail.

signal bit extraction, the part i knew would bite:
- Intel / little-endian (`@1`): start bit is the LSB, bits just climb through
  the bytes. easy - `raw |= bit << i`.
- Motorola / big-endian (`@0`): start bit is the *MSB*, and the walk
  sawtooths. you count the bit index down to 0 inside a byte, then jump +15
  to land on bit 7 of the next byte. build `raw` MSB-first. took a couple of
  paper diagrams. test: start bit 7, len 16 over `[0x12,0x34]` should give
  `0x1234`.
- signed: if the top bit of the field is set, subtract `1 << length`.
- dbc marks 29-bit ids by setting bit 31, so mask that off and remember it.

`canbench signals drive.log toy.dbc` walks the log, looks up each id, prints
the decoded signals with units + a "no message in the dbc" count. wrote
`toy.dbc` by hand with ids that line up with `drive.log`.

catch2 tests in `dbc_test.cpp` - Intel/Motorola/signed/scaling + a small
parse. still no cmake locally, built with clang++ and eyeballed the output.

## day 4

waveform view - `src/wave/`. wanted something i could actually look at instead
of a string of 0s and 1s.

first had to teach the frame layer to hand back more than a flat bit list.
added `annotated_timeline()` - same bits as `bit_timeline()` but each one
tagged with its field (sof / id / control / dlc / data / crc / delims / ack /
eof / ifs) and a `stuffed` flag. rewrote `bit_timeline()` as a thin wrapper
over it so the old tests didn't move.

`wave()` draws it: a field ruler, then two rows of box characters for the
high/low rails (idle bus is recessive so it starts high, SOF is the first
drop), then a row of `^` under the stuff bits. edges are just
`rising -> ┌┘`, `falling -> ┐└`. one col per bit.

`canbench wave 7DF#0201050000000000` is a good one - all those zero bytes
force 14 stuff bits and you can see them march across.

control bits (SRR/IDE/RTR/r1/r0) all get lumped as "ctl" in the ruler, nobody
reads them one at a time. the ruler labels can crowd each other on tight
frames but it's close enough.

catch2 tests in `wave_test.cpp` - annotated vs flat timeline match, sof/ifs at
the ends, stuff bits only in sof..crc, and the drawing has the rows + one
caret per stuff bit. still building with clang++, no cmake.

## day 5

small one - `arbitration_bits()` + `arbitration_cmp()` in the frame layer, so
the virtual bus has something to call when two nodes talk over each other.

arbitration is just the id bits (MSB first) sent out while everyone watches
the wire. dominant (0) beats recessive (1), so lower id wins. `arbitration_cmp`
walks the two bit fields and returns at the first mismatch.

the fiddly cases, now covered by tests:
- data vs remote, same id: data wins because RTR is dominant on a data frame
- std vs extended with the same 11-bit base: the std frame's field is shorter
  and it wins every contested bit (its IDE is dominant where the ext frame's
  SRR/IDE are recessive), so shorter-and-equal = std takes it

## day 6

`canbench arb frame frame ...` - stable_sort the frames through
`arbitration_cmp` and print them in the order the bus would pick. tiny wrapper
but it makes yesterday's compare function something i can actually see, and
it's a stand-in until the real bus loop exists.

## day 7

virtual bus - `src/bus/`. finally something that isn't just one frame at a
time.

`Node` is a name + a queue of frames. `run_bus()` runs rounds: every node
with a frame left contends with its front-of-queue frame, `arbitration_cmp`
picks the winner, that frame comes off and gets logged, repeat till every
queue's empty. no bit timing, no actual collisions on the wire - just "given
these nodes want to send these frames, in what order do they actually get
out." that's honestly most of what i wanted from arbitration anyway.

ties go to whoever's first in the node list, same as real silicon just wins
an arbitrary race. and a node can't cut in front of its own earlier frames
even if a later one would technically win - it's still a queue.

`canbench sim ecu:100#DEADBEEF,500#00 abs:200#R,100#01 dash:7DF#0201` -
`name:frame,frame,...` per node, comma separated, space between nodes.

catch2 tests in `bus_test.cpp`: cross-node ordering, own-queue ordering stays
put, and the empty cases. still haven't run them for real, no cmake.

## next

- error counters + bus-off - a node racks up faults and eventually goes
  quiet, which is where fault injection actually gets interesting
- fault injection: flip a bit, kill the ack, corrupt a crc mid-transmission
