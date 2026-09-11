#include "moonpi/app.hpp"
#include "moonpi/version.hpp"
namespace moonpi {
App::App(const Schemas &s, const ComponentRegistry &c, const BoardRegistry &b, fs::path data,
         std::unique_ptr<IGpioController> gpio)
    : schemas_(s), components_(c), boards_(b), compiler_(s, c, b),
      process_lock_(data / "process.lock"),
      runtime_(std::move(gpio), nullptr, data / "scheduler.json"), log_(data / "logs/audit.jsonl"),
      data_(std::move(data)) {
  fs::create_directories(data_);
  recovery_ = fs::exists(data_ / "session.active") || fs::exists(data_ / "project.moonpi.json.tmp");
  log_.record("application.start", {{"name", metadata::name},
                                    {"mode", runtime_.snapshot().at("mode")},
                                    {"recovery", recovery_}});
  if (fs::exists(data_ / "project.moonpi.json"))
    try {
      auto loaded = read_json(data_ / "project.moonpi.json");
      schemas_.validate("project", loaded);
      design_ = std::move(loaded);
      saved_design_ = design_;
    } catch (const std::exception &e) {
      load_failed_ = true;
      log_.record("project.load_failed", {{"message", e.what()}});
    }
  validation_ = compiler_.compile(design_, revision_);
  atomic_write(data_ / "session.active", "active", false);
}
App::~App() {
  runtime_.stop();
  try {
    log_.record("application.safe_shutdown");
    fs::remove(data_ / "session.active");
  } catch (...) {
  }
}
Json App::snapshot_locked() const {
  return {{"metadata",
           {{"name", metadata::name},
            {"author", metadata::author},
            {"version", metadata::version},
            {"architecture", metadata::architecture}}},
          {"project", design_},
          {"project_saved", design_ == saved_design_},
          {"design_revision", revision_},
          {"validated_revision", validation_.valid() ? revision_ : 0},
          {"validation", validation_.json()},
          {"runtime", runtime_.snapshot()},
          {"ai", ai_.status()},
          {"audit", log_.status()},
          {"recovery", recovery_},
          {"load_failed", load_failed_},
          {"catalogue_diagnostics", components_.diagnostics()}};
}
Json App::snapshot() const {
  std::lock_guard lock(mutex_);
  return snapshot_locked();
}
Json App::catalogue() const {
  return {{"boards", boards_.catalogue()}, {"components", components_.catalogue()}};
}
Json App::resources(const std::string &role) const {
  std::lock_guard lock(mutex_);
  return compiler_.compatible_resources(design_.at("board").at("definition"), role);
}
Json App::command(const Json &command) {
  std::lock_guard lock(mutex_);
  schemas_.validate("command", command);
  const std::string type = command.at("command");
  // Stop commands must remain effective from stale clients.
  if (type != "EmergencyStop" && type != "Stop" &&
      command.at("expected_revision").get<std::uint64_t>() != revision_)
    throw Error("command.stale", "Project changed on the server; resynchronize before editing");
  // A full disk must never prevent Stop or EmergencyStop from taking effect.
  if (type == "EmergencyStop" || type == "Stop") {
    if (type == "EmergencyStop")
      runtime_.emergency_stop();
    else
      runtime_.stop();
    try {
      log_.record("command.accepted", {{"command", type}, {"revision", revision_}});
    } catch (...) {
    }
    return snapshot_locked();
  }
  log_.record("command.accepted", {{"command", type}, {"revision", revision_}});
  if (type == "InjectInput") {
    if (!command.contains("node") || !command.contains("value"))
      throw Error("command.input", "InjectInput needs node and value");
    runtime_.inject_input(command.at("node"), command.at("value"));
  } else if (type == "Trigger") {
    if (!command.contains("node") || !command.contains("port"))
      throw Error("command.trigger", "Trigger needs node and port");
    if (runtime_.snapshot().at("applied_revision") != revision_)
      throw Error("runtime.stale", "Apply the current design before triggering nodes");
    runtime_.trigger(command.at("node"), command.at("port"));
  } else if (type == "SetDesign") {
    if (!command.contains("project"))
      throw Error("command.project", "SetDesign requires project");
    schemas_.validate("project", command.at("project"));
    design_ = command.at("project");
    ++revision_;
    validation_ = compiler_.compile(design_, revision_);
  } else if (type == "Validate")
    validation_ = compiler_.compile(design_, revision_);
  else if (type == "Apply") {
    if (runtime_.snapshot().at("mode") == "hardware" &&
        design_.at("board").at("definition") != "moonpi.board.rpi3b-plus")
      throw Error("hardware.board", "Project board does not match the verified Raspberry Pi 3 B+");
    validation_ = compiler_.compile(design_, revision_);
    if (!validation_.valid())
      throw Error("graph.invalid", "Fix validation errors before Apply");
    runtime_.apply(validation_.graph);
    log_.record("hardware.applied", validation_.json());
  } else if (type == "Start") {
    auto state = runtime_.snapshot();
    if (state.at("applied_revision") != revision_)
      throw Error("runtime.stale", "Apply the current design before Run");
    runtime_.start();
  } else if (type == "Stop")
    runtime_.stop();
  else if (type == "EmergencyStop")
    runtime_.emergency_stop();
  else if (type == "ClearEmergency")
    runtime_.clear_emergency();
  else if (type == "Save") {
    if (load_failed_)
      throw Error(
          "persistence.protected",
          "The original project is corrupt. Move it aside before saving to preserve recovery data");
    schemas_.validate("project", design_);
    atomic_write(data_ / "project.moonpi.json", design_.dump(2));
    saved_design_ = design_;
    log_.record("project.saved", {{"revision", revision_}});
  } else if (type == "Load") {
    auto loaded = read_json(data_ / "project.moonpi.json");
    schemas_.validate("project", loaded);
    auto validation = compiler_.compile(loaded, revision_ + 1);
    runtime_.stop();
    design_ = std::move(loaded);
    saved_design_ = design_;
    ++revision_;
    validation_ = std::move(validation);
    load_failed_ = false;
  }
  return snapshot_locked();
}
} // namespace moonpi
