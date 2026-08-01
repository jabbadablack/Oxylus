#include <algorithm>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <thread>

#include "Core/JobManager.hpp"

class JobManagerTest : public ::testing::Test {
protected:
  void SetUp() override {
    manager = std::make_unique<ox::JobManager>();
    // Without init() there are no worker threads, so the first wait() blocks forever on a job
    // nothing can run. This is what hung the suite.
    std::ignore = manager->init();
  }

  void TearDown() override {
    manager->shutdown();
    std::ignore = manager->deinit();
  }

  std::unique_ptr<ox::JobManager> manager = nullptr;
};

// --- Basic Functionality Tests ---

TEST_F(JobManagerTest, ExecutesSingleJob) {
  std::atomic<bool> executed{false};

  manager->push_job_name("TestJob");
  auto job = ox::Job::create([&] { executed = true; });
  manager->pop_job_name();

  manager->submit(job);
  manager->wait();

  EXPECT_TRUE(executed);
}

// Completion order across a worker pool is not a guarantee - the point of the pool is that jobs run
// concurrently. What is guaranteed is that every submitted job runs exactly once before wait()
// returns. (This previously asserted submission order, which only held because the manager was never
// started and so nothing ever ran.)
TEST_F(JobManagerTest, ExecutesEverySubmittedJob) {
  std::vector<int> executed;
  std::mutex executed_mutex;

  manager->push_job_name("OrderTest");

  for (int i = 0; i < 5; ++i) {
    auto job = ox::Job::create([&, i] {
      std::lock_guard lock(executed_mutex);
      executed.push_back(i);
    });
    manager->submit(job);
  }
  manager->pop_job_name();

  manager->wait();

  std::ranges::sort(executed);
  EXPECT_EQ(executed, std::vector<int>({0, 1, 2, 3, 4}));
}

// --- Thread Safety Tests ---

TEST_F(JobManagerTest, HandlesConcurrentAccess) {
  std::atomic<int> counter{0};
  constexpr int kIterations = 1000;

  manager->push_job_name("ConcurrentTest");
  for (int i = 0; i < kIterations; ++i) {
    manager->submit(ox::Job::create([&] { counter.fetch_add(1, std::memory_order_relaxed); }));
  }
  manager->pop_job_name();

  manager->wait();
  EXPECT_EQ(counter.load(), kIterations);
}

TEST_F(JobManagerTest, SafeWithSimultaneousSubmitAndShutdown) {
  manager->push_job_name("StressTest");
  std::thread worker([this] {
    for (int i = 0; i < 100; ++i) {
      manager->submit(ox::Job::create([] {}));
    }
  });
  manager->pop_job_name();

  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  manager->shutdown();
  worker.join();

  SUCCEED(); // Just testing we don't crash
}

// --- Tracking System Tests ---

TEST_F(JobManagerTest, TracksJobStatusWhenEnabled) {
  manager->get_tracker().start_tracking();

  // The job has to be held in flight to observe it as "working". An empty job finishes long before
  // the assertion runs, which is why this raced once the manager actually started its workers.
  std::atomic<bool> release{false};
  std::atomic<bool> started{false};

  manager->push_job_name("TrackedJob");
  auto job = ox::Job::create([&] {
    started.store(true);
    while (!release.load()) {
      std::this_thread::yield();
    }
  });
  manager->submit(job);
  manager->pop_job_name();

  while (!started.load()) {
    std::this_thread::yield();
  }

  auto status = manager->get_tracker().get_status();
  ASSERT_EQ(status.size(), 1);
  EXPECT_EQ(status[0].first, "TrackedJob");
  EXPECT_TRUE(status[0].second); // in flight

  release.store(true);
  manager->wait();

  status = manager->get_tracker().get_status();
  EXPECT_FALSE(status[0].second); // done
}

TEST_F(JobManagerTest, NoTrackingWhenDisabled) {
  manager->get_tracker().stop_tracking();

  manager->push_job_name("Untracked");
  manager->submit(ox::Job::create([] {}));
  manager->pop_job_name();
  manager->wait();

  EXPECT_TRUE(manager->get_tracker().get_status().empty());
}

TEST_F(JobManagerTest, CleanupOldJobs) {
  manager->get_tracker().start_tracking();

  const std::string job_name = "TempJob";
  manager->push_job_name(job_name);
  auto job = ox::Job::create([] {});
  manager->submit(job);
  manager->pop_job_name();
  manager->wait();

  // Simulate time passing
  if (auto record = manager->get_tracker().find_job(job_name)) {
    record->get().completion_time -= std::chrono::seconds(3);

    // Cleanup jobs older than 1 second
    manager->get_tracker().cleanup_old(std::chrono::seconds(1));

    // Verify the job was cleaned up
    EXPECT_TRUE(manager->get_tracker().get_status().empty());
  } else {
    FAIL() << "Job record not found for: " << job_name;
  }
}
