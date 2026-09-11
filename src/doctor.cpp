#include "moonpi/doctor.hpp"
#include "moonpi/catalog.hpp"
#include "moonpi/version.hpp"
#ifdef __linux__
#include <unistd.h>
#endif
namespace moonpi {
Json installation_check(const Json &config) {
  Json checks = Json::array();
  auto add = [&](const char *id, const char *status, const std::string &message) {
    checks.push_back({{"id", id}, {"status", status}, {"message", message}});
  };
  try {
    Schemas schemas(config.at("schemas").get<std::string>());
    schemas.validate("config", config);
    ComponentRegistry components(schemas);
    const fs::path resources = config.at("resources").get<std::string>();
    if (!fs::is_directory(resources / "components"))
      throw Error("doctor.resources", "Components folder is missing");
    components.load(resources / "components");
    components.load(fs::path(config.at("data").get<std::string>()) / "components", true);
    BoardRegistry boards(resources / "boards", schemas);
    boards.get("moonpi.board.rpi3b-plus");
    if (components.catalogue().empty() || !components.diagnostics().empty())
      throw Error("doctor.catalogue",
                  "Component catalogue is empty or contains invalid definitions");
    add("catalogue", "pass",
        std::to_string(components.catalogue().size()) + " components and Pi 3 B+ board validated");
  } catch (const std::exception &e) {
    add("catalogue", "fail", e.what());
  }
  const fs::path web = config.at("web").get<std::string>();
  add("web",
      fs::is_regular_file(web / "index.html") && fs::is_directory(web / "assets") ? "pass" : "fail",
      "Frontend index.html and assets directory: " + web.string());
  const fs::path data = fs::absolute(config.at("data").get<std::string>());
  auto parent = data;
  while (!fs::exists(parent) && parent != parent.parent_path())
    parent = parent.parent_path();
  if (!fs::is_directory(parent))
    add("data", "fail", "Data path has no usable directory ancestor");
  else {
#ifdef __linux__
    add("data", ::access(parent.c_str(), W_OK | X_OK) == 0 ? "pass" : "fail",
        "Data directory ancestor permissions checked for this user: " + parent.string());
#else
    add("data", "warning",
        "Directory ancestor exists; effective write permissions were not tested: " +
            parent.string());
#endif
  }
  if (config.at("mode") == "hardware") {
#ifdef __linux__
    std::ifstream tree("/proc/device-tree/compatible", std::ios::binary);
    std::string compatible((std::istreambuf_iterator<char>(tree)), {});
    add("board",
        compatible.find("raspberrypi,3-model-b-plus") != std::string::npos ? "pass" : "fail",
        "Hardware mode requires a Raspberry Pi 3 B+ device tree");
    const fs::path chip = config.at("gpio_chip").get<std::string>();
    add("gpio",
        fs::is_character_file(chip) && ::access(chip.c_str(), R_OK | W_OK) == 0 ? "pass" : "fail",
        "GPIO character device and permissions: " + chip.string());
#else
    add("hardware", "fail", "Physical GPIO requires Linux on the supported Raspberry Pi");
#endif
    add("wiring", "warning",
        "No device was opened or pin requested. Chip identity, pin ownership, buses and physical "
        "wiring still require hardware verification.");
  } else
    add("mode", "pass", "Simulation selected; no physical device is required");
  add("network", "warning",
      "Port availability and token validity are checked at service startup, not by this read-only "
      "check");
  bool failed = false;
  for (const auto &check : checks)
    if (check.at("status") == "fail")
      failed = true;
  return {{"name", metadata::name}, {"version", metadata::version}, {"read_only", true},
          {"ready", !failed},       {"mode", config.at("mode")},    {"checks", checks}};
}
} // namespace moonpi
