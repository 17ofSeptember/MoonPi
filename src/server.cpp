#include "moonpi/server.hpp"
#include <cstring>
namespace moonpi {
namespace {
int respond(mg_connection *c, int status, const Json &data, const std::string &extra = {}) {
  auto body = data.dump();
  mg_printf(
      c,
      "HTTP/1.1 %d OK\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nCache-Control: "
      "no-store\r\nX-Content-Type-Options: nosniff\r\n%sConnection: close\r\n\r\n",
      status, body.size(), extra.c_str());
  mg_write(c, body.data(), body.size());
  return status;
}
std::string header(const mg_connection *c, const char *key) {
  auto h = mg_get_header(c, key);
  return h ? h : "";
}
} // namespace
bool Server::authorized(const mg_connection *c, bool websocket) const {
  auto origin = header(c, "Origin");
  const auto host = header(c, "Host");
  if (!origin.empty() && origin != "http://" + host)
    return false;
  if (websocket && origin.empty())
    return false;
  if (token_.empty())
    return true;
  return (!websocket && secret_equal(header(c, "Authorization"), "Bearer " + token_)) ||
         sessions_.valid(session_cookie(header(c, "Cookie")));
}
int Server::handle(mg_connection *c, void *user) {
  try {
    return static_cast<Server *>(user)->request(c);
  } catch (const Error &e) {
    return respond(c,
                   e.code == "command.stale"       ? 409
                   : e.code == "security.sessions" ? 429
                                                   : 400,
                   {{"error", diagnostic(e.code, e.what())}});
  } catch (const std::exception &e) {
    return respond(c, 400, {{"error", diagnostic("request.invalid", e.what())}});
  } catch (...) {
    return respond(c, 500, {{"error", diagnostic("server.internal", "Internal server error")}});
  }
}
int Server::request(mg_connection *c) {
  const auto *info = mg_get_request_info(c);
  const std::string uri = info->local_uri ? info->local_uri : "";
  const std::string method = info->request_method;
  if (uri.starts_with("/api/")) {
    if (method == "POST" && uri == "/api/v1/session") {
      {
        std::lock_guard lock(rate_mutex_);
        if (Clock::now() - login_start_ >= std::chrono::seconds(1)) {
          login_start_ = Clock::now();
          logins_ = 0;
        }
        if (++logins_ > 10)
          return respond(
              c, 429,
              {{"error", diagnostic("security.rate", "Too many session requests; retry shortly")}});
      }
      if (!authorized(c))
        return respond(c, 403,
                       {{"error", diagnostic("security.denied", "Session token rejected")}});
      if (token_.empty())
        return respond(c, 200, {{"authentication", "disabled"}});
      const auto id = sessions_.create(session_cookie(header(c, "Cookie")));
      return respond(c, 200, {{"authentication", "session"}},
                     "Set-Cookie: moonpi_session=" + id +
                         "; Path=/; HttpOnly; SameSite=Strict\r\n");
    }
    if (!authorized(c))
      return respond(
          c, 403, {{"error", diagnostic("security.denied", "Origin or session token rejected")}});
    if (method == "DELETE" && uri == "/api/v1/session") {
      sessions_.revoke(session_cookie(header(c, "Cookie")));
      return respond(
          c, 200, {{"signed_out", true}},
          "Set-Cookie: moonpi_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n");
    }
    if (method == "GET" && uri == "/api/v1/state")
      return respond(c, 200, app_.snapshot());
    if (method == "GET" && uri == "/api/v1/catalogue")
      return respond(c, 200, app_.catalogue());
    if (method == "GET" && uri == "/api/v1/resources")
      return respond(c, 200, app_.resources("gpio_output"));
    if (method == "GET" && uri == "/api/v1/health") {
      const auto state = app_.snapshot();
      const auto &runtime = state.at("runtime");
      size_t websocket_clients;
      {
        std::lock_guard lock(clients_mutex_);
        websocket_clients = clients_.size();
      }
      const bool graph_ok = runtime.at("error").is_null();
      const bool persistence_ok =
          !state.at("load_failed").get<bool>() && state.at("audit").at("status") == "healthy";
      Json degraded_nodes = Json::array();
      for (const auto &[id, node] : runtime.at("nodes").items())
        if (node.value("state", "") == "DEGRADED" || node.contains("error"))
          degraded_nodes.push_back(id);
      return respond(
          c, 200,
          {{"status",
            graph_ok && persistence_ok && degraded_nodes.empty() ? "healthy" : "degraded"},
           {"hardware", runtime.at("mode")},
           {"ai", state.at("ai").at("available").get<bool>() ? "healthy" : "unavailable"},
           {"uptime_seconds",
            std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - started_).count()},
           {"authentication", token_.empty() ? "disabled" : "session_or_bearer"},
           {"subsystems",
            {{"http", {{"status", "healthy"}}},
             {"websocket", {{"status", "healthy"}, {"clients", websocket_clients}, {"limit", 4}}},
             {"executor",
              {{"status", graph_ok ? "healthy" : "degraded"},
               {"running", runtime.at("running")},
               {"error", runtime.at("error")}}},
             {"scheduler",
              {{"status", graph_ok ? "healthy" : "degraded"},
               {"active_timers", runtime.at("active_timers")},
               {"queue_depth", runtime.at("queue_depth")}}},
             {"devices",
              {{"status", degraded_nodes.empty() && graph_ok ? "healthy" : "degraded"},
               {"degraded_nodes", degraded_nodes}}},
             {"persistence",
              {{"status", persistence_ok ? "healthy" : "degraded"},
               {"protected_project", state.at("load_failed")},
               {"recovered_session", state.at("recovery")}}},
             {"ai", state.at("ai")}}}});
    }
    if (method == "POST" && uri == "/api/v1/commands") {
      if (header(c, "Content-Type").find("application/json") != 0)
        throw Error("request.content_type", "Expected application/json");
      if (info->content_length < 0 || info->content_length > 1024 * 1024)
        return respond(
            c, 413,
            {{"error", diagnostic("request.size", "Content-Length must be between 0 and 1 MiB")}});
      std::string body(static_cast<size_t>(info->content_length), '\0');
      size_t offset = 0;
      while (offset < body.size()) {
        auto n = mg_read(c, body.data() + offset, body.size() - offset);
        if (n <= 0)
          throw Error("request.truncated", "Incomplete request body");
        offset += static_cast<size_t>(n);
      }
      auto command = parse_json(body);
      if (command.value("command", "") != "EmergencyStop" &&
          command.value("command", "") != "Stop") {
        std::lock_guard lock(rate_mutex_);
        auto now = Clock::now();
        if (now - rate_start_ > std::chrono::seconds(1)) {
          rate_start_ = now;
          commands_ = 0;
        }
        if (++commands_ > 30)
          return respond(c, 429,
                         {{"error", diagnostic("request.rate", "Command rate limit exceeded")}});
      }
      return respond(c, 200, app_.command(command));
    }
    return respond(c, 404, {{"error", diagnostic("request.not_found", "Unknown API route")}});
  }
  if (method != "GET" && method != "HEAD")
    return respond(c, 405, {{"error", diagnostic("request.method", "Method not allowed")}});
  // Explicit asset allowlist: no URL decoding, traversal, symlinks or arbitrary files.
  if (uri.find("..") != std::string::npos || uri.find('%') != std::string::npos ||
      uri.find('\\') != std::string::npos || uri.find(':') != std::string::npos)
    return respond(c, 403, {{"error", "Invalid asset path"}});
  auto relative = uri == "/" ? std::string("index.html") : uri.substr(1);
  if (relative != "index.html" && relative != "images/raspberry-pi-reference.jpg" &&
      !relative.starts_with("assets/"))
    return respond(c, 404, {{"error", "Asset not found"}});
  auto path = fs::weakly_canonical(web_ / relative);
  auto check = path.lexically_relative(fs::weakly_canonical(web_));
  if (check.empty() || check.is_absolute() || *check.begin() == ".." || !fs::is_regular_file(path))
    return respond(c, 404, {{"error", "Asset not found; build the frontend"}});
  const auto ext = path.extension().string();
  std::string mime = ext == ".html"  ? "text/html"
                     : ext == ".js"  ? "text/javascript"
                     : ext == ".css" ? "text/css"
                     : ext == ".svg" ? "image/svg+xml"
                     : ext == ".jpg" ? "image/jpeg"
                                     : "application/octet-stream";
  std::ifstream input(path, std::ios::binary);
  std::string body((std::istreambuf_iterator<char>(input)), {});
  mg_printf(c,
            "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nX-Content-Type-Options: "
            "nosniff\r\nContent-Security-Policy: default-src 'self'; script-src 'self'; style-src "
            "'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:; frame-ancestors "
            "'none'\r\nConnection: close\r\n\r\n",
            mime.c_str(), body.size());
  if (method != "HEAD")
    mg_write(c, body.data(), body.size());
  return 200;
}
int Server::connect(const mg_connection *c, void *user) {
  try {
    auto &s = *static_cast<Server *>(user);
    std::lock_guard lock(s.clients_mutex_);
    if (!s.authorized(c, true) || s.admitted_.size() >= 4)
      return 1;
    s.admitted_.insert(c);
    return 0;
  } catch (...) {
    return 1;
  }
}
void Server::ready(mg_connection *c, void *user) {
  try {
    auto &s = *static_cast<Server *>(user);
    std::lock_guard lock(s.clients_mutex_);
    s.clients_.insert(c);
  } catch (...) {
  }
}
int Server::data(mg_connection *, int bits, char *data, size_t size, void *) {
  try {
    if ((bits & 15) == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE)
      return 0;
    if ((bits & 15) == MG_WEBSOCKET_OPCODE_PING || (bits & 15) == MG_WEBSOCKET_OPCODE_PONG)
      return 1;
    if (bits != 0x81 || size > 256)
      return 0;
    auto message = parse_json(std::string_view(data, size));
    return message == Json{{"version", 1}, {"type", "Resync"}} ? 1 : 0;
  } catch (...) {
    return 0;
  }
}
void Server::close(const mg_connection *c, void *user) {
  auto &s = *static_cast<Server *>(user);
  std::lock_guard lock(s.clients_mutex_);
  s.clients_.erase(const_cast<mg_connection *>(c));
  s.admitted_.erase(c);
}
Server::Server(App &app, fs::path web, std::string bind, int port, std::string token)
    : app_(app), web_(std::move(web)), token_(std::move(token)),
      endpoint_(bind + ":" + std::to_string(port)) {
  mg_init_library(0);
  const char *options[] = {"listening_ports",
                           endpoint_.c_str(),
                           "num_threads",
                           "8",
                           "connection_queue",
                           "16",
                           "request_timeout_ms",
                           "1500",
                           "websocket_timeout_ms",
                           "10000",
                           "enable_websocket_ping_pong",
                           "yes",
                           "max_request_size",
                           "16384",
                           nullptr};
  context_ = mg_start(nullptr, nullptr, options);
  if (!context_)
    throw Error("server.start", "Cannot bind " + endpoint_);
  mg_set_request_handler(context_, "/", handle, this);
  mg_set_websocket_handler(context_, "/api/v1/events", connect, ready, data, close, this);
  telemetry_ = std::jthread([this](std::stop_token stop) {
    std::uint64_t seq = 0;
    while (!stop.stop_requested()) {
      try {
        auto state = app_.snapshot();
        auto payload = Json{
            {"version", 1},
            {"type", "StateSnapshot"},
            {"sequence", ++seq},
            {"timestamp_ms", timestamp_ms()},
            {"state",
             state}}.dump();
        std::lock_guard lock(clients_mutex_);
        for (auto it = clients_.begin(); it != clients_.end();) {
          auto *c = *it;
          if (!authorized(c, true)) {
            mg_websocket_write(c, MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE, nullptr, 0);
            it = clients_.erase(it);
          } else {
            mg_websocket_write(c, MG_WEBSOCKET_OPCODE_TEXT, payload.data(), payload.size());
            ++it;
          }
        }
      } catch (...) {
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });
}
Server::~Server() {
  telemetry_.request_stop();
  telemetry_.join();
  mg_stop(context_);
  mg_exit_library();
}
} // namespace moonpi
