#include "Sim/SimHost.hpp"

#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Utils/Log.hpp"

namespace ox {
SimHost* SimHost::instance_ = nullptr;

SimHost::SimHost(VFS& vfs, JobManager& job_manager, EventSystem& event_system, const Timestep& timestep)
    : vfs_(vfs),
      job_manager_(job_manager),
      event_system_(event_system),
      timestep_(timestep) {
  set_instance(this);
}

SimHost::~SimHost() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

auto SimHost::set_instance(SimHost* instance) -> void { instance_ = instance; }

auto SimHost::init(this SimHost& self) -> std::expected<void, std::string> {
  ZoneScoped;

  if (!self.registry.init()) {
    return std::unexpected("Failed to initialize a simulation module.");
  }

  return {};
}

auto SimHost::deinit(this SimHost& self) -> std::expected<void, std::string> {
  ZoneScoped;

  if (!self.registry.deinit()) {
    return std::unexpected("Failed to deinitialize a simulation module.");
  }

  return {};
}

auto SimHost::get_vfs() -> VFS& {
  OX_CHECK_NULL(instance_);
  return instance_->vfs_;
}

auto SimHost::get_job_manager() -> JobManager& {
  OX_CHECK_NULL(instance_);
  return instance_->job_manager_;
}

auto SimHost::get_event_system() -> EventSystem& {
  OX_CHECK_NULL(instance_);
  return instance_->event_system_;
}

auto SimHost::get_timestep() -> const Timestep& {
  OX_CHECK_NULL(instance_);
  return instance_->timestep_;
}
} // namespace ox
