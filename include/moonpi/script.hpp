#pragma once
#include "common.hpp"
struct lua_State;
namespace moonpi {
// Each node owns a bounded Lua state. Writes are committed only after success.
class PinScript {
  lua_State *lua_ = nullptr;
  size_t bytes_ = 0;
  std::string source_, event_;
  bool input_ = false, output_ = false;
  int chunk_ = 0;
  static void *allocate(void *, void *, size_t, size_t);
  static int prepare(lua_State *);
  static int invoke(lua_State *);
  static int read_pin(lua_State *);
  static int write_pin(lua_State *);
  static PinScript &self(lua_State *);
  void checked(int);

public:
  explicit PinScript(std::string source);
  ~PinScript();
  PinScript(const PinScript &) = delete;
  PinScript &operator=(const PinScript &) = delete;
  bool execute(const std::string &event, bool value, bool current);
};
} // namespace moonpi
