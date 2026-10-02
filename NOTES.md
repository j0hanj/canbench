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

## day 13

small one - noticed the `.asc` vs `.log` extension check in main.cpp was
case-sensitive-ish (`== ".asc" || == ".ASC"`, so `drive.Asc` would've quietly
gone through the candump parser and failed to make sense of it). also it
lived straight in the cli, untested, unlike literally everything else in
this project.

pulled it out of the cli into `src/log/reader.hpp` - `guess_format()` does a
real case-insensitive suffix check, `read_any_log()` wraps it. same three
callers (`dump`/`signals`/`check`), but now the dispatch logic lives in
canbench_core where it can actually be tested, instead of sitting untested
in main.cpp like it was.

tests in `reader_test.cpp` for the case-insensitivity plus the edge cases
(`weird.asc.log` should go candump - last extension wins, `asc` with no dot
is too short to match). 60/60 through ctest.

## day 14

bit-level fault injection. this was the thing i'd been calling "just a flag on
the frame" since day 8 and it always bugged me - `sim` says a frame is bad but
never says *why* or what a receiver would actually do about it.

so i wrote the other half of the wire: `src/frame/wire.hpp`, `decode_wire()`.
`bit_timeline()` goes frame -> bits, this goes bits -> frame. it destuffs as it
reads (a `WireReader` that mirrors the encoder - nothing before SOF, a stuff bit
counts as bit 1 of the next run, and the last crc bit can be followed by a stuff
bit too), walks sof/id/control/dlc/data/crc, and reports the first thing that
goes wrong:
- stuff error: six equal bits where a stuff bit should be
- form error: sof not dominant, crc/ack delimiter or eof not recessive, srr
  dominant on an extended frame
- crc error: what i computed over what i read != what was on the wire
- truncated: ran out of bits (a flipped dlc sends it hunting for bytes that
  don't exist)

not modelled: bit error and ack error, they're the *transmitter's* checks (it
notices the wire disagrees with what it drove / nobody acked). i just report
whether the ack slot came back dominant. also cheated a little: any dominant eof
bit is a form error, real receivers let the last one slide (overload frame).

`sweep_single_flips()` flips every bit of a frame's timeline one at a time and
decodes each. `canbench inject 123#DEADBEEF` prints it as a per-field table,
`inject <frame> <bit>` does one flip and shows what the receiver ends up seeing.

things i learned from actually running it:
- 77/81 flips of `123#DEADBEEF` are caught. the 4 that aren't: the ack slot
  (flipping it just looks like an ack) and the 3 ifs bits. everything sof..eof
  gets caught, and there's a test that sweeps 14 different frames (std, ext,
  remote, every dlc, all-zeros / all-ones stuffing torture) and asserts that.
  that's the crc-15 + stuffing doing what they're supposed to.
- a flipped bit can break a run of five and orphan a stuff bit. flipping bit 40
  turns `11111` into `11101`, so the real stuff bit two places later (wire bit
  42) no longer follows a run of five - the destuffer reads it as data and
  everything after slides by one, giving `DE AD BA 77` instead of
  `DE AD BE EF`. checked by destuffing both versions by hand: original drops
  stuff bits at 42 and 53, the flipped one only at 53. crc catches it, but it's
  a neat demo of why stuff bits are inside the crc-covered span.
- my first truncation test failed because it chopped only the interframe space,
  which the decoder correctly ignores. test was wrong, decoder was right.

`sim` still uses the `!` flag. next step would be having `!` mean "flip a random
bit and let the decoder decide the error" so tec/rec get bumped by real detected
errors rather than a bool. haven't done that yet.

68/68 through ctest.

## day 15

closed out yesterday's "next": `sim`'s `!` actually flips a bit now instead
of just being a bool that means "pretend this is bad."

`corrupt_one_bit(f)` in `wire.cpp` - flips the last data bit, or the last
control bit if the frame carries no data (remote frames, so there's always
somewhere to flip), and runs it through `decode_wire`. `run_bus` calls that
for every faulty queued frame and stashes the real `WireError` on the
`Transmission` instead of just the bool. tec/rec math is unchanged - still
+8/+1 - the difference is the *reason* is now grounded in an actual
corrupted frame instead of asserted.

`canbench sim` output changed from `[FAULT]` to `[FAULT: crc error]` (it's
basically always a crc error for a data corruption - stuff errors would need
the flip to land right next to a run of five, which `corrupt_one_bit` isn't
aiming for). checked a remote frame (`200#R!`, no data) actually exercises
the control-field fallback instead of silently doing nothing.

tests: `corrupt_one_bit` always trips something (swept the same 14-frame
spread from yesterday, remote frames included), and on the bus side "a
faulty send carries the real error the receiver caught" / "a clean send has
no wire error" - so the bool and the real error can't quietly drift apart
again. 72/72 through ctest.

## day 16

`follows <a> <b> <max_gap>` in the spec language - the other "next" item,
timing rules that look across two different ids instead of just one id's own
gaps.

checks that every occurrence of `a` in the log gets answered by a `b` within
`max_gap` seconds after it - the request/response shape. real motivating
case is OBD-II: you send a `7DF` query, the ecu answers on `7E8`, and if that
takes too long something's wrong.

implementation's a plain nested scan - for each `a` timestamp, look for any
`b` timestamp that's `>= a` and within the window. not fast (O(n*m)) but logs
in this project are hand-typed and tiny, so it doesn't matter yet. an answer
has to come *after* the request - an early `b` with no `a` before it doesn't
count, which the tests check for specifically (staged a log with a `7E8`
sitting before any `7DF` and made sure it still failed).

`a` never showing up in the log passes vacuously, same call as `range` and
`period`. added `id2` to `Rule` for the second id since present/absent/period
only needed one.

manually built a little obd request/response log to check it end to end - a
fast reply passes, a slow second one (0.1s and 0.15s against a 0.05s window)
correctly fails and names the offending timestamp. didn't add it to
`toy.spec` since `drive.log` has no `7E8` frames and I want that file to stay
"everything passes."

76/76 through ctest.

## day 17

`corrupt_one_bit` picks a random bit now instead of always the last data (or
control) bit - the other half of yesterday's "next" item.

first attempt was wrong and i caught it before committing: i had it *search*
for a bit that would specifically cause a stuff error and prefer that, so a
zero-heavy frame would show something other than crc error for once. tried it
on a handful of frames and every single one came back "stuff error" - every
one. turns out the crc is 15 pseudo-random-looking bits, and a run of 5
identical bits shows up somewhere in almost any CAN frame once you include
the crc, so there's basically always a stuff-error bit to find. "prefer
stuff" doesn't give a realistic mix, it gives "always stuff" - just swapped
one monotonous answer for a different monotonous answer.

what i actually wanted: pick uniformly at random from every bit SOF..eof
(skipping the ack slot and ifs, which decode_wire doesn't care about) and let
whatever happens happen. ran `canbench sim ecu:123#DEADBEEF!` 25 times and
tallied the fault kind: 21 crc, 3 form, 1 stuff - which is exactly the shape
i'd expect, since id/data bits vastly outnumber the handful of fixed-form
bits (sof, delimiters, eof) and crc mismatches are what nearly all of them
turn into.

tests had to change shape too - can't assert a specific caught error kind
anymore since it's genuinely random call to call, but every single sample
frame still has to come back as *some* error (ran each one 20x to make sure),
and a 200-flip run of `123#DEADBEEF` has to turn up more than one distinct
kind so a regression back to "always the same bit" would get caught. ran the
whole suite 5 times in a row to make sure the randomness wasn't going to
make something flaky - 76/76 every time.

## day 18

bus-off recovery - the last thing on the "next" list from a while back.

the real rule: a bus-off node needs 128 occurrences of 11 consecutive
recessive bits before it's allowed back on. this sim is round-based (whole
frames, not individual bits with idle time between them), so there's no
clean way to count "11 recessive bits in a row" - closest honest stand-in is
counting 128 *other frames* going by while the node is off. documented that
as exactly what it is, a simplification, right in bus.cpp's header comment
and errors.hpp's.

`recover()` in errors.hpp just zeroes both counters - one-liner, same
pattern as the other note_* functions. `run_bus` tracks an `off_streak` per
node, bumps it for every bus-off node on every transmission (even ones it
didn't send - it's still "hearing" the bus), and calls `recover()` once a
node hits 128. the node picks its queue back up next round like nothing
happened, since classify() just recomputes off the now-zeroed counters.

tried it: 34 faulty frames from one node, 128 clean ones from another sharing
the bus. ecu goes off at frame 32 (as before), and right on schedule - frame
160, which is 32 + 128 - it's back to active and sends its last 2 queued
frames. exactly where the math says it should land.

the honest gap: if there's no other traffic once a node goes off, it just
never recovers in this sim, even though a real bus sitting idle would clear
the recessive-bit sequence almost instantly. wrote a test for that case too
so it's a documented limitation, not a silent one.

tests: recovery actually happens (was off, then active again, finished all
its queued frames), and the "nobody else around" case stays off for good.
plus a focused `errors_test.cpp` case for `recover()` on its own. 79/79
through ctest.

that's everything from the original todo list plus every "next" item it grew
along the way. don't have a clear next thing lined up - whatever's next
probably starts from using this for something instead of adding to it.

## next

- (open - see day 18)
