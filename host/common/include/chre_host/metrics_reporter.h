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

#ifndef CHRE_HOST_METRICS_REPORTER_H_
#define CHRE_HOST_METRICS_REPORTER_H_

#include <aidl/android/frameworks/stats/IStats.h>
#include <chre_atoms_log.h>
#include <mutex>

namespace android::chre {

class MetricsReporter {
 public:
  /**
   * Creates an instance of MetricsReporter or returns nullptr if there was an
   * exception.
   */
  static std::unique_ptr<MetricsReporter> Create();

  MetricsReporter(const MetricsReporter &) = delete;
  MetricsReporter &operator=(const MetricsReporter &) = delete;

  /**
   * Creates and reports CHRE vendor atom and send it to stats_client.
   *
   * @param atom the vendor atom to be reported
   * @return true if the metric was successfully reported, false otherwise.
   */
  bool reportMetric(const aidl::android::frameworks::stats::VendorAtom &atom);

  /**
   * Reports an AP Wakeup caused by a nanoapp.
   *
   * @return whether the operation was successful.
   */
  bool logApWakeupOccurred(uint64_t nanoappId);

  /**
   * Reports a nanoapp load failed metric.
   *
   * @return whether the operation was successful.
   */
  bool logNanoappLoadFailed(
      uint64_t nanoappId,
      android::chre::Atoms::ChreHalNanoappLoadFailed::Type type,
      android::chre::Atoms::ChreHalNanoappLoadFailed::Reason reason);

  /**
   * Called when the binder dies for the stats service.
   */
  void onBinderDied();

 private:
  static std::shared_ptr<aidl::android::frameworks::stats::IStats>
  getStatsService(MetricsReporter &metricsReporter);

  MetricsReporter() = default;

  void setStatsService(
      std::shared_ptr<aidl::android::frameworks::stats::IStats> statsService) {
    mStatsService = statsService;
  }

  std::mutex mStatsServiceMutex;
  std::shared_ptr<aidl::android::frameworks::stats::IStats> mStatsService =
      nullptr;
};

}  // namespace android::chre

#endif  // CHRE_HOST_METRICS_REPORTER_H_