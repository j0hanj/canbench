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
```

`decode` prints the fields, the crc, and the raw bit sequence for one frame.
`dump` reads a whole `candump -l` file and lists every frame with its time
offset, bus, and bytes, plus a count of anything that didn't parse.
`signals` matches each logged frame against the `.dbc` and prints the decoded
values - rpm, coolant temp, wheel speeds - with units. handles both Intel and
Motorola bit layouts and signed signals.

## todo

- [x] frame struct + parse the `123#DEADBEEF` shorthand
- [x] crc-15 (poly 0x4599)
- [x] bit stuffing + full on-wire bit layout
- [x] read an actual candump .log file
- [x] .dbc parser -> named signals
- [ ] virtual bus w/ arbitration
- [ ] error counters + bus-off
- [ ] fault injection
- [ ] spec check w/ pass/fail
- [ ] some kind of waveform view

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
