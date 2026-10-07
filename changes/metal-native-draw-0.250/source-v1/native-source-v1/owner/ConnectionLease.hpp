#pragma once
#include <cstdint>

// A single attempt owns one service reference and at most one connection.
// RTXProbe 0.32 consumes its client reservation even without a firmware launch.
template<class Ops> class ConnectionLease final {
  Ops &ops;
  uint32_t service_ = 0, connection_ = 0;
  bool attempted_ = false;
public:
  int openResult = 0, closeResult = 0;
  explicit ConnectionLease(Ops &backend): ops(backend) {}
  ConnectionLease(const ConnectionLease &) = delete;
  ConnectionLease &operator=(const ConnectionLease &) = delete;
  ~ConnectionLease() { close(); }
  bool open(uint32_t service) {
    if (attempted_ || !service) return false;
    attempted_ = true;
    openResult = ops.retain(service);
    if (openResult) return false;
    service_ = service;
    openResult = ops.open(service, &connection_);
    if (openResult || !connection_) { close(); return false; }
    return true;
  }
  void close() {
    // Clear ownership before callbacks. Never retry an uncertain close.
    const auto connection = connection_; connection_ = 0;
    const auto service = service_; service_ = 0;
    if (connection) closeResult = ops.close(connection);
    if (service) ops.release(service);
  }
  uint32_t connection() const { return connection_; }
  uint32_t service() const { return service_; }
};
