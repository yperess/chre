/*
 * Copyright (C) 2023 The Android Open Source Project
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

#ifndef LOG_TAG
#define LOG_TAG "CHRE.HAL.CLIENT"
#endif

#include "chre_host/hal_client.h"

#include <utils/SystemClock.h>

#include <cinttypes>
#include <thread>

namespace android::chre {

using ::aidl::android::hardware::contexthub::IContextHub;
using ::aidl::android::hardware::contexthub::IContextHubCallback;
using ::ndk::ScopedAStatus;

HalError HalClient::initConnection() {
  std::lock_guard<std::shared_mutex> lockGuard{mConnectionLock};

  if (mContextHub != nullptr) {
    LOGW("Context hub is already connected.");
    return HalError::SUCCESS;
  }

  // Wait to connect to the service. Note that we don't do local retries because
  // we're relying on the internal retries in AServiceManager_waitForService().
  // If HAL service has just restarted, it can take a few seconds to connect.
  ndk::SpAIBinder binder{
      AServiceManager_waitForService(kAidlServiceName.c_str())};
  if (binder.get() == nullptr) {
    return HalError::BINDER_CONNECTION_FAILED;
  }

  // Link the death recipient to handle the binder disconnection event.
  if (AIBinder_linkToDeath(binder.get(), mDeathRecipient.get(), this) !=
      STATUS_OK) {
    LOGE("Failed to link the binder death recipient");
    return HalError::LINK_DEATH_RECIPIENT_FAILED;
  }

  // Retrieve a handle of context hub service.
  mContextHub = IContextHub::fromBinder(binder);
  if (mContextHub == nullptr) {
    LOGE("Got null context hub from the binder connection");
    return HalError::NULL_CONTEXT_HUB_FROM_BINDER;
  }

  // Register an IContextHubCallback.
  ScopedAStatus status =
      mContextHub->registerCallback(kDefaultContextHubId, mCallback);
  if (!status.isOk()) {
    LOGE("Unable to register callback: %s", status.getDescription().c_str());
    return HalError::CALLBACK_REGISTRATION_FAILED;
  }
  LOGI("Successfully (re)connected to HAL");
  return HalError::SUCCESS;
}

void HalClient::onHalDisconnected(void *cookie) {
  LOGW("CHRE HAL is disconnected. Reconnecting...");
  int64_t startTime = ::android::elapsedRealtime();
  auto *halClient = static_cast<HalClient *>(cookie);
  {
    std::lock_guard<std::shared_mutex> lock(halClient->mConnectionLock);
    halClient->mContextHub = nullptr;
  }
  {
    std::lock_guard<std::shared_mutex> lock(halClient->mStateLock);
    halClient->mConnectedEndpoints.clear();
  }
  HalError result = halClient->initConnection();
  uint64_t duration = ::android::elapsedRealtime() - startTime;
  if (result != HalError::SUCCESS) {
    LOGE("Failed to fully reconnect to HAL after %" PRIu64
         "ms, HalErrorCode: %" PRIi32,
         duration, result);
    return;
  }
  LOGI("Reconnected to HAL after %" PRIu64 "ms", duration);
}

ScopedAStatus HalClient::connectEndpoint(
    const HostEndpointInfo &hostEndpointInfo) {
  HostEndpointId endpointId = hostEndpointInfo.hostEndpointId;
  if (isEndpointConnected(endpointId)) {
    // Connecting the endpoint again even though it is already connected to let
    // HAL and/or CHRE be the single place to control the behavior.
    LOGW("Endpoint id %" PRIu16 " is already connected.", endpointId);
  }
  ScopedAStatus result = callIfConnectedOrError(
      [&]() { return mContextHub->onHostEndpointConnected(hostEndpointInfo); });
  if (result.isOk()) {
    insertConnectedEndpoint(hostEndpointInfo.hostEndpointId);
  } else {
    LOGE("Failed to connect the endpoint id %" PRIu16,
         hostEndpointInfo.hostEndpointId);
  }
  return result;
}

ScopedAStatus HalClient::disconnectEndpoint(HostEndpointId hostEndpointId) {
  if (!isEndpointConnected(hostEndpointId)) {
    // Disconnecting the endpoint again even though it is already disconnected
    // to let HAL and/or CHRE be the single place to control the behavior.
    LOGW("Endpoint id %" PRIu16 " is already disconnected.", hostEndpointId);
  }
  ScopedAStatus result = callIfConnectedOrError([&]() {
    return mContextHub->onHostEndpointDisconnected(hostEndpointId);
  });
  if (result.isOk()) {
    removeConnectedEndpoint(hostEndpointId);
  } else {
    LOGE("Failed to disconnect the endpoint id %" PRIu16, hostEndpointId);
  }
  return result;
}

ScopedAStatus HalClient::sendMessage(const ContextHubMessage &message) {
  uint16_t hostEndpointId = message.hostEndPoint;
  if (!isEndpointConnected(hostEndpointId)) {
    // For now this is still allowed but in the future
    // HalError::UNEXPECTED_ENDPOINT_STATE will be returned.
    LOGW("Endpoint id %" PRIu16
         " is unknown or disconnected. Message sending will be skipped in the "
         "future.");
  }
  return callIfConnectedOrError(
      [&]() { return mContextHub->sendMessageToHub(mContextHubId, message); });
}
}  // namespace android::chre