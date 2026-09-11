#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
namespace moonpi {
using Json = nlohmann::json;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;
inline std::int64_t timestamp_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
struct Error : std::runtime_error {
  std::string code;
  Error(std::string c, const std::string &message)
      : std::runtime_error(message), code(std::move(c)) {}
};
inline Json diagnostic(std::string code, std::string message, std::string subject = "",
                       std::string severity = "ERROR") {
  return {{"code", code}, {"message", message}, {"subject", subject}, {"severity", severity}};
}
inline bool json_depth_limit(int depth, Json::parse_event_t, Json &) {
  if (depth > 64)
    throw Error("schema.depth", "JSON nesting exceeds 64 levels");
  return true;
}
inline Json parse_json(std::string_view text) {
  return Json::parse(text, json_depth_limit);
}
inline Json read_json(const fs::path &path) {
  if (fs::file_size(path) > 2 * 1024 * 1024)
    throw Error("persistence.too_large", "JSON file exceeds 2 MiB: " + path.string());
  std::ifstream in(path);
  if (!in)
    throw Error("persistence.open", "Cannot open " + path.string());
  return Json::parse(in, json_depth_limit);
}
inline std::string uuid() {
  std::random_device rd;
  const char *hex = "0123456789abcdef";
  std::string s;
  for (int i = 0; i < 32; ++i)
    s += hex[rd() & 15];
  s[12] = '4';
  s[16] = hex[8 + (rd() & 3)];
  s.insert(20, "-");
  s.insert(16, "-");
  s.insert(12, "-");
  s.insert(8, "-");
  return s;
}
} // namespace moonpi
