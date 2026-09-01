# CAN notes

stuff i figured out while building the frame layer. mostly from the Bosch CAN 2.0
spec and the linux can-utils docs. writing it down so i don't have to re-learn it.

## a data frame, in order

```
SOF | arbitration | control | data | CRC | ACK | EOF | IFS
```

- **SOF** - one dominant bit, says "frame starting"
- **arbitration**
  - standard: 11-bit id (high bit first), then the RTR bit
  - extended: 11-bit id, SRR (recessive), IDE (recessive), 18 more id bits, RTR
- **control** - IDE (standard frames only), r0 reserved bit (+ r1 for extended),
  then a 4-bit DLC. classic CAN: DLC 0..8 is that many bytes, 9..15 all mean 8
- **data** - 0..8 bytes, high bit first
- **CRC** - 15 bits, then a recessive delimiter bit
- **ACK** - sender puts a recessive bit in the ack slot, any receiver that liked
  the frame yanks it dominant. then a recessive delimiter
- **EOF** - 7 recessive bits
- **IFS** - 3 more recessive bits before the bus counts as free

## crc-15

poly is `x^15 + x^14 + x^10 + x^8 + x^7 + x^4 + x^3 + 1` = `0x4599`. runs over
SOF + arbitration + control + data, *before* stuffing. init 0, no reflect, no
final xor. quick check: crc of ascii "123456789" should be `0x059E`.

## bit stuffing

from SOF to the end of the crc: after 5 bits in a row that are the same, the
sender jams in one opposite bit, and the receiver throws it away. the crc
delimiter, ack, and eof are *not* stuffed. this is why frames aren't a fixed
length on the wire.

## errors (haven't built this yet)

- each node has a TX and an RX error counter
- counter over 127 -> error-passive. TX counter over 255 -> bus-off, node shuts
  up until it recovers
- an error frame is 6 dominant bits (or 6 recessive if the node's already
  error-passive) then 8 recessive delimiter bits
