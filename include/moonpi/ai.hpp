#pragma once
#include "common.hpp"
namespace moonpi {
class IAiEngine {
public:
  virtual ~IAiEngine() = default;
  virtual Json status() const = 0;
  virtual Json propose(const Json &request) = 0;
};
class UnavailableAiEngine final : public IAiEngine {
public:
  Json status() const override {
    return {{"available", false},
            {"engine", "Needle 2"},
            {"reason", "Native integration is not enabled in this build"}};
  }
  Json propose(const Json &) override {
    throw Error("ai.unavailable", "Needle 2 is unavailable; manual editing remains available");
  }
};
class MockAiEngine final : public IAiEngine {
  Json response_;

public:
  explicit MockAiEngine(Json response) : response_(std::move(response)) {}
  Json status() const override {
    return {{"available", true}, {"engine", "Mock (test only)"}};
  }
  Json propose(const Json &) override {
    return response_;
  }
};
} // namespace moonpi
