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
#include <optional>
#include <thread>

#include "gtest/gtest.h"

#include "chre/platform/linux/task_util/task_manager.h"

namespace {

uint32_t gVarTaskManager = 0;

constexpr auto incrementGVar = []() { ++gVarTaskManager; };

TEST(TaskManager, FlushTasks) {
  chre::TaskManager taskManager;
  for (uint32_t i = 0; i < 50; ++i) {
    taskManager.flushTasks();
  }
}

TEST(TaskManager, MultipleNonRepeatingTasks) {
  chre::TaskManager taskManager;
  gVarTaskManager = 0;
  constexpr uint32_t numTasks = 50;
  for (uint32_t i = 0; i < numTasks; ++i) {
    taskManager.addTask(incrementGVar, std::chrono::nanoseconds(0));
    std::this_thread::sleep_for(std::chrono::nanoseconds(50));
  }
  taskManager.flushTasks();
  EXPECT_TRUE(gVarTaskManager == numTasks);
}

TEST(TaskManager, MultipleTypesOfTasks) {
  chre::TaskManager taskManager;
  gVarTaskManager = 0;
  constexpr uint32_t numTasks = 50;
  for (uint32_t i = 0; i < numTasks; ++i) {
    taskManager.addTask(incrementGVar, std::chrono::nanoseconds(0));
    std::this_thread::sleep_for(std::chrono::nanoseconds(50));
  }
  uint32_t nanosecondsToRepeat = 100;
  std::optional<uint32_t> taskId = taskManager.addTask(
      incrementGVar, std::chrono::nanoseconds(nanosecondsToRepeat));
  EXPECT_TRUE(taskId.has_value());
  uint32_t taskRepeatTimesMax = 11;
  std::this_thread::sleep_for(
      std::chrono::nanoseconds(nanosecondsToRepeat * taskRepeatTimesMax));
  EXPECT_TRUE(taskManager.cancelTask(taskId.value()));
  taskManager.flushTasks();
  EXPECT_TRUE(gVarTaskManager >= numTasks + taskRepeatTimesMax - 1);
}

TEST(TaskManager, FlushTasksWithoutCancel) {
  chre::TaskManager taskManager;
  gVarTaskManager = 0;
  constexpr uint32_t numTasks = 50;
  for (uint32_t i = 0; i < numTasks; ++i) {
    taskManager.addTask(incrementGVar, std::chrono::nanoseconds(0));
    std::this_thread::sleep_for(std::chrono::nanoseconds(50));
  }
  uint32_t nanosecondsToRepeat = 100;
  std::optional<uint32_t> taskId = taskManager.addTask(
      incrementGVar, std::chrono::nanoseconds(nanosecondsToRepeat));
  EXPECT_TRUE(taskId.has_value());
  uint32_t taskRepeatTimesMax = 11;
  std::this_thread::sleep_for(
      std::chrono::nanoseconds(nanosecondsToRepeat * taskRepeatTimesMax));
  taskManager.flushTasks();
  EXPECT_TRUE(gVarTaskManager >= numTasks + taskRepeatTimesMax - 1);
}

}  // namespace
