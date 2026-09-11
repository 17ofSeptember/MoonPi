#include "moonpi/script.hpp"
#include <cstdlib>
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}
namespace moonpi {
void *PinScript::allocate(void *user, void *ptr, size_t old_size, size_t size) {
  auto &self = *static_cast<PinScript *>(user);
  if (!ptr)
    old_size = 0;
  if (!size) {
    std::free(ptr);
    self.bytes_ -= old_size;
    return nullptr;
  }
  constexpr size_t limit = 512 * 1024;
  if (size > limit || self.bytes_ - old_size > limit - size)
    return nullptr;
  auto *next = std::realloc(ptr, size);
  if (next)
    self.bytes_ = self.bytes_ - old_size + size;
  return next;
}
PinScript &PinScript::self(lua_State *state) {
  return **static_cast<PinScript **>(lua_getextraspace(state));
}
int PinScript::read_pin(lua_State *state) {
  lua_pushboolean(state, self(state).output_);
  return 1;
}
int PinScript::write_pin(lua_State *state) {
  luaL_checktype(state, 1, LUA_TBOOLEAN);
  if (lua_gettop(state) != 1)
    return luaL_error(state, "pin.write expects one Boolean");
  self(state).output_ = lua_toboolean(state, 1) != 0;
  return 0;
}
int PinScript::prepare(lua_State *state) {
  auto &script = self(state);
  // Copy a small whitelist from base into a fresh global environment.
  luaopen_base(state);
  lua_newtable(state);
  for (const auto *name :
       {"assert", "error", "ipairs", "pairs", "next", "select", "tonumber", "tostring", "type"}) {
    lua_getfield(state, -2, name);
    lua_setfield(state, -2, name);
  }
  lua_pushvalue(state, -1);
  lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  lua_settop(state, 0);
  lua_newtable(state);
  lua_pushcfunction(state, read_pin);
  lua_setfield(state, -2, "read");
  lua_pushcfunction(state, write_pin);
  lua_setfield(state, -2, "write");
  lua_setglobal(state, "pin");
  if (luaL_loadbufferx(state, script.source_.data(), script.source_.size(), "Pin script", "t") !=
      LUA_OK)
    return lua_error(state);
  script.chunk_ = luaL_ref(state, LUA_REGISTRYINDEX);
  return 0;
}
int PinScript::invoke(lua_State *state) {
  auto &script = self(state);
  lua_pushlstring(state, script.event_.data(), script.event_.size());
  lua_setglobal(state, "event");
  lua_pushboolean(state, script.input_);
  lua_setglobal(state, "value");
  lua_rawgeti(state, LUA_REGISTRYINDEX, script.chunk_);
  lua_call(state, 0, 0);
  return 0;
}
void PinScript::checked(int status) {
  if (status == LUA_OK)
    return;
  const char *message =
      lua_type(lua_, -1) == LUA_TSTRING ? lua_tostring(lua_, -1) : "Script raised an error";
  const std::string detail = message ? message : "Script memory limit exceeded";
  lua_settop(lua_, 0);
  throw Error("script.execution", detail);
}
PinScript::PinScript(std::string source) : source_(std::move(source)) {
  if (source_.size() > 16384)
    throw Error("script.size", "Script exceeds 16384 bytes");
  lua_ = lua_newstate(allocate, this);
  if (!lua_)
    throw Error("script.memory", "Cannot allocate script state");
  *static_cast<PinScript **>(lua_getextraspace(lua_)) = this;
  lua_pushcfunction(lua_, prepare);
  try {
    checked(lua_pcall(lua_, 0, 0, 0));
  } catch (...) {
    lua_close(lua_);
    lua_ = nullptr;
    throw;
  }
}
PinScript::~PinScript() {
  if (lua_)
    lua_close(lua_);
}
bool PinScript::execute(const std::string &event, bool value, bool current) {
  event_ = event;
  input_ = value;
  output_ = current;
  lua_sethook(
      lua_,
      [](lua_State *state, lua_Debug *) {
        luaL_error(state,
                   "Script instruction limit exceeded (50000); use an Interval node for timing");
      },
      LUA_MASKCOUNT, 50000);
  lua_pushcfunction(lua_, invoke);
  const int status = lua_pcall(lua_, 0, 0, 0);
  lua_sethook(lua_, nullptr, 0, 0);
  checked(status);
  return output_;
}
} // namespace moonpi
