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

#ifndef CHRE_HOST_HAL_CLIENT_H_
#define CHRE_HOST_HAL_CLIENT_H_

#include <cinttypes>
#include <memory>
#include <shared_mutex>
#include <unordered_set>
#include <vector>

#include <aidl/android/hardware/contexthub/ContextHubMessage.h>
#include <aidl/android/hardware/contexthub/HostEndpointInfo.h>
#include <aidl/android/hardware/contexthub/IContextHub.h>
#include <aidl/android/hardware/contexthub/IContextHubCallback.h>
#include <aidl/android/hardware/contexthub/NanoappBinary.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "chre_host/log.h"
#include "host/hal_generic/common/hal_error.h"

namespace android::chre {

using ::aidl::android::hardware::contexthub::ContextHubInfo;
using ::aidl::android::hardware::contexthub::ContextHubMessage;
using ::aidl::android::hardware::contexthub::HostEndpointInfo;
using ::aidl::android::hardware::contexthub::IContextHub;
using ::aidl::android::hardware::contexthub::IContextHubCallback;
using ::aidl::android::hardware::contexthub::IContextHubDefault;
using ::aidl::android::hardware::contexthub::NanoappBinary;
using ::aidl::android::hardware::contexthub::Setting;
using ::ndk::ScopedAStatus;

/**
 * A class exposing CHRE HAL APIs to clients and taking care of binder
 * (re)connection.
 *
 * <p>This class also maintains a set of connected host endpoints, using which
 * it is enforced that a message can only be sent to/from an endpoint id that is
 * already connected to HAL.
 *
 * <p>When the binder connection to HAL is disconnected HalClient will have a
 * death recipient to re-establish the connection. Be aware that it is a
 * client's responsibility to reconnect all the endpoints. This is because when
 * the binder connection is set up, it is possible that all the API calls can't
 * reach CHRE yet if CHRE also restarts at the same time. A client should rely
 * on IContextHubCallback.handleContextHubAsyncEvent() to handle the RESTARTED
 * event which is a signal that CHRE is up running.
 * TODO(b/309690054): In reality HAL rarely crashes. When it does, CHRE could
 *   still be up running so HalClient should try to re-establish the states of
 *   connected endpoints by calling connectEndpoint anyway after the
 *   binder link reconnects.
 *
 * TODO(b/309690054): When CHRE crashes, HAL will bypass this HalCLient library
 *   and notify the client directly via
 *   IContextHubCallback.handleContextHubAsyncEvent(). In this situation
 *   HalClient.mConnectedEndpoints become out-of-date.
 *
 * TODO(b/297912356): The name of this class is the same as an internal struct
 *   used by HalClientManager. Consider rename the latter one to avoid confusion
 *
 */
class HalClient {
 public:
  static constexpr int32_t kDefaultContextHubId = 0;
  static std::unique_ptr<HalClient> create(
      const std::shared_ptr<IContextHubCallback> &callback,
      int32_t contextHubId = kDefaultContextHubId) {
    auto halClient =
        std::unique_ptr<HalClient>(new HalClient(callback, contextHubId));
    if (halClient->initConnection() != HalError::SUCCESS) {
      return nullptr;
    }
    return halClient;
  }

  ScopedAStatus queryNanoapps() {
    return callIfConnectedOrError(
        [&]() { return mContextHub->queryNanoapps(mContextHubId); });
  }

  ScopedAStatus sendMessage(const ContextHubMessage &message);

  ScopedAStatus connectEndpoint(const HostEndpointInfo &hostEndpointInfo);

  ScopedAStatus disconnectEndpoint(char16_t hostEndpointId);

 protected:
  explicit HalClient(const std::shared_ptr<IContextHubCallback> &callback,
                     int32_t contextHubId = kDefaultContextHubId)
      : mContextHubId(contextHubId), mCallback(callback) {
    ABinderProcess_startThreadPool();
    mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(
        AIBinder_DeathRecipient_new(onHalDisconnected));
  }

  /**
   * Initializes the connection to CHRE HAL.
   */
  HalError initConnection();

  using HostEndpointId = char16_t;

  const std::string kAidlServiceName =
      std::string() + IContextHub::descriptor + "/default";

  /** The callback for a disconnected HAL binder connection. */
  static void onHalDisconnected(void *cookie);

  ScopedAStatus callIfConnectedOrError(
      const std::function<ScopedAStatus()> &func) {
    std::shared_lock<std::shared_mutex> sharedLock(mConnectionLock);
    if (mContextHub == nullptr) {
      return fromHalError(HalError::BINDER_DISCONNECTED);
    }
    return func();
  }

  bool isEndpointConnected(HostEndpointId hostEndpointId) {
    std::shared_lock<std::shared_mutex> lock(mStateLock);
    return mConnectedEndpoints.find(hostEndpointId) !=
           mConnectedEndpoints.end();
  }

  void insertConnectedEndpoint(HostEndpointId hostEndpointId) {
    std::lock_guard<std::shared_mutex> lock(mStateLock);
    mConnectedEndpoints.insert(hostEndpointId);
  }

  void removeConnectedEndpoint(HostEndpointId hostEndpointId) {
    std::lock_guard<std::shared_mutex> lock(mStateLock);
    mConnectedEndpoints.erase(hostEndpointId);
  }

  static ScopedAStatus fromHalError(HalError errorCode) {
    return errorCode == HalError::SUCCESS
               ? ScopedAStatus::ok()
               : ScopedAStatus::fromServiceSpecificError(
                     static_cast<int32_t>(errorCode));
  }

  // Multi-contextHub is not supported at this moment.
  int32_t mContextHubId;

  // The lock guarding mConnectedEndpoints.
  std::shared_mutex mStateLock;
  std::unordered_set<HostEndpointId> mConnectedEndpoints{};

  // The lock guarding mContextHub.
  std::shared_mutex mConnectionLock;
  std::shared_ptr<IContextHub> mContextHub;

  // Handler of the binder disconnection event with HAL.
  ndk::ScopedAIBinder_DeathRecipient mDeathRecipient;

  std::shared_ptr<IContextHubCallback> mCallback;
};

}  // namespace android::chre
#endif  // CHRE_HOST_HAL_CLIENT_H_