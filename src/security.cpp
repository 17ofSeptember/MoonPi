#include "moonpi/security.hpp"
#include <array>
#ifdef _WIN32
// clang-format off: bcrypt.h requires the Windows declarations first.
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#else
#include <cerrno>
#include <sys/random.h>
#endif
namespace moonpi {
namespace {
std::string random_session() {
  std::array<unsigned char, 32> bytes{};
#ifdef _WIN32
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    throw Error("security.random", "Cannot generate a session credential");
#else
  size_t offset = 0;
  while (offset < bytes.size()) {
    const auto n = getrandom(bytes.data() + offset, bytes.size() - offset, GRND_NONBLOCK);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0)
      throw Error("security.random", "Cannot generate a session credential");
    offset += static_cast<size_t>(n);
  }
#endif
  std::string result;
  constexpr char hex[] = "0123456789abcdef";
  for (auto byte : bytes) {
    result += hex[byte >> 4];
    result += hex[byte & 15];
  }
  return result;
}
} // namespace
bool secret_equal(std::string_view a, std::string_view b) {
  if (a.size() != b.size())
    return false;
  unsigned difference = 0;
  for (size_t i = 0; i < a.size(); ++i)
    difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  return difference == 0;
}
std::string session_cookie(std::string_view cookies) {
  std::string value;
  bool found = false;
  while (!cookies.empty()) {
    const auto end = cookies.find(';');
    auto part = cookies.substr(0, end);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t'))
      part.remove_prefix(1);
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t'))
      part.remove_suffix(1);
    if (part.starts_with("moonpi_session=")) {
      if (found)
        return {};
      found = true;
      value = std::string(part.substr(15));
    }
    if (end == std::string_view::npos)
      break;
    cookies.remove_prefix(end + 1);
  }
  return value;
}
std::string Sessions::create(const std::string &existing, Clock::time_point now) {
  std::lock_guard lock(mutex_);
  std::erase_if(sessions_, [&](const auto &entry) { return entry.second <= now; });
  // Reconnects reuse the remaining lifetime; they do not extend it indefinitely.
  if (sessions_.contains(existing))
    return existing;
  if (sessions_.size() >= 16)
    throw Error("security.sessions", "Browser session limit reached");
  auto id = random_session();
  sessions_.emplace(id, now + std::chrono::hours(8));
  return id;
}
bool Sessions::valid(const std::string &id, Clock::time_point now) const {
  std::lock_guard lock(mutex_);
  const auto entry = sessions_.find(id);
  return entry != sessions_.end() && entry->second > now;
}
void Sessions::revoke(const std::string &id) {
  std::lock_guard lock(mutex_);
  sessions_.erase(id);
}
} // namespace moonpi
