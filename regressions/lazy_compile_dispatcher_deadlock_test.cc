#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <atomic>
#include <vector>
#include "src/compiler-dispatcher/lazy-compile-dispatcher.h"

using namespace v8::internal;

// Mock LazyCompileDispatcher to expose internal behavior for testing.
class MockLazyCompileDispatcher : public LazyCompileDispatcher {
 public:
  MockLazyCompileDispatcher(Isolate* isolate, Platform* platform, size_t max_stack_size)
      : LazyCompileDispatcher(isolate, platform, max_stack_size) {}

  MOCK_METHOD(void, NotifyRemovedBackgroundJob, (const base::MutexGuard&), (override));
};

// Test fixture for setting up the environment.
class LazyCompileDispatcherDeadlockTest : public ::testing::Test {
 protected:
  void SetUp() override {
    isolate_ = nullptr;  // Replace with actual isolate initialization if needed.
    platform_ = nullptr; // Replace with actual platform initialization if needed.
    max_stack_size_ = 1024;
    dispatcher_ = new MockLazyCompileDispatcher(isolate_, platform_, max_stack_size_);
  }

  void TearDown() override {
    delete dispatcher_;
  }

  Isolate* isolate_;
  Platform* platform_;
  size_t max_stack_size_;
  MockLazyCompileDispatcher* dispatcher_;
};

// Test Case 1: Deadlock in DeleteJob.
TEST_F(LazyCompileDispatcherDeadlockTest, DeadlockInDeleteJob) {
  LazyCompileDispatcher::Job job1(nullptr);
  LazyCompileDispatcher::Job job2(nullptr);

  // Add jobs to shared resource.
  dispatcher_->pending_background_jobs_.push_back(&job1);
  dispatcher_->pending_background_jobs_.push_back(&job2);

  // Simulate multiple threads trying to delete jobs.
  std::thread t1([&]() {
    base::MutexGuard lock(&dispatcher_->mutex_);
    dispatcher_->DeleteJob(&job1, lock);
  });

  std::thread t2([&]() {
    base::MutexGuard lock(&dispatcher_->mutex_);
    dispatcher_->DeleteJob(&job2, lock);
  });

  // Join threads and observe if they deadlock.
  t1.join();
  t2.join();

  // If the test completes, there is no deadlock. Otherwise, a deadlock exists.
  SUCCEED();
}

// Test Case 2: Deadlock in WaitForJobIfRunningOnBackground.
TEST_F(LazyCompileDispatcherDeadlockTest, DeadlockInWaitForJobIfRunningOnBackground) {
  LazyCompileDispatcher::Job job(nullptr);
  dispatcher_->pending_background_jobs_.push_back(&job);

  std::atomic<bool> start_thread2(false);

  // Thread 1 simulates waiting for a job to finish.
  std::thread t1([&]() {
    base::MutexGuard lock(&dispatcher_->mutex_);
    start_thread2.store(true);  // Signal Thread 2 to start.
    dispatcher_->WaitForJobIfRunningOnBackground(&job, lock);
  });

  // Thread 2 modifies the job list concurrently.
  std::thread t2([&]() {
    while (!start_thread2.load()) {
      std::this_thread::yield();
    }
    base::MutexGuard lock(&dispatcher_->mutex_);
    dispatcher_->pending_background_jobs_.erase(
        std::remove(dispatcher_->pending_background_jobs_.begin(),
                    dispatcher_->pending_background_jobs_.end(),
                    &job),
        dispatcher_->pending_background_jobs_.end());
  });

  t1.join();
  t2.join();

  SUCCEED();
}

// Test Case 3: Deadlock in AbortJob.
TEST_F(LazyCompileDispatcherDeadlockTest, DeadlockInAbortJob) {
  LazyCompileDispatcher::Job job(nullptr);
  dispatcher_->pending_background_jobs_.push_back(&job);

  std::atomic<bool> start_thread2(false);

  // Thread 1 simulates aborting a job.
  std::thread t1([&]() {
    base::MutexGuard lock(&dispatcher_->mutex_);
    start_thread2.store(true);  // Signal Thread 2 to start.
    dispatcher_->AbortJob(DirectHandle<SharedFunctionInfo>(nullptr));
  });

  // Thread 2 accesses the same resource concurrently.
  std::thread t2([&]() {
    while (!start_thread2.load()) {
      std::this_thread::yield();
    }
    base::MutexGuard lock(&dispatcher_->mutex_);
    dispatcher_->pending_background_jobs_.push_back(&job);
  });

  t1.join();
  t2.join();

  SUCCEED();
}
