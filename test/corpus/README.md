# corpus

small CAN traces for testing. keep them tiny and say where they came from.

- hand-typed frames are fine, one per line in the `123#DEADBEEF` form
- real captures: only public ones (can-utils samples, open car-hacking datasets).
  drop a note here with the source

nothing from a paid tool or a specific car's proprietary database.

## files

- `drive.log` - hand-typed, candump `.log` format
  (`(secs.usecs) iface 123#DEADBEEF`). couple of std + ext ids, a remote
  frame, a padded OBD-II style request. for the log reader once it exists.
- `toy.dbc` - hand-written, made up. engine/transmission/wheel-speed/obd
  messages keyed to the ids in `drive.log` so `signals` has something to
  decode. numbers aren't from any real car.
