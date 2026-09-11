#include "moonpi/persistence.hpp"
#include <iostream>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif
namespace moonpi {
struct ProcessLock::Impl {
#ifdef _WIN32
  HANDLE handle = INVALID_HANDLE_VALUE;
  ~Impl() {
    if (handle != INVALID_HANDLE_VALUE)
      CloseHandle(handle);
  }
#else
  int handle = -1;
  ~Impl() {
    if (handle >= 0)
      ::close(handle);
  }
#endif
};
ProcessLock::ProcessLock(const fs::path &path) : impl_(std::make_unique<Impl>()) {
  fs::create_directories(path.parent_path());
#ifdef _WIN32
  impl_->handle = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
  if (impl_->handle == INVALID_HANDLE_VALUE)
    throw Error("persistence.lock",
                "Data directory is in use by another Moon Pi process, or is not writable");
#else
  impl_->handle = ::open(path.c_str(), O_RDWR | O_CREAT, 0600);
  if (impl_->handle < 0 || flock(impl_->handle, LOCK_EX | LOCK_NB) != 0)
    throw Error("persistence.lock", "Data directory is in use or not writable");
#endif
}
ProcessLock::~ProcessLock() = default;
namespace {
void durable_replace(const fs::path &from, const fs::path &to) {
#ifdef _WIN32
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    throw Error("persistence.rename",
                "Atomic replacement failed: " + std::to_string(GetLastError()));
#else
  if (::rename(from.c_str(), to.c_str()) != 0)
    throw Error("persistence.rename", "Atomic rename failed");
  int dir = ::open(to.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
  if (dir < 0)
    throw Error("persistence.sync", "Cannot open parent directory");
  int result = fsync(dir);
  ::close(dir);
  if (result != 0)
    throw Error("persistence.sync", "Directory sync failed");
#endif
}
} // namespace
void atomic_write(const fs::path &path, const std::string &data, bool backup) {
  fs::create_directories(path.parent_path());
  const fs::path tmp = path.string() + ".tmp";
#ifdef _WIN32
  HANDLE file = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE)
    throw Error("persistence.open", "Cannot create temporary save file");
  DWORD written = 0;
  bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) &&
            written == data.size() && FlushFileBuffers(file);
  CloseHandle(file);
  if (!ok)
    throw Error("persistence.write", "Could not flush complete project; original is preserved");
#else
  int file = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (file < 0)
    throw Error("persistence.open", "Cannot create temporary save file");
  size_t offset = 0;
  while (offset < data.size()) {
    auto n = ::write(file, data.data() + offset, data.size() - offset);
    if (n <= 0) {
      ::close(file);
      throw Error("persistence.write", "Incomplete project write");
    }
    offset += static_cast<size_t>(n);
  }
  int synced = fsync(file);
  ::close(file);
  if (synced != 0)
    throw Error("persistence.sync", "Project sync failed");
#endif
  if (backup && fs::exists(path)) {
    std::ifstream input(path, std::ios::binary);
    std::string previous((std::istreambuf_iterator<char>(input)), {});
    if (!input.eof() && input.fail())
      throw Error("persistence.backup", "Cannot read previous project");
    atomic_write(path.string() + ".bak", previous, false);
  }
  durable_replace(tmp, path);
}
void AuditLog::record(const std::string &event, const Json &fields) {
  std::lock_guard lock(mutex_);
  try {
    Json record = {{"timestamp_ms", timestamp_ms()}, {"event", event}, {"fields", fields}};
    std::cout << record.dump() << std::endl;
    fs::create_directories(path_.parent_path());
    if (fs::exists(path_) && fs::file_size(path_) > rotation_bytes_) {
      for (int index = 3; index >= 1; --index) {
        const fs::path source =
            index == 1 ? path_ : fs::path(path_.string() + "." + std::to_string(index - 1));
        const fs::path destination = path_.string() + "." + std::to_string(index);
        if (index == 3 && fs::exists(destination))
          fs::remove(destination);
        if (fs::exists(source))
          fs::rename(source, destination);
      }
    }
    std::ofstream out(path_, std::ios::app);
    if (!out)
      throw Error("logging.unavailable", "Audit log is not writable");
    out << record.dump() << '\n';
    out.flush();
    if (!out)
      throw Error("logging.write", "Audit log write failed");
    error_.clear();
  } catch (const std::exception &e) {
    error_ = e.what();
    throw;
  }
}
Json AuditLog::status() const {
  std::lock_guard lock(mutex_);
  return {{"status", error_.empty() ? "healthy" : "degraded"},
          {"error", error_.empty() ? Json() : Json(error_)}};
}
} // namespace moonpi
