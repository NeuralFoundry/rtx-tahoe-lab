#include "driver/RuntimeDmaProtocol.hpp"
#include <cstdio>
#include <cstdlib>
namespace R = RuntimeDmaProtocol;
static unsigned checks;
static void check(bool ok) { ++checks; if (!ok) { std::fprintf(stderr, "check %u failed\n", checks); std::abort(); } }
int main() {
  R::Session s;
  check(s.checkWrite(0, 0, 1) == R::Error::InvalidState);
  check(s.begin(R::Error::Ok).ok());
  for (unsigned r = 0; r < R::ResourceCount; ++r) {
    check(s.checkRead(r, 0, 1) == R::Error::IncompleteReads);
    check(s.checkSync(r, 1) == R::Error::IncompleteWrites);
    check(s.checkSync(r, 3) == R::Error::InvalidLength);
    check(s.checkWrite(r, R::Sizes[r], 1) == R::Error::InvalidLength);
    check(s.checkWrite(r, ~R::U64(0), 2) == R::Error::InvalidLength);
    check(s.checkWrite(r, R::Sizes[r] - 1, 1) == R::Error::Ok);
  }
  check(s.publish().ok());
  for (unsigned r = 0; r < R::ResourceCount; ++r) {
    // Two updates of the same bytes, plus an unrelated alias, under one mapping.
    for (unsigned round = 0; round < 3; ++round) {
      check(s.write(r, R::Sizes[r] - 17, 17).ok());
      check(s.checkRead(r, 0, 1) == R::Error::IncompleteReads);
      check(s.checkSync(r, 1) == R::Error::IncompleteWrites);
      check(s.sync(r, 2).ok());
      check(s.checkRead(r, 0, 1) == R::Error::IncompleteReads);
      check(s.sync(r, 1).ok());
      check(s.read(r, R::Sizes[r] - 17, 17).ok());
      check(s.read(r, 0, 1).ok());
      check(s.outEpoch[r] == s.writeEpoch[r] && s.inEpoch[r] == s.writeEpoch[r]);
    }
    check(s.written[r] == 51 && s.readBytes[r] == 54 && s.outCount[r] == 4 && s.inCount[r] == 3);
  }
  check(s.finish().ok()); check(!s.resourcesHeld()); check(s.cleanupVerified);
  check(s.checkRead(0, 0, 1) == R::Error::InvalidState);
  check(s.checkSync(0, 1) == R::Error::InvalidState);
  check(s.abort().ok());
  R::Session dirty; dirty.begin(R::Error::Ok); dirty.publish(); dirty.write(0, 0, 1);
  check(!dirty.sync(0, 1).ok()); check(dirty.resourcesHeld());
  check(dirty.inCount[0] == 0); check(dirty.abort().ok());
  for (unsigned failure = 0; failure < 5; ++failure) {
    R::Session f; f.begin(R::Error::Ok); f.publish(); f.sync(0, 1);
    if (failure == 0) check(!f.write(0, 0, 1, false).ok());
    if (failure == 1) check(!f.sync(0, 1, false).ok());
    if (failure == 2) check(!f.read(0, 0, 1, false).ok());
    if (failure == 3) check(!f.publish(false).ok());
    if (failure == 4) check(!f.finish(false).ok());
    check(f.state == R::State::Failed && f.resourcesHeld());
    check(!f.abort(false).ok() && f.resourcesHeld());
    check(f.cleanupResult(true).ok() && !f.resourcesHeld());
    check(f.checkWrite(0, 0, 1) == R::Error::InvalidState);
  }
  R::Session overflow; overflow.begin(R::Error::Ok);
  overflow.writeEpoch[0] = ~R::U64(0);
  check(overflow.checkWrite(0, 0, 1) == R::Error::InvalidLength);
  overflow.writeEpoch[0] = 1; overflow.written[0] = ~R::U64(0);
  check(overflow.checkWrite(0, 0, 1) == R::Error::InvalidLength);
  overflow.outCount[0] = ~R::U64(0);
  check(overflow.checkPublish() == R::Error::InvalidLength);
  check(overflow.abort().ok());
  R::Session partial; check(!partial.begin(R::Error::InvalidAddress).ok());
  check(partial.resourcesHeld()); check(partial.abort().ok());
  std::printf("%u runtime DMA checks passed (host model, no hardware)\n", checks);
}
