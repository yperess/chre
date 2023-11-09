/*
 * Copyright (C) 2022 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <chrono>
#include <cmath>
#include <mutex>
#include <optional>

#include "gtest/gtest.h"

#include "chre/platform/linux/task_util/task_manager.h"

namespace {

TEST(TaskManager, TaskManagerCanDoNothing) {
  chre::TaskManager taskManager;
}

TEST(TaskManager, FlushTasksCanBeCalledMultipleTimes) {
  chre::TaskManager taskManager;

  constexpr uint32_t numCallsToFlush = 50;
  for (uint32_t i = 0; i < numCallsToFlush; ++i) {
    taskManager.flushTasks();
  }
}

TEST(TaskManager, TaskCanBeAddedToTopOfQueue) {
  std::mutex mutex;
  std::condition_variable condVar;
  chre::TaskManager taskManager;

  std::unique_lock<std::mutex> lock(mutex);

  auto notifyFunc = [&mutex, &condVar]() {
    { std::unique_lock<std::mutex> lock(mutex); }

    condVar.notify_all();
  };

  auto taskAfterDelay = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::seconds(1000));
  std::optional<uint32_t> taskAfter =
      taskManager.addTask([]() { FAIL(); },
                          /* intervalOrDelay */ taskAfterDelay,
                          /* isOneShot */ true);
  EXPECT_TRUE(taskAfter.has_value());

  std::optional<uint32_t> taskBefore =
      taskManager.addTask(notifyFunc,
                          /* intervalOrDelay */ std::chrono::nanoseconds(100),
                          /* isOneShot */ true);
  EXPECT_TRUE(taskBefore.has_value());

  condVar.wait(lock);
  taskManager.flushTasks();
}

TEST(TaskManager, MultipleNonRepeatingTasksAreExecuted) {
  uint32_t counter = 0;
  std::mutex mutex;
  std::condition_variable condVar;
  chre::TaskManager taskManager;

  std::unique_lock<std::mutex> lock(mutex);

  constexpr uint32_t numTasks = 50;
  auto incrementFunc = [&mutex, &condVar, &counter]() {
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++counter;
    }

    condVar.notify_all();
  };

  for (uint32_t i = 0; i < numTasks; ++i) {
    std::optional<uint32_t> taskId =
        taskManager.addTask(incrementFunc,
                            /* intervalOrDelay */ std::chrono::nanoseconds(0));
    EXPECT_TRUE(taskId.has_value());
  }

  condVar.wait(lock, [&counter]() { return counter >= numTasks; });
  taskManager.flushTasks();
  EXPECT_EQ(counter, numTasks);
}

TEST(TaskManager, RepeatingAndOneShotTasksCanExecuteTogether) {
  uint32_t counter = 0;
  std::mutex mutex;
  std::condition_variable condVar;
  chre::TaskManager taskManager;

  std::unique_lock<std::mutex> lock(mutex);

  constexpr uint32_t numTasks = 50;
  auto incrementFunc = [&mutex, &condVar, &counter]() {
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++counter;
    }

    condVar.notify_all();
  };

  for (uint32_t i = 0; i < numTasks; ++i) {
    std::optional<uint32_t> taskId =
        taskManager.addTask(incrementFunc,
                            /* intervalOrDelay */ std::chrono::nanoseconds(0));
    EXPECT_TRUE(taskId.has_value());
  }

  constexpr std::chrono::nanoseconds interval(50);
  std::optional<uint32_t> taskId = taskManager.addTask(incrementFunc, interval);
  ASSERT_TRUE(taskId.has_value());

  constexpr uint32_t taskRepeatTimesMax = 5;
  condVar.wait(
      lock, [&counter]() { return counter >= numTasks + taskRepeatTimesMax; });
  EXPECT_TRUE(taskManager.cancelTask(taskId.value()));
  taskManager.flushTasks();
  EXPECT_GE(counter, numTasks + taskRepeatTimesMax);
}

TEST(TaskManager, TasksCanBeFlushedEvenIfNotCancelled) {
  uint32_t counter = 0;
  std::mutex mutex;
  std::condition_variable condVar;
  chre::TaskManager taskManager;

  std::unique_lock<std::mutex> lock(mutex);

  constexpr uint32_t numTasks = 50;
  auto incrementFunc = [&mutex, &condVar, &counter]() {
    {
      std::unique_lock<std::mutex> lock(mutex);
      ++counter;
    }

    condVar.notify_all();
  };

  for (uint32_t i = 0; i < numTasks; ++i) {
    std::optional<uint32_t> taskId =
        taskManager.addTask(incrementFunc,
                            /* intervalOrDelay */ std::chrono::nanoseconds(0));
    EXPECT_TRUE(taskId.has_value());
  }

  constexpr std::chrono::nanoseconds interval(50);
  std::optional<uint32_t> taskId = taskManager.addTask(incrementFunc, interval);
  ASSERT_TRUE(taskId.has_value());

  constexpr uint32_t taskRepeatTimesMax = 5;
  condVar.wait(
      lock, [&counter]() { return counter >= numTasks + taskRepeatTimesMax; });
  taskManager.flushTasks();
  EXPECT_GE(counter, numTasks + taskRepeatTimesMax);
}

}  // namespace
