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

## day 8

fault injection + error counters + bus-off, all in one go since they're
really one feature - `src/bus/errors.hpp` plus hooking it into `run_bus()`.
this is the one i was most looking forward to.

`errors.hpp` is CAN's fault confinement rules, simplified: every node has a
TEC and REC. good tx = tec-1, bad tx = tec+8, good rx = rec-1, bad rx =
rec+1. tec/rec >= 128 = error-passive, tec > 255 = bus-off and you're done
transmitting for good. real ISO 11898-1 has more nuance (different error
types add different amounts, rec has a weird "don't decrement past 119 once
it's been over 127" carve-out) but this gets the shape right and that's what
i wanted to see.

fault injection is just a bool on a queued frame now - `QueuedFrame.faulty`.
mark one and `run_bus` treats it as corrupted: the sender eats a transmit
error, every other node still on the bus eats a receive error (that's the
real behavior - on a real bus everyone sees a bad crc and flags it, which is
what bumps the sender's tec in the first place). a node that goes bus-off
stops contending immediately, and its remaining queue just never sends -
no recovery, that's future work if i ever care.

cli: `!` after a frame in `sim` marks it faulty -
`ecu:100#DEADBEEF!,200#00` sends one bad frame then one good one. fed it 34
faulted frames from one node and watched it cross into PASSIVE at the 16th
(tec 128) and BUS-OFF at the 32nd (tec 256), then its last 2 frames just
never went out. exactly the thing i wanted this whole project to be able to
show.

catch2 tests: `errors_test.cpp` for the counter math and state thresholds in
isolation, `bus_test.cpp` extended with a full passive->bus-off run and a
check that a bus-off node's leftover frames get reported as never-sent.
still haven't run any of it through ctest, no cmake on this laptop.

## day 9

`brew install cmake` finally. ran ctest for real for the first time since day
1 - all these "haven't run it, no cmake" notes were covering for actually
just eyeballing clang++ output this whole time. 44 tests, 2 failed:

- `parse_short("800#00")` was supposed to get rejected (0x800 doesn't fit in
  11 bits) but it wasn't. the bug: `f.extended = id_text.size() > 3 || *id >
  kStdIdMax` was auto-promoting an out-of-range 3-digit id to extended
  instead of just rejecting it, so "800" quietly became a 29-bit frame with
  id 0x800 instead of an error. extended should only ever come from digit
  count, not from the value overflowing. one-line fix.
- `log.duration() == 0.1` failed with `0.0999999046 == 0.1` - subtracting two
  ~1.65e9 doubles eats enough precision that exact equality doesn't survive.
  not a bug in the code, a bug in the test - swapped it for `WithinAbs`.

both fixed, all 44 green now. glad i finally checked - the id-overflow one
was a real correctness bug that's been sitting there since day 1 and none of
my clang++ eyeball checks would've ever caught it since i never happened to
type an out-of-range 3-digit id.

## day 10

spec check - `src/spec/`. last box on the original todo list, all checked
off now.

spec file is stupidly simple on purpose: `present <id>`, `absent <id>`,
`range <signal> <min> <max>`, `#` comments, one per line. present/absent just
count matching ids in the log. range needs a `.dbc` - looks up which message
has a signal by that name, decodes every occurrence in the log, fails if any
of them land outside the bounds. bad lines in the spec get collected like
everywhere else instead of aborting.

`canbench check drive.log toy.spec toy.dbc` prints PASS/FAIL per rule plus a
count, and the process exit code is 0 only if everything passed - so it's
actually usable as a script/CI gate, not just a printout. tried it against a
deliberately bad spec (`range EngineSpeed 0 5000` when the toy data hits
11127) and got exit 1 with the offending value called out.

edge cases i made sure were tests, not vibes: no dbc given for a range rule
(fail, don't crash), a range rule for a signal name the dbc doesn't have
(fail), a signal that's in the dbc but never shows up in this particular log
(passes - nothing to check isn't a violation).

ran the whole suite through ctest again since cmake's actually installed now
- 51/51 green.

## day 11

`period <id> <min> <max>` rule in the spec language - first thing off the
"probably next" list. checks the gap between consecutive sends of an id
stays inside a window, seconds. reused `Rule.lo/hi` for the gap bounds
instead of adding new fields since it's the same shape as `range`.

fewer than two sends of that id in the log = nothing to compare, so it
passes vacuously, same call i made for range rules on a signal that never
shows up. felt more honest than either failing (there's no violation to
point to) or refusing to run.

added it to `toy.spec` too: `period 123 0.03 0.07` against `drive.log`'s
three 0x123 frames (gaps of ~0.056s and ~0.044s), passes.

tests for the parse, a real gap violation, and the <2-sends case. 54/54
through ctest.

## day 12

second log format - `src/log/asc.hpp` for Vector's `.asc` (what
CANoe/CANalyzer export). reused `LogFile`/`LogEntry`/`LogError` from
`log.hpp` as-is instead of inventing a parallel type - `dump`, `signals`,
`check` don't know or care which reader produced the log they got, main.cpp
just picks by file extension (`read_any_log()`, `.asc` vs anything else).

the frame line format is different enough from candump to be interesting:

```
0.001000 1  123             Rx   d 4 DE AD BE EF
0.002500 1  1F334455x       Rx   d 2 11 22
```
timestamp, channel, id, Rx/Tx, d/r, dlc, data bytes. the neat bit: extended
ids get an explicit trailing `x` instead of candump's "count the hex digits"
convention - actually less ambiguous. header lines (`date`, `base`, `no
internal events logged`, `Begin/End Triggerblock`) don't start with a number,
so the parser just skips any line whose first token isn't a timestamp,
rather than trying to enumerate every header variant CANoe might emit.

remembered the id-overflow bug from day 9 this time - a non-`x` id over
0x7FF is rejected outright instead of getting quietly upgraded to extended.
wrote the test for that specifically before writing the fix, for once.

made `drive.asc` as the exact same frames as `drive.log` and diffed
`dump`/`signals`/`check` output between the two - identical (down to the
float-precision `0.0999999s` vs `0.1s` span quirk, which only candump's
raw-epoch timestamps hit since the asc file's timestamps start at 0). good
sign the abstraction actually holds.

58/58 through ctest.

## next

- everything on the original list is done, plus period checks and now two
  log formats. probably: real bus-off recovery, or timing rules that look
  across different ids (not just one id's own gaps)
