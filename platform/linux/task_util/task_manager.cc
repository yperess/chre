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

#include <cassert>
#include <iostream>
#include <limits>
#include <memory>

#include "chre/platform/linux/task_util/task_manager.h"

namespace chre {

TaskManager::TaskManager()
    : mQueue(std::greater<Task>()),
      mCurrentTask(nullptr),
      mContinueRunningThread(true),
      mCurrentId(0) {
  mThread = std::thread(&TaskManager::run, this);
}

TaskManager::~TaskManager() {
  flushTasks();

  mContinueRunningThread = false;
  mConditionVariable.notify_all();
  if (mThread.joinable()) {
    mThread.join();
  }
}

std::optional<uint32_t> TaskManager::addTask(
    const Task::TaskFunction &func, std::chrono::nanoseconds intervalOrDelay,
    bool isOneShot) {
  if (!mContinueRunningThread) {
    LOGW("Execution thread is shutting down. Cannot add a task.");
    return {};
  }

  std::lock_guard<std::mutex> lock(mMutex);
  assert(mCurrentId < std::numeric_limits<uint32_t>::max());
  uint32_t returnId = mCurrentId++;
  Task task(func, intervalOrDelay, returnId, isOneShot);
  if (!mQueue.push(task)) {
    return {};
  }

  if (mQueue.top().getId() == task.getId()) {
    mConditionVariable.notify_all();
  }
  return returnId;
}

bool TaskManager::cancelTask(uint32_t taskId) {
  std::lock_guard<std::mutex> lock(mMutex);

  bool success = false;
  if (!mContinueRunningThread) {
    LOGW("Execution thread is shutting down. Cannot cancel a task.");
  } else if (mCurrentTask != nullptr && mCurrentTask->getId() == taskId) {
    // The currently executing task may want to cancel itself.
    mCurrentTask->cancel();
    success = true;
  } else {
    for (auto iter = mQueue.begin(); iter != mQueue.end(); ++iter) {
      if (iter->getId() == taskId) {
        iter->cancel();
        success = true;
        break;
      }
    }
  }

  return success;
}

void TaskManager::flushTasks() {
  std::lock_guard<std::mutex> lock(mMutex);
  while (!mQueue.empty()) {
    mQueue.pop();
  }
}

void TaskManager::run() {
  auto stopWaitingPredicate = [this]() {
    return !mContinueRunningThread || !mQueue.empty();
  };

  while (true) {
    std::unique_lock<std::mutex> lock(mMutex);

    if (mQueue.empty()) {
      mConditionVariable.wait(lock, stopWaitingPredicate);
    } else {
      Task &task = mQueue.top();
      if (!task.isReadyToExecute()) {
        mConditionVariable.wait_until(lock, task.getExecutionTimestamp(),
                                      stopWaitingPredicate);
      }
    }
    if (!mContinueRunningThread) {
      return;
    }

    if (!mQueue.empty()) {
      Task task = mQueue.top();
      if (task.isReadyToExecute()) {
        mQueue.pop();
        mCurrentTask = &task;
        lock.unlock();
        task.execute();
        lock.lock();
        mCurrentTask = nullptr;
        if (task.isRepeating() && !mQueue.push(task)) {
          LOGE("TaskManager: Could not push task to priority queue");
        }
      }
    }
  }
}

}  // namespace chre
