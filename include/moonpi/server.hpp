#pragma once
#include "app.hpp"
#include "security.hpp"
#include <civetweb.h>
#include <set>
namespace moonpi {
class Server {
  App &app_;
  fs::path web_;
  std::string token_, endpoint_;
  mg_context *context_ = nullptr;
  std::mutex clients_mutex_;
  std::set<mg_connection *> clients_;
  std::set<const mg_connection *> admitted_;
  std::jthread telemetry_;
  std::mutex rate_mutex_;
  Clock::time_point rate_start_ = Clock::now();
  int commands_ = 0;
  Sessions sessions_;
  Clock::time_point login_start_ = Clock::now();
  int logins_ = 0;
  Clock::time_point started_ = Clock::now();
  bool authorized(const mg_connection *, bool websocket = false) const;
  int request(mg_connection *);
  static int handle(mg_connection *, void *);
  static int connect(const mg_connection *, void *);
  static void ready(mg_connection *, void *);
  static int data(mg_connection *, int, char *, size_t, void *);
  static void close(const mg_connection *, void *);

public:
  Server(App &, fs::path, std::string bind, int port, std::string token = "");
  ~Server();
};
} // namespace moonpi
