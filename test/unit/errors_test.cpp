#include "bus/errors.hpp"

#include <catch2/catch_test_macros.hpp>

using canbench::BusState;
using canbench::classify;
using canbench::ErrorCounters;
using canbench::note_rx_error;
using canbench::note_rx_ok;
using canbench::note_tx_error;
using canbench::note_tx_ok;

TEST_CASE("fresh counters are error-active", "[errors]") {
  CHECK(classify({}) == BusState::kActive);
}

TEST_CASE("tec doesn't go negative", "[errors]") {
  ErrorCounters c;
  note_tx_ok(c);
  CHECK(c.tec == 0);
}

TEST_CASE("a bad tx adds 8, a good one takes 1 back off", "[errors]") {
  ErrorCounters c;
  note_tx_error(c);
  note_tx_error(c);
  CHECK(c.tec == 16);
  note_tx_ok(c);
  CHECK(c.tec == 15);
}

TEST_CASE("128 crosses into error-passive", "[errors]") {
  ErrorCounters c{127, 0};
  CHECK(classify(c) == BusState::kActive);
  note_tx_error(c);  // one more error bumps it to 135
  CHECK(c.tec == 135);
  CHECK(classify(c) == BusState::kPassive);
}

TEST_CASE("rec alone can also trip passive", "[errors]") {
  ErrorCounters c{0, 128};
  CHECK(classify(c) == BusState::kPassive);
}

TEST_CASE("256 is bus-off, 255 is still just passive", "[errors]") {
  CHECK(classify({255, 0}) == BusState::kPassive);
  CHECK(classify({256, 0}) == BusState::kOff);
}

TEST_CASE("rec climbs on rx errors and settles on rx ok", "[errors]") {
  ErrorCounters c;
  for (int i = 0; i < 5; ++i) note_rx_error(c);
  CHECK(c.rec == 5);
  note_rx_ok(c);
  CHECK(c.rec == 4);
}
