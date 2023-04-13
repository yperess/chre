/*
 * Copyright (C) 2021 The Android Open Source Project
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

#include "clients_test.h"

#include <gtest/gtest.h>

#include <string.h>
#include <thread>

#include "chpp/app.h"
#include "chpp/clients.h"
#include "chpp/clients/gnss.h"
#include "chpp/clients/wifi.h"
#include "chpp/clients/wwan.h"
#include "chpp/macros.h"
#include "chpp/memory.h"
#include "chpp/platform/platform_link.h"
#include "chpp/platform/utils.h"
#include "chpp/services.h"
#include "chpp/time.h"
#include "chpp/transport.h"
#include "chre/pal/wwan.h"

class ClientsTest : public testing::Test {
 protected:
  void SetUp() override {
    chppClearTotalAllocBytes();

    memset(&mAppContext, 0, sizeof(mAppContext));
    memset(&mTransportContext, 0, sizeof(mTransportContext));
    memset(&mLinkContext, 0, sizeof(mLinkContext));
    mLinkContext.linkEstablished = true;

    chppTransportInit(&mTransportContext, &mAppContext, &mLinkContext,
                      getLinuxLinkApi());
    chppAppInit(&mAppContext, &mTransportContext);
    mClientState =
        (struct ChppClientState *)mAppContext.registeredClientContexts[0];
    chppClientInit(mClientState, CHPP_HANDLE_NEGOTIATED_RANGE_START);

    mTransportContext.resetState = CHPP_RESET_STATE_NONE;
  }

  void TearDown() override {
    chppAppDeinit(&mAppContext);
    chppTransportDeinit(&mTransportContext);

    EXPECT_EQ(chppGetTotalAllocBytes(), 0);
  }

  struct ChppTransportState mTransportContext;
  struct ChppAppState mAppContext;
  struct ChppLinuxLinkState mLinkContext;
  struct ChppClientState *mClientState;
  struct ChppRequestResponseState mRRState;
};

void validateClientStateAndRRState(struct ChppClientState *clientState,
                                   const struct ChppAppHeader *header) {
  ASSERT_NE(clientState, nullptr);
  const uint8_t clientIdx = clientState->index;

  ASSERT_NE(clientState->appContext, nullptr);
  ASSERT_NE(clientState->appContext->registeredClients, nullptr);
  ASSERT_NE(clientState->appContext->registeredClients[clientIdx], nullptr);
  ASSERT_NE(
      clientState->appContext->registeredClientStates[clientIdx]->rRStates,
      nullptr);
  ASSERT_LT(
      header->command,
      clientState->appContext->registeredClients[clientIdx]->rRStateCount);
}

struct ChppRequestResponseState *getClientRRState(
    struct ChppClientState *clientState, struct ChppAppHeader *header) {
  validateClientStateAndRRState(clientState, header);

  uint8_t clientIdx = clientState->index;
  return &(clientState->appContext->registeredClientStates[clientIdx]
               ->rRStates[header->command]);
}

void validateTimeout(uint64_t timeoutTimeNs, uint64_t expectedTimeNs) {
  constexpr uint64_t kJitterNs = 10 * CHPP_NSEC_PER_MSEC;

  if (expectedTimeNs == CHPP_TIME_MAX) {
    EXPECT_EQ(timeoutTimeNs, expectedTimeNs);
  } else {
    EXPECT_GE(timeoutTimeNs, expectedTimeNs);
    EXPECT_LE(timeoutTimeNs, expectedTimeNs + kJitterNs);
  }
}

void registerAndValidateRequestForTimeout(struct ChppClientState *clientState,
                                          struct ChppAppHeader *header,
                                          uint64_t kTimeoutNs,
                                          uint64_t expectedTimeNs) {
  struct ChppRequestResponseState *rRState =
      getClientRRState(clientState, header);
  chppClientTimestampRequest(clientState, rRState, header, kTimeoutNs);

  validateTimeout(clientState->appContext->nextRequestTimeoutNs,
                  expectedTimeNs);
}

void registerAndValidateResponseForTimeout(struct ChppClientState *clientState,
                                           struct ChppAppHeader *header,
                                           uint64_t expectedTimeNs) {
  struct ChppRequestResponseState *rRState =
      getClientRRState(clientState, header);
  chppClientTimestampResponse(clientState, rRState, header);

  validateTimeout(clientState->appContext->nextRequestTimeoutNs,
                  expectedTimeNs);
}

void validateTimeoutResponse(const struct ChppAppHeader *request,
                             const struct ChppAppHeader *response) {
  ASSERT_NE(request, nullptr);
  ASSERT_NE(response, nullptr);

  EXPECT_EQ(response->handle, request->handle);
  EXPECT_EQ(response->type, CHPP_MESSAGE_TYPE_SERVICE_RESPONSE);
  EXPECT_EQ(response->transaction, request->transaction);
  EXPECT_EQ(response->error, CHPP_APP_ERROR_TIMEOUT);
  EXPECT_EQ(response->command, request->command);
}

// Simulates a request from a client and a response from the service.
// There should be no error as the timeout is infinite.
TEST_F(ClientsTest, RequestResponseTimestampValid) {
  struct ChppAppHeader *reqHeader =
      chppAllocClientRequestCommand(mClientState, 0 /* command */);
  chppClientTimestampRequest(mClientState, &mRRState, reqHeader,
                             CHPP_CLIENT_REQUEST_TIMEOUT_INFINITE);

  struct ChppAppHeader *respHeader =
      chppAllocServiceResponse(reqHeader, sizeof(*reqHeader));
  ASSERT_TRUE(chppClientTimestampResponse(mClientState, &mRRState, respHeader));

  chppFree(reqHeader);
  chppFree(respHeader);
}

// Simulates a single request from the client with 2 responses.
TEST_F(ClientsTest, RequestResponseTimestampDuplicate) {
  struct ChppAppHeader *reqHeader =
      chppAllocClientRequestCommand(mClientState, 0 /* command */);
  chppClientTimestampRequest(mClientState, &mRRState, reqHeader,
                             CHPP_CLIENT_REQUEST_TIMEOUT_INFINITE);

  struct ChppAppHeader *respHeader =
      chppAllocServiceResponse(reqHeader, sizeof(*reqHeader));

  // The first response has no error.
  ASSERT_TRUE(chppClientTimestampResponse(mClientState, &mRRState, respHeader));

  // The second response errors as one response has already been received.
  ASSERT_FALSE(
      chppClientTimestampResponse(mClientState, &mRRState, respHeader));

  chppFree(reqHeader);
  chppFree(respHeader);
}

// Simulates a response to a request that has not been timestamped.
TEST_F(ClientsTest, RequestResponseTimestampInvalidId) {
  struct ChppAppHeader *reqHeader =
      chppAllocClientRequestCommand(mClientState, 0 /* command */);
  chppClientTimestampRequest(mClientState, &mRRState, reqHeader,
                             CHPP_CLIENT_REQUEST_TIMEOUT_INFINITE);

  struct ChppAppHeader *newReqHeader =
      chppAllocClientRequestCommand(mClientState, 0 /* command */);

  // We expect a response for req but get a response for newReq.
  // That is an error (the transaction does not match).
  struct ChppAppHeader *respHeader =
      chppAllocServiceResponse(newReqHeader, sizeof(*reqHeader));
  ASSERT_FALSE(
      chppClientTimestampResponse(mClientState, &mRRState, respHeader));

  chppFree(reqHeader);
  chppFree(newReqHeader);
  chppFree(respHeader);
}

// Make sure the request does not timeout right away.
TEST_F(ClientsTest, RequestTimeoutAddRemoveSingle) {
  EXPECT_EQ(mAppContext.nextRequestTimeoutNs, CHPP_TIME_MAX);

  struct ChppAppHeader *reqHeader =
      chppAllocClientRequestCommand(mClientState, 1 /* command */);

  const uint64_t timeNs = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeoutNs = 1000 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader, kTimeoutNs,
                                       timeNs + kTimeoutNs);

  // Timeout is not expired yet.
  EXPECT_EQ(chppTransportGetClientRequestTimeoutResponse(&mTransportContext),
            nullptr);

  registerAndValidateResponseForTimeout(mClientState, reqHeader, CHPP_TIME_MAX);

  chppFree(reqHeader);
}

TEST_F(ClientsTest, RequestTimeoutAddRemoveMultiple) {
  struct ChppAppHeader *reqHeader1 =
      chppAllocClientRequestCommand(mClientState, 0 /* command */);
  struct ChppAppHeader *reqHeader2 =
      chppAllocClientRequestCommand(mClientState, 1 /* command */);
  struct ChppAppHeader *reqHeader3 =
      chppAllocClientRequestCommand(mClientState, 2 /* command */);

  EXPECT_EQ(mAppContext.nextRequestTimeoutNs, CHPP_TIME_MAX);

  // kTimeout1Ns is the smallest so it will be the first timeout to expire
  // for all the requests.
  const uint64_t time1Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout1Ns = 2000 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader1, kTimeout1Ns,
                                       time1Ns + kTimeout1Ns);

  const uint64_t time2Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout2Ns = 4000 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader2, kTimeout2Ns,
                                       time1Ns + kTimeout1Ns);

  const uint64_t time3Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout3Ns = 3000 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader3, kTimeout3Ns,
                                       time1Ns + kTimeout1Ns);

  registerAndValidateResponseForTimeout(mClientState, reqHeader1,
                                        time3Ns + kTimeout3Ns);

  // Timeout is not expired yet.
  EXPECT_EQ(chppTransportGetClientRequestTimeoutResponse(&mTransportContext),
            nullptr);

  // kTimeout4Ns is now the smallest timeout.
  const uint64_t time4Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout4Ns = 1000 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader1, kTimeout4Ns,
                                       time4Ns + kTimeout4Ns);

  registerAndValidateResponseForTimeout(mClientState, reqHeader1,
                                        time3Ns + kTimeout3Ns);

  registerAndValidateResponseForTimeout(mClientState, reqHeader3,
                                        time2Ns + kTimeout2Ns);

  registerAndValidateResponseForTimeout(mClientState, reqHeader2,
                                        CHPP_TIME_MAX);

  EXPECT_EQ(chppTransportGetClientRequestTimeoutResponse(&mTransportContext),
            nullptr);

  chppFree(reqHeader1);
  chppFree(reqHeader2);
  chppFree(reqHeader3);
}

TEST_F(ClientsTest, DuplicateRequestTimeoutResponse) {
  EXPECT_EQ(mAppContext.nextRequestTimeoutNs, CHPP_TIME_MAX);

  struct ChppAppHeader *reqHeader =
      chppAllocClientRequestCommand(mClientState, 1 /* command */);

  const uint64_t time1Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout1Ns = 200 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader, kTimeout1Ns,
                                       time1Ns + kTimeout1Ns);

  std::this_thread::sleep_for(std::chrono::nanoseconds(kTimeout1Ns / 2));

  const uint64_t time2Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout2Ns = 200 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader, kTimeout2Ns,
                                       time2Ns + kTimeout2Ns);

  std::this_thread::sleep_for(
      std::chrono::nanoseconds(kTimeout1Ns + time1Ns - chppGetCurrentTimeNs()));
  // First request would have timed out but superseded by second request
  ASSERT_GT(mAppContext.nextRequestTimeoutNs, chppGetCurrentTimeNs());

  std::this_thread::sleep_for(
      std::chrono::nanoseconds(kTimeout2Ns + time2Ns - chppGetCurrentTimeNs()));
  // Second request should have timed out - so we get a response.
  ASSERT_LT(mAppContext.nextRequestTimeoutNs, chppGetCurrentTimeNs());

  struct ChppAppHeader *response =
      chppTransportGetClientRequestTimeoutResponse(&mTransportContext);
  validateTimeoutResponse(reqHeader, response);
  chppFree(response);

  registerAndValidateResponseForTimeout(mClientState, reqHeader, CHPP_TIME_MAX);
  EXPECT_EQ(chppTransportGetClientRequestTimeoutResponse(&mTransportContext),
            nullptr);

  chppFree(reqHeader);
}

TEST_F(ClientsTest, RequestTimeoutResponse) {
  EXPECT_EQ(mAppContext.nextRequestTimeoutNs, CHPP_TIME_MAX);

  struct ChppAppHeader *reqHeader1 =
      chppAllocClientRequestCommand(mClientState, 1 /* command */);
  struct ChppAppHeader *reqHeader2 =
      chppAllocClientRequestCommand(mClientState, 2 /* command */);

  const uint64_t time1Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout1Ns = 200 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader1, kTimeout1Ns,
                                       time1Ns + kTimeout1Ns);

  std::this_thread::sleep_for(std::chrono::nanoseconds(kTimeout1Ns));
  ASSERT_LT(mAppContext.nextRequestTimeoutNs, chppGetCurrentTimeNs());

  // No response in time, we then get a timeout response.
  struct ChppAppHeader *response =
      chppTransportGetClientRequestTimeoutResponse(&mTransportContext);
  validateTimeoutResponse(reqHeader1, response);
  chppFree(response);

  registerAndValidateResponseForTimeout(mClientState, reqHeader1,
                                        CHPP_TIME_MAX);
  // No other request in timeout.
  EXPECT_EQ(chppTransportGetClientRequestTimeoutResponse(&mTransportContext),
            nullptr);

  // Simulate a new timeout and make sure we have a timeout response.
  const uint64_t time2Ns = chppGetCurrentTimeNs();
  constexpr uint64_t kTimeout2Ns = 200 * CHPP_NSEC_PER_MSEC;
  registerAndValidateRequestForTimeout(mClientState, reqHeader2, kTimeout2Ns,
                                       time2Ns + kTimeout2Ns);

  std::this_thread::sleep_for(std::chrono::nanoseconds(kTimeout2Ns));
  ASSERT_LT(mAppContext.nextRequestTimeoutNs, chppGetCurrentTimeNs());

  response = chppTransportGetClientRequestTimeoutResponse(&mTransportContext);
  validateTimeoutResponse(reqHeader2, response);
  chppFree(response);

  chppFree(reqHeader1);
  chppFree(reqHeader2);
}