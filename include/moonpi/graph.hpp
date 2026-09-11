#pragma once
#include "catalog.hpp"
namespace moonpi {
struct Lease {
  std::string resource, node;
  int bcm;
  bool safe;
  bool input = false, pull_up = false;
};
struct CompiledNode {
  std::string id, driver;
  Json properties;
};
struct I2cLease {
  std::string node;
  int bus = 1, address = 0;
  std::string driver;
};
struct Route {
  std::string source, source_port, target, target_port;
};
struct CompiledGraph {
  std::uint64_t revision = 0;
  std::vector<CompiledNode> nodes;
  std::vector<Lease> leases;
  std::vector<Route> routes;
  std::vector<I2cLease> i2c;
};
struct Validation {
  Json diagnostics = Json::array();
  CompiledGraph graph;
  bool valid() const {
    for (const auto &d : diagnostics)
      if (d.at("severity") != "WARNING" && d.at("severity") != "INFO")
        return false;
    return true;
  }
  Json json() const;
};
class GraphCompiler {
  const Schemas &schemas_;
  const ComponentRegistry &components_;
  const BoardRegistry &boards_;

public:
  GraphCompiler(const Schemas &s, const ComponentRegistry &c, const BoardRegistry &b)
      : schemas_(s), components_(c), boards_(b) {}
  Validation compile(const Json &, std::uint64_t revision) const;
  Json compatible_resources(const std::string &board, const std::string &role) const;
};
Json blank_project();
} // namespace moonpi
