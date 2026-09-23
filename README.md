# canbench

messing around with CAN bus stuff in software.

if you plug an OBD-II reader into a car you get a firehose of CAN frames - little
messages that every module in the car (engine, brakes, dash, etc) uses to talk to
each other. i've been logging these and there's basically no free tool that just
reads a log file and tells you what's in it. the real ones (canalyzer and
friends) cost money and want a hardware interface plugged into an actual car.

so this is me trying to build one that only needs the log file. eventually i want
it to:

- take a frame and show me exactly what goes on the wire (CAN has this annoying
  bit-stuffing + a weird 15-bit crc)
- read a whole candump log and decode every frame
- with a `.dbc` file, turn the raw bytes into real numbers - rpm, speed, coolant
  temp
- simulate a few fake modules talking on a virtual bus so i can test without a car
- let me inject faults (flip a bit, kill the ack, corrupt the crc) and watch the
  error counters trip and a node go bus-off
- check a log against a little spec file and say pass/fail

## right now

frame decode, reading a candump log, and pulling named signals out with a
`.dbc`. you can do:

```
canbench decode 123#DEADBEEF
canbench dump some.log
canbench signals some.log some.dbc
canbench wave 123#DEADBEEF
canbench arb 200#R 100#00 7DF#0201
canbench sim ecu:100#DEADBEEF,500#00 abs:200#R,100#01 dash:7DF#0201
canbench check drive.log drive.spec toy.dbc
```

`decode` prints the fields, the crc, and the raw bit sequence for one frame.
`dump` reads a whole log and lists every frame with its time offset, bus, and
bytes, plus a count of anything that didn't parse. it takes either a
`candump -l` `.log` file or a Vector `.asc` (the CANoe/CANalyzer format) -
picked by the file extension, and `signals`/`check` take either too. same
`LogFile` either way, so nothing downstream cares which one it got.
`signals` matches each logged frame against the `.dbc` and prints the decoded
values - rpm, coolant temp, wheel speeds - with units. handles both Intel and
Motorola bit layouts and signed signals.
`arb` takes a handful of frames and sorts them the way the bus would when they
all start at once - lowest id first, data before remote. it's the arbitration
rule on its own, ahead of the actual virtual bus.
`sim` is that virtual bus - give it a few nodes, each with its own queue of
frames (`name:frame,frame,...`), and it runs arbitration round by round until
every queue is empty, printing the order everything actually went out in. a
node's own frames still go out in the order it queued them - winning
arbitration doesn't let a node cut in front of its earlier frames.

it also does fault injection and error counters now: tag any frame with a
trailing `!` and it's treated as corrupted on the wire. the sender takes a
transmit error (its TEC goes up by 8), everyone else still on the bus takes a
receive error (REC up by 1). enough of those in a row and a node crosses into
error-passive, then bus-off, at which point it stops sending and whatever's
left in its queue never goes out - the same fault confinement rule that keeps
one flaky module from jamming a real car's bus:

```
$ canbench sim ecu:100#DEADBEEF!,...(34 total)... abs:200#00,200#00,200#00
bus order (35 frames sent):
   1  ecu    id=0x100 std data dlc=4 [DE AD BE EF]      tec=8    rec=0    active  [FAULT]
     ...
  16  ecu    id=0x100 std data dlc=4 [DE AD BE EF]      tec=128  rec=0    PASSIVE  [FAULT]
     ...
  32  ecu    id=0x100 std data dlc=4 [DE AD BE EF]      tec=256  rec=0    BUS-OFF  [FAULT]
  33  abs    id=0x200 std data dlc=1 [00]               tec=0    rec=32   active
  34  abs    id=0x200 std data dlc=1 [00]               tec=0    rec=32   active
  35  abs    id=0x200 std data dlc=1 [00]               tec=0    rec=32   active
--
  ecu  sent 32/34  tec=256 rec=0  BUS-OFF  (2 never sent)
  abs  sent 3/3  tec=0 rec=32  active
```
`check` is the last item on the original list - point it at a log and a spec
file, get a pass/fail per rule and an overall exit code (0 if everything
passed, 1 if anything failed, so it's usable in a script). the spec language
is tiny: `present <id>`, `absent <id>`, `range <signal> <min> <max>` (needs a
`.dbc` to decode the signal), `period <id> <min> <max>` (checks the gap
between consecutive sends of that id stays inside `[min, max]` seconds - a
quick way to catch a node that's fallen off its normal send rate). one rule
per line, `#` comments:

```
$ canbench check drive.log toy.spec toy.dbc
PASS  id 0x123 seen 3 times
PASS  id 0x3B1 seen 2 times
PASS  id 0x999 seen 0 times
PASS  EngineSpeed stayed in [0, 16383.8] over 3 frames
PASS  CoolantTemp stayed in [-40, 215] over 3 frames
PASS  Gear stayed in [0, 8] over 2 frames
PASS  id 0x123 gaps stayed in [0.03, 0.07]s over 3 sends
-- 7/7 rules passed
```
`wave` draws the frame the way it goes out on the wire - a square wave with the
fields marked and every stuff bit flagged:

```
id=0x123 std data dlc=4 [DE AD BE EF]   81 bits on the wire, 2 stuffed
  field  SID........ ctlDLC DATA............................ CRC............  A EOF... IFS
    rec  ┐  ┌┐ ┌┐  ┌─┐   ┌┐ ┌─┐┌───┐┌┐┌┐┌─┐┌─┐┌────┐ ┌──┐┌────┐  ┌──┐ ┌─┐┌┐┌──────────────
    dom  └──┘└─┘└──┘ └───┘└─┘ └┘   └┘└┘└┘ └┘ └┘    └─┘  └┘    └──┘  └─┘ └┘└┘
  stuff                                            ^          ^
  (high = recessive/1, low = dominant/0, ^ = stuff bit)
```

## todo

- [x] frame struct + parse the `123#DEADBEEF` shorthand
- [x] crc-15 (poly 0x4599)
- [x] bit stuffing + full on-wire bit layout
- [x] read an actual candump .log file
- [x] .dbc parser -> named signals
- [x] virtual bus w/ arbitration (round-based, no bit timing yet)
- [x] error counters + bus-off
- [x] fault injection (flip a flag on a frame for now, not a real bit-level corrupt)
- [x] spec check w/ pass/fail
- [x] some kind of waveform view (ascii for now)

## build

need cmake + a c++20 compiler. catch2 gets pulled in automatically for tests.

```
cmake -B build
cmake --build build
ctest --test-dir build
```

## data

CAN samples in `test/corpus/` are either hand-typed or from public captures (noted
in the file). nothing off a real car's proprietary db.
