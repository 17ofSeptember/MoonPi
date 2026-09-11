#pragma once
#include "common.hpp"
#include <nlohmann/json-schema.hpp>
namespace moonpi {
class Schemas {
  std::map<std::string, std::unique_ptr<nlohmann::json_schema::json_validator>> validators_;

public:
  explicit Schemas(const fs::path &root);
  void validate(const std::string &kind, const Json &value) const;
};
class ComponentRegistry {
  const Schemas &schemas_;
  std::map<std::string, Json> components_;
  Json diagnostics_ = Json::array();

public:
  explicit ComponentRegistry(const Schemas &schemas) : schemas_(schemas) {}
  void load(const fs::path &root, bool user = false);
  const Json *find(const std::string &id) const;
  Json catalogue(const std::string &query = "") const;
  const Json &diagnostics() const {
    return diagnostics_;
  }
  static bool driver_available(const std::string &id);
};
class BoardRegistry {
  std::map<std::string, Json> boards_;

public:
  BoardRegistry(const fs::path &, const Schemas &);
  const Json &get(const std::string &id) const;
  Json catalogue() const;
};
} // namespace moonpi
