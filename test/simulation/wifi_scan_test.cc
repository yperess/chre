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

#include <cstdint>

#include "chre/core/event_loop_manager.h"
#include "chre/core/settings.h"
#include "chre/platform/linux/pal_nan.h"
#include "chre/platform/linux/pal_wifi.h"
#include "chre/platform/log.h"
#include "chre/util/system/napp_permissions.h"
#include "chre_api/chre/event.h"
#include "chre_api/chre/wifi.h"
#include "gtest/gtest.h"
#include "test_base.h"
#include "test_event.h"
#include "test_event_queue.h"
#include "test_util.h"

namespace chre {
namespace {

CREATE_CHRE_TEST_EVENT(SCAN_REQUEST, 20);

struct WifiAsyncData {
  const uint32_t *cookie;
  chreError errorCode;
};

constexpr uint64_t kAppOneId = 0x0123456789000001;
constexpr uint64_t kAppTwoId = 0x0123456789000002;

class WifiScanRequestQueueTestBase : public TestBase {
 public:
  void SetUp() {
    TestBase::SetUp();
    // Add delay to make sure the requests are queued.
    chrePalWifiDelayResponse(PalWifiAsyncRequestTypes::SCAN,
                             std::chrono::seconds(1));
  }

  void TearDown() {
    TestBase::TearDown();
    chrePalWifiDelayResponse(PalWifiAsyncRequestTypes::SCAN,
                             std::chrono::seconds(0));
  }
};

class WifiScanTestNanoapp : public TestNanoapp {
 public:
  WifiScanTestNanoapp()
      : TestNanoapp(
            TestNanoappInfo{.perms = NanoappPermissions::CHRE_PERMS_WIFI}) {}

  explicit WifiScanTestNanoapp(uint64_t id)
      : TestNanoapp(TestNanoappInfo{
            .perms = NanoappPermissions::CHRE_PERMS_WIFI, .id = id}) {}

  void handleEvent(uint32_t, uint16_t eventType,
                   const void *eventData) override {
    switch (eventType) {
      case CHRE_EVENT_WIFI_ASYNC_RESULT: {
        auto *event = static_cast<const chreAsyncResult *>(eventData);
        TestEventQueueSingleton::get()->pushEvent(
            CHRE_EVENT_WIFI_ASYNC_RESULT,
            WifiAsyncData{
                .cookie = static_cast<const uint32_t *>(event->cookie),
                .errorCode = static_cast<chreError>(event->errorCode)});
        break;
      }

      case CHRE_EVENT_WIFI_SCAN_RESULT: {
        TestEventQueueSingleton::get()->pushEvent(CHRE_EVENT_WIFI_SCAN_RESULT);
        break;
      }

      case CHRE_EVENT_TEST_EVENT: {
        auto event = static_cast<const TestEvent *>(eventData);
        switch (event->type) {
          case SCAN_REQUEST:
            bool success = false;
            if (mNextFreeCookieIndex < kMaxPendingCookie) {
              mCookies[mNextFreeCookieIndex] =
                  *static_cast<uint32_t *>(event->data);
              success = chreWifiRequestScanAsyncDefault(
                  &mCookies[mNextFreeCookieIndex]);
              mNextFreeCookieIndex++;
            } else {
              LOGE("Too many cookies passed from test body!");
            }
            TestEventQueueSingleton::get()->pushEvent(SCAN_REQUEST, success);
        }
      }
    }
  }

 protected:
  static constexpr uint8_t kMaxPendingCookie = 10;
  uint32_t mCookies[kMaxPendingCookie];
  uint8_t mNextFreeCookieIndex = 0;
};

TEST_F(TestBase, WifiScanBasicSettingTest) {
  uint64_t appId = loadNanoapp(MakeUnique<WifiScanTestNanoapp>());

  EventLoopManagerSingleton::get()->getSettingManager().postSettingChange(
      Setting::WIFI_AVAILABLE, true /* enabled */);

  constexpr uint32_t firstCookie = 0x1010;
  bool success;
  WifiAsyncData wifiAsyncData;

  sendEventToNanoapp(appId, SCAN_REQUEST, firstCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);

  waitForEvent(CHRE_EVENT_WIFI_ASYNC_RESULT, &wifiAsyncData);
  EXPECT_EQ(wifiAsyncData.errorCode, CHRE_ERROR_NONE);
  EXPECT_EQ(*wifiAsyncData.cookie, firstCookie);
  waitForEvent(CHRE_EVENT_WIFI_SCAN_RESULT);

  EventLoopManagerSingleton::get()->getSettingManager().postSettingChange(
      Setting::WIFI_AVAILABLE, false /* enabled */);

  constexpr uint32_t secondCookie = 0x2020;
  sendEventToNanoapp(appId, SCAN_REQUEST, secondCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);

  waitForEvent(CHRE_EVENT_WIFI_ASYNC_RESULT, &wifiAsyncData);
  EXPECT_EQ(wifiAsyncData.errorCode, CHRE_ERROR_FUNCTION_DISABLED);
  EXPECT_EQ(*wifiAsyncData.cookie, secondCookie);

  EventLoopManagerSingleton::get()->getSettingManager().postSettingChange(
      Setting::WIFI_AVAILABLE, true /* enabled */);
  unloadNanoapp(appId);
}

TEST_F(WifiScanRequestQueueTestBase, WifiQueuedScanSettingChangeTest) {
  uint64_t appOneId = loadNanoapp(MakeUnique<WifiScanTestNanoapp>(kAppOneId));
  uint64_t appTwoId = loadNanoapp(MakeUnique<WifiScanTestNanoapp>(kAppTwoId));

  constexpr uint32_t firstRequestCookie = 0x1010;
  constexpr uint32_t secondRequestCookie = 0x2020;
  bool success;
  sendEventToNanoapp(appOneId, SCAN_REQUEST, firstRequestCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);
  sendEventToNanoapp(appTwoId, SCAN_REQUEST, secondRequestCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);

  EventLoopManagerSingleton::get()->getSettingManager().postSettingChange(
      Setting::WIFI_AVAILABLE, false /* enabled */);

  WifiAsyncData wifiAsyncData;
  waitForEvent(CHRE_EVENT_WIFI_ASYNC_RESULT, &wifiAsyncData);
  EXPECT_EQ(wifiAsyncData.errorCode, CHRE_ERROR_NONE);
  EXPECT_EQ(*wifiAsyncData.cookie, firstRequestCookie);
  waitForEvent(CHRE_EVENT_WIFI_SCAN_RESULT);

  waitForEvent(CHRE_EVENT_WIFI_ASYNC_RESULT, &wifiAsyncData);
  EXPECT_EQ(wifiAsyncData.errorCode, CHRE_ERROR_FUNCTION_DISABLED);
  EXPECT_EQ(*wifiAsyncData.cookie, secondRequestCookie);

  EventLoopManagerSingleton::get()->getSettingManager().postSettingChange(
      Setting::WIFI_AVAILABLE, true /* enabled */);

  unloadNanoapp(appOneId);
  unloadNanoapp(appTwoId);
}

TEST_F(WifiScanRequestQueueTestBase, WifiScanRejectRequestFromSameNanoapp) {
  uint64_t appId = loadNanoapp(MakeUnique<WifiScanTestNanoapp>());

  constexpr uint32_t firstRequestCookie = 0x1010;
  constexpr uint32_t secondRequestCookie = 0x2020;
  bool success;
  sendEventToNanoapp(appId, SCAN_REQUEST, firstRequestCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);
  sendEventToNanoapp(appId, SCAN_REQUEST, secondRequestCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_FALSE(success);

  WifiAsyncData wifiAsyncData;
  waitForEvent(CHRE_EVENT_WIFI_ASYNC_RESULT, &wifiAsyncData);
  EXPECT_EQ(wifiAsyncData.errorCode, CHRE_ERROR_NONE);
  EXPECT_EQ(*wifiAsyncData.cookie, firstRequestCookie);
  waitForEvent(CHRE_EVENT_WIFI_SCAN_RESULT);

  unloadNanoapp(appId);
}

TEST_F(WifiScanRequestQueueTestBase, WifiScanActiveScanFromDistinctNanoapps) {
  CREATE_CHRE_TEST_EVENT(CONCURRENT_NANOAPP_RECEIVED_EXPECTED_ASYNC_EVENT_COUNT,
                         1);
  CREATE_CHRE_TEST_EVENT(CONCURRENT_NANOAPP_READ_COOKIE, 2);

  constexpr uint8_t kExpectedReceiveAsyncResultCount = 2;
  // receivedCookieCount is shared across apps and must be static.
  // But we want it initialized each time the test is executed.
  static uint8_t receivedCookieCount;
  receivedCookieCount = 0;

  class WifiScanTestConcurrentNanoapp : public TestNanoapp {
   public:
    explicit WifiScanTestConcurrentNanoapp(uint64_t id)
        : TestNanoapp(TestNanoappInfo{
              .perms = NanoappPermissions::CHRE_PERMS_WIFI, .id = id}) {}

    void handleEvent(uint32_t, uint16_t eventType,
                     const void *eventData) override {
      switch (eventType) {
        case CHRE_EVENT_WIFI_ASYNC_RESULT: {
          auto *event = static_cast<const chreAsyncResult *>(eventData);
          if (event->errorCode == CHRE_ERROR_NONE) {
            mReceivedCookie = *static_cast<const uint32_t *>(event->cookie);
            ++receivedCookieCount;
          } else {
            LOGE("Received failed async result");
          }

          if (receivedCookieCount == kExpectedReceiveAsyncResultCount) {
            TestEventQueueSingleton::get()->pushEvent(
                CONCURRENT_NANOAPP_RECEIVED_EXPECTED_ASYNC_EVENT_COUNT);
          }
          break;
        }

        case CHRE_EVENT_TEST_EVENT: {
          auto event = static_cast<const TestEvent *>(eventData);
          bool success = false;
          uint32_t expectedCookie;
          switch (event->type) {
            case SCAN_REQUEST:
              mSentCookie = *static_cast<uint32_t *>(event->data);
              success = chreWifiRequestScanAsyncDefault(&(mSentCookie));
              TestEventQueueSingleton::get()->pushEvent(SCAN_REQUEST, success);
              break;
            case CONCURRENT_NANOAPP_READ_COOKIE:
              TestEventQueueSingleton::get()->pushEvent(
                  CONCURRENT_NANOAPP_READ_COOKIE, mReceivedCookie);
              break;
          }
        }
      }
    }

   protected:
    uint32_t mSentCookie;
    uint32_t mReceivedCookie;
  };

  uint64_t appOneId =
      loadNanoapp(MakeUnique<WifiScanTestConcurrentNanoapp>(kAppOneId));
  uint64_t appTwoId =
      loadNanoapp(MakeUnique<WifiScanTestConcurrentNanoapp>(kAppTwoId));

  constexpr uint32_t kAppOneRequestCookie = 0x1010;
  constexpr uint32_t kAppTwoRequestCookie = 0x2020;
  bool success;
  sendEventToNanoapp(appOneId, SCAN_REQUEST, kAppOneRequestCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);
  sendEventToNanoapp(appTwoId, SCAN_REQUEST, kAppTwoRequestCookie);
  waitForEvent(SCAN_REQUEST, &success);
  EXPECT_TRUE(success);

  waitForEvent(CONCURRENT_NANOAPP_RECEIVED_EXPECTED_ASYNC_EVENT_COUNT);

  uint32_t receivedCookie;
  sendEventToNanoapp(appOneId, CONCURRENT_NANOAPP_READ_COOKIE);
  waitForEvent(CONCURRENT_NANOAPP_READ_COOKIE, &receivedCookie);
  EXPECT_EQ(kAppOneRequestCookie, receivedCookie);

  sendEventToNanoapp(appTwoId, CONCURRENT_NANOAPP_READ_COOKIE);
  waitForEvent(CONCURRENT_NANOAPP_READ_COOKIE, &receivedCookie);
  EXPECT_EQ(kAppTwoRequestCookie, receivedCookie);

  unloadNanoapp(appOneId);
  unloadNanoapp(appTwoId);
}

}  // namespace
}  // namespace chre