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

#include "chre_host/metrics_reporter.h"

#include <android/binder_manager.h>
#include <chre_atoms_log.h>
#include <chre_host/log.h>
#include <mutex>

namespace android::chre {

using ::aidl::android::frameworks::stats::IStats;
using ::aidl::android::frameworks::stats::VendorAtom;
using ::aidl::android::frameworks::stats::VendorAtomValue;
using ::android::chre::Atoms::CHRE_AP_WAKE_UP_OCCURRED;
using ::android::chre::Atoms::CHRE_HAL_NANOAPP_LOAD_FAILED;
using ::android::chre::Atoms::ChreHalNanoappLoadFailed;

std::shared_ptr<IStats> MetricsReporter::getStatsService() {
  const std::string statsServiceName =
      std::string(IStats::descriptor).append("/default");
  if (!AServiceManager_isDeclared(statsServiceName.c_str())) {
    LOGE("Stats service is not declared.");
    return nullptr;
  }

  ndk::SpAIBinder statsServiceBinder =
      ndk::SpAIBinder(AServiceManager_waitForService(statsServiceName.c_str()));
  if (statsServiceBinder.get() == nullptr) {
    LOGE("Failed to get the IStats service binder");
    return nullptr;
  }

  binder_status_t status = AIBinder_linkToDeath(
      statsServiceBinder.get(), AIBinder_DeathRecipient_new([](void *cookie) {
        MetricsReporter *metricsReporter =
            static_cast<MetricsReporter *>(cookie);
        metricsReporter->onBinderDied();
      }),
      this);
  if (status != STATUS_OK) {
    LOGE("Failed to link to death the stats service binder");
    return nullptr;
  }

  std::shared_ptr<IStats> statsService = IStats::fromBinder(statsServiceBinder);
  if (statsService == nullptr) {
    LOGE("Failed to get IStats service");
    return nullptr;
  }
  return statsService;
}

bool MetricsReporter::reportMetric(const VendorAtom &atom) {
  ndk::ScopedAStatus ret;
  {
    std::lock_guard<std::mutex> lock(mStatsServiceMutex);
    if (mStatsService == nullptr) {
      mStatsService = getStatsService();
      if (mStatsService == nullptr) {
        return false;
      }
    }

    ret = mStatsService->reportVendorAtom(atom);
  }

  if (!ret.isOk()) {
    LOGE("Failed to report vendor atom");
  }
  return ret.isOk();
}

bool MetricsReporter::logApWakeupOccurred(uint64_t nanoappId) {
  std::vector<VendorAtomValue> values(1);
  values[0].set<VendorAtomValue::longValue>(nanoappId);

  const VendorAtom atom{
      .atomId = CHRE_AP_WAKE_UP_OCCURRED,
      .values{std::move(values)},
  };

  return reportMetric(atom);
}

bool MetricsReporter::logNanoappLoadFailed(
    uint64_t nanoappId, ChreHalNanoappLoadFailed::Type type,
    ChreHalNanoappLoadFailed::Reason reason) {
  std::vector<VendorAtomValue> values(3);
  values[0].set<VendorAtomValue::longValue>(nanoappId);
  values[1].set<VendorAtomValue::intValue>(type);
  values[2].set<VendorAtomValue::intValue>(reason);

  const VendorAtom atom{
      .atomId = CHRE_HAL_NANOAPP_LOAD_FAILED,
      .values{std::move(values)},
  };

  return reportMetric(atom);
}

void MetricsReporter::onBinderDied() {
  LOGI("MetricsReporter: stats service died - reconnecting");

  std::lock_guard<std::mutex> lock(mStatsServiceMutex);
  mStatsService.reset();
  mStatsService = getStatsService();
}

}  // namespace android::chre
