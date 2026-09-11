#include "moonpi/doctor.hpp"
#include "moonpi/gpio_device.hpp"
#include "moonpi/server.hpp"
#include "moonpi/version.hpp"
#include <csignal>
#include <iostream>
#include <regex>
namespace {
volatile std::sig_atomic_t stopping = 0;
void signal_stop(int) {
  stopping = 1;
}
std::string environment(const char *name) {
#ifdef _WIN32
  char *raw = nullptr;
  size_t size = 0;
  if (_dupenv_s(&raw, &size, name) != 0)
    throw std::runtime_error("Cannot read environment");
  std::unique_ptr<char, decltype(&std::free)> value(raw, &std::free);
  return value ? value.get() : "";
#else
  const auto *value = std::getenv(name);
  return value ? value : "";
#endif
}
} // namespace
int main(int argc, char **argv) {
  using namespace moonpi;
  try {
    Json config = {{"version", 1},
                   {"bind", "0.0.0.0"},
                   {"port", 8080},
                   {"resources", "resources"},
                   {"schemas", "schemas"},
                   {"web", "web"},
                   {"data", "user-data"},
                   {"mode", "simulation"},
                   {"gpio_chip", "/dev/gpiochip0"}};
    for (int i = 1; i < argc; ++i)
      if (std::string(argv[i]) == "--config") {
        if (++i >= argc)
          throw Error("config.argument", "Missing config path");
        config.update(read_json(argv[i]));
      }
    for (const auto &key :
         {"bind", "port", "resources", "schemas", "web", "data", "mode", "gpio_chip"}) {
      std::string env = "MOONPI_" + std::string(key);
      for (auto &c : env)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      const auto value = environment(env.c_str());
      if (!value.empty()) {
        if (std::string(key) == "port")
          config[key] = std::stoi(value);
        else
          config[key] = value;
      }
    }
    bool validate_components = false, dry_run = false, doctor = false;
    std::string component_action;
    fs::path component_file;
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg == "component") {
        if (i + 2 >= argc)
          throw Error("config.argument", "Usage: moonpi component validate|inspect FILE");
        component_action = argv[++i];
        component_file = argv[++i];
        if (component_action != "validate" && component_action != "inspect")
          throw Error("config.argument", "Expected component validate or inspect");
        continue;
      }
      if (arg == "--version") {
        std::cout << metadata::name << " " << metadata::version << " by " << metadata::author
                  << "\n";
        return 0;
      }
      if (arg == "--help") {
        std::cout << metadata::name
                  << " [--simulation | --hardware] [--gpio-chip /dev/gpiochip0] [--bind ADDRESS] "
                     "[--port 8080] [--resources DIR] [--schemas DIR] [--web DIR] [--data DIR] "
                     "[--config FILE] [--dry-run] [--doctor] [--validate-components]\n"
                     "moonpi component validate|inspect FILE [--schemas DIR]\n";
        return 0;
      }
      if (arg == "--config") {
        ++i;
        continue;
      }
      if (arg == "--simulation" || arg == "--hardware") {
        config["mode"] = arg == "--hardware" ? "hardware" : "simulation";
        continue;
      }
      if (arg == "--validate-components") {
        validate_components = true;
        continue;
      }
      if (arg == "--dry-run") {
        dry_run = true;
        continue;
      }
      if (arg == "--doctor") {
        doctor = true;
        continue;
      }
      if (arg == "--bind" || arg == "--port" || arg == "--resources" || arg == "--schemas" ||
          arg == "--web" || arg == "--data" || arg == "--gpio-chip") {
        if (++i >= argc)
          throw Error("config.argument", "Missing value for " + arg);
        if (arg == "--port")
          config["port"] = std::stoi(argv[i]);
        else
          config[arg == "--gpio-chip" ? "gpio_chip" : arg.substr(2)] = argv[i];
      } else
        throw Error("config.argument", "Unknown option: " + arg);
    }
    if (doctor) {
      const auto report = installation_check(config);
      std::cout << report.dump(2) << '\n';
      return report.at("ready").get<bool>() ? 0 : 1;
    }
    Schemas schemas(config.at("schemas").get<std::string>());
    schemas.validate("config", config);
    if (!component_action.empty()) {
      if (!fs::is_regular_file(component_file))
        throw Error("component.file", "Component file does not exist");
      ComponentRegistry single(schemas);
      single.load(component_file);
      Json result = {{"valid", single.diagnostics().empty()},
                     {"diagnostics", single.diagnostics()}};
      if (component_action == "inspect")
        result["components"] = single.catalogue();
      std::cout << result.dump(2) << '\n';
      return single.diagnostics().empty() ? 0 : 1;
    }
    if (!std::regex_match(config.at("bind").get<std::string>(),
                          std::regex(R"((\d{1,3}\.){3}\d{1,3})")))
      throw Error("config.bind", "Bind must be an IPv4 address");
    fs::path resources = config.at("resources").get<std::string>();
    ComponentRegistry registry(schemas);
    registry.load(resources / "components");
    registry.load(fs::path(config.at("data").get<std::string>()) / "components", true);
    BoardRegistry boards(resources / "boards", schemas);
    if (validate_components) {
      std::cout << registry.catalogue().dump(2) << "\n" << registry.diagnostics().dump(2) << "\n";
      return registry.diagnostics().empty() ? 0 : 1;
    }
    std::signal(SIGINT, signal_stop);
    std::signal(SIGTERM, signal_stop);
    // Dry run deliberately uses no physical device, even with --hardware.
    std::unique_ptr<IGpioController> gpio =
        config.at("mode") == "hardware" && !dry_run
            ? make_linux_gpio_backend(config.at("gpio_chip").get<std::string>())
            : std::make_unique<SimulatedHardwareBackend>();
    App app(schemas, registry, boards, config.at("data").get<std::string>(), std::move(gpio));
    if (dry_run) {
      auto state = app.snapshot();
      std::cout << state.at("validation").dump(2) << "\n";
      return state.at("validation").at("valid").get<bool>() ? 0 : 1;
    }
    const auto token = environment("MOONPI_TOKEN");
    if (!token.empty() && !std::regex_match(token, std::regex("[A-Za-z0-9_-]{24,128}")))
      throw Error(
          "config.token",
          "MOONPI_TOKEN must contain 24-128 URL-safe letters, digits, underscores or hyphens");
    Server server(app, config.at("web").get<std::string>(), config.at("bind"), config.at("port"),
                  token);
    std::cout << metadata::name << " by " << metadata::author
              << (config.at("mode") == "hardware" ? " — HARDWARE MODE" : " — SIMULATION MODE")
              << "\nLocal: http://localhost:" << config.at("port")
              << "\nLAN bind: " << config.at("bind") << "\n";
    while (!stopping)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    app.command({{"version", 1}, {"command", "EmergencyStop"}, {"expected_revision", 0}});
    return 0;
  } catch (const std::exception &e) {
    std::cerr << Json{{"severity", "ERROR"}, {"message", e.what()}}.dump() << "\n";
    return 1;
  }
}
