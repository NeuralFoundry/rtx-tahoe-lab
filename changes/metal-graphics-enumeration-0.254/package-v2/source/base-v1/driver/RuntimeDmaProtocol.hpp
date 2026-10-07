#pragma once
#include "GSPDmaProtocol.hpp"

// RTXProbe 0.13 bootstrap transport: repeated access to the nine owned GSP
// buffers. This is a host-only lifetime. It must not be used after GPU START.
namespace RuntimeDmaProtocol {
using namespace GSPDmaProtocol;
class Session {
  bool held = false;
  Error last = Error::Ok;
  Result result(Error e) const { return {e, held}; }
  bool active() const { return held && (state == State::Writing || state == State::Published); }
  Error chunk(U32 resource, U64 offset, U64 length) const {
    if (!active()) return Error::InvalidState;
    if (resource >= ResourceCount) return Error::InvalidResource;
    if (!length || length > Page || offset > Sizes[resource] || length > Sizes[resource] - offset)
      return Error::InvalidLength;
    return Error::Ok;
  }
public:
  State state = State::Idle;
  bool cleanupVerified = false;
  U64 written[ResourceCount] = {}, readBytes[ResourceCount] = {};
  U64 writeEpoch[ResourceCount] = {}, outEpoch[ResourceCount] = {}, inEpoch[ResourceCount] = {};
  U64 outCount[ResourceCount] = {}, inCount[ResourceCount] = {};
  bool resourcesHeld() const { return held; }
  Error lastError() const { return last; }
  Result fail(Error e) { state = State::Failed; last = e == Error::Ok ? Error::TransportFailed : e; return result(last); }
  Result begin(Error e) {
    if (state != State::Idle) return fail(Error::InvalidState);
    held = true;
    if (e != Error::Ok) return fail(e);
    state = State::Writing;
    // The native zero fill is a CPU write too. Publish it before importing.
    for (U32 i = 0; i < ResourceCount; ++i) writeEpoch[i] = 1;
    return result(Error::Ok);
  }
  Error checkWrite(U32 r, U64 off, U64 len) const {
    const Error e = chunk(r, off, len);
    if (e != Error::Ok) return e;
    return writeEpoch[r] == ~U64(0) || written[r] > ~U64(0) - len ? Error::InvalidLength : Error::Ok;
  }
  Result write(U32 r, U64 off, U64 len, bool good = true) {
    const Error e = checkWrite(r, off, len);
    if (e != Error::Ok) return fail(e);
    if (!good) return fail(Error::TransportFailed);
    written[r] += len; ++writeEpoch[r]; state = State::Writing;
    return result(Error::Ok);
  }
  Error checkSync(U32 r, U32 direction) const {
    if (!active()) return Error::InvalidState;
    if (r >= ResourceCount) return Error::InvalidResource;
    if (direction != 1 && direction != 2) return Error::InvalidLength;
    // 1 = import, 2 = publish, matching IODirection. Never both at once.
    if (direction == 1 && outEpoch[r] != writeEpoch[r]) return Error::IncompleteWrites;
    if ((direction == 1 ? inCount[r] : outCount[r]) == ~U64(0)) return Error::InvalidLength;
    return Error::Ok;
  }
  Result sync(U32 r, U32 direction, bool good = true) {
    const Error e = checkSync(r, direction);
    if (e != Error::Ok) return fail(e);
    if (!good) return fail(Error::TransportFailed);
    if (direction == 1) { inEpoch[r] = outEpoch[r]; ++inCount[r]; }
    else { outEpoch[r] = writeEpoch[r]; inEpoch[r] = 0; ++outCount[r]; }
    bool clean = true;
    for (U32 i = 0; i < ResourceCount; ++i) clean = clean && outEpoch[i] == writeEpoch[i];
    state = clean ? State::Published : State::Writing;
    return result(Error::Ok);
  }
  Error checkPublish() const {
    if (!active()) return Error::InvalidState;
    for (U32 i = 0; i < ResourceCount; ++i) {
      const Error e = checkSync(i, 2); if (e != Error::Ok) return e;
    }
    return Error::Ok;
  }
  Result publish(bool good = true) {
    const Error e = checkPublish();
    if (e != Error::Ok) return fail(e);
    if (!good) return fail(Error::TransportFailed);
    for (U32 i = 0; i < ResourceCount; ++i) sync(i, 2);
    return result(Error::Ok);
  }
  Error checkRead(U32 r, U64 off, U64 len) const {
    const Error e = chunk(r, off, len);
    if (e != Error::Ok) return e;
    if (inEpoch[r] != writeEpoch[r]) return Error::IncompleteReads;
    return readBytes[r] > ~U64(0) - len ? Error::InvalidLength : Error::Ok;
  }
  Result read(U32 r, U64 off, U64 len, bool good = true) {
    const Error e = checkRead(r, off, len);
    if (e != Error::Ok) return fail(e);
    if (!good) return fail(Error::TransportFailed);
    readBytes[r] += len; return result(Error::Ok);
  }
  Error checkFinish() const { return active() ? Error::Ok : Error::InvalidState; }
  Result finish(bool good = true) {
    const Error e = checkFinish(); if (e != Error::Ok) return fail(e);
    if (!good) return fail(Error::CleanupFailed);
    held = false; cleanupVerified = true; state = State::Finished; return result(Error::Ok);
  }
  Result abort(bool good = true) {
    if (state == State::Finished || state == State::Aborted) return result(Error::Ok);
    if (held && !good) return fail(Error::CleanupFailed);
    held = false; cleanupVerified = true; state = State::Aborted; return result(Error::Ok);
  }
  Result cleanupResult(bool good) {
    if (!held) return result(Error::Ok);
    if (!good) return fail(Error::CleanupFailed);
    held = false; cleanupVerified = true;
    if (state == State::Writing || state == State::Published) return fail(Error::InvalidState);
    return result(Error::Ok);
  }
};
} // namespace RuntimeDmaProtocol
