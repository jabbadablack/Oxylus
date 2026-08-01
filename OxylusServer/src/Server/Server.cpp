#include "Server/Server.hpp"

#include <algorithm>

#include "Core/EventSystem.hpp"
#include "Core/JobManager.hpp"
#include "Core/VFS.hpp"
#include "Scene/Scene.hpp"
#include "Utils/Log.hpp"
#include "Utils/Timer.hpp"

namespace ox {
Server* Server::instance_ = nullptr;

// The shared services a standalone server has to bring itself, because there is no App to own
// them. Defined here rather than in the header so the header stays free of their definitions.
struct Server::OwnedServices {
  VFS vfs = {};
  JobManager job_manager = {};
  EventSystem event_system = {};
  Timestep timestep = {};
};

Server::Server(std::filesystem::path assets_directory)
    : owned_(std::make_unique<OwnedServices>()),
      assets_directory_(std::move(assets_directory)),
      vfs_(owned_->vfs),
      job_manager_(owned_->job_manager),
      event_system_(owned_->event_system),
      timestep_(owned_->timestep) {
  set_instance(this);
}

Server::Server(VFS& vfs, JobManager& job_manager, EventSystem& event_system, const Timestep& timestep)
    : owned_(nullptr),
      vfs_(vfs),
      job_manager_(job_manager),
      event_system_(event_system),
      timestep_(timestep) {
  set_instance(this);
}

Server::~Server() {
  if (instance_ == this) {
    instance_ = nullptr;
  }
}

auto Server::set_instance(Server* instance) -> void { instance_ = instance; }

auto Server::init(this Server& self) -> std::expected<void, std::string> {
  ZoneScoped;

  // Standalone only: App does all of this for a hosted server, and doing it twice would mount the
  // VFS again and start a second worker pool.
  if (self.owned_) {
    self.vfs_.mount_dir(VFS::APP_DIR, std::filesystem::absolute(self.assets_directory_));

    if (const auto result = self.job_manager_.init(); !result.has_value()) {
      return std::unexpected(fmt::format("Failed to initialize the job manager: {}", result.error()));
    }

    if (const auto result = self.event_system_.init(); !result.has_value()) {
      return std::unexpected(fmt::format("Failed to initialize the event system: {}", result.error()));
    }
  }

  if (!self.registry.init()) {
    return std::unexpected("Failed to initialize a simulation module.");
  }

  return {};
}

auto Server::deinit(this Server& self) -> std::expected<void, std::string> {
  ZoneScoped;

  self.scenes_.clear();

  if (!self.registry.deinit()) {
    return std::unexpected("Failed to deinitialize a simulation module.");
  }

  if (self.owned_) {
    self.job_manager_.wait();
    static_cast<void>(self.event_system_.deinit());
    static_cast<void>(self.job_manager_.deinit());
  }

  return {};
}

auto Server::register_scene(this Server& self, const std::shared_ptr<Scene>& scene) -> void {
  if (!scene) {
    return;
  }

  if (std::ranges::find(self.scenes_, scene) == self.scenes_.end()) {
    self.scenes_.emplace_back(scene);
  }
}

auto Server::unregister_scene(this Server& self, const Scene* scene) -> void {
  std::erase_if(self.scenes_, [scene](const std::shared_ptr<Scene>& s) { return s.get() == scene; });
}

auto Server::tick(this Server& self, const Timestep& timestep) -> void {
  ZoneScoped;

  for (const auto& scene : self.scenes_) {
    scene->runtime_update(timestep);
  }
}

auto Server::tick_owned(this Server& self) -> const Timestep& {
  ZoneScoped;

  OX_CHECK_NULL(self.owned_);

  self.owned_->timestep.on_update();
  self.tick(self.owned_->timestep);

  return self.owned_->timestep;
}

auto Server::set_tick_rate(this Server& self, const f64 ticks_per_second) -> void {
  OX_CHECK_NULL(self.owned_);

  if (ticks_per_second > 0.0) {
    self.owned_->timestep.set_max_frame_time(1000.0 / ticks_per_second);
  } else {
    self.owned_->timestep.reset_max_frame_time();
  }
}

auto Server::load_scene(this Server& self, const std::filesystem::path& path) -> std::shared_ptr<Scene> {
  ZoneScoped;

  auto scene = std::make_shared<Scene>();
  scene->init(path.stem().string());

  if (!scene->load_from_file(path)) {
    OX_LOG_ERROR("Could not load scene \"{}\".", path);
    return nullptr;
  }

  self.register_scene(scene);
  return scene;
}

auto Server::get_vfs() -> VFS& {
  OX_CHECK_NULL(instance_);
  return instance_->vfs_;
}

auto Server::get_job_manager() -> JobManager& {
  OX_CHECK_NULL(instance_);
  return instance_->job_manager_;
}

auto Server::get_event_system() -> EventSystem& {
  OX_CHECK_NULL(instance_);
  return instance_->event_system_;
}

auto Server::get_timestep() -> const Timestep& {
  OX_CHECK_NULL(instance_);
  return instance_->timestep_;
}
} // namespace ox
