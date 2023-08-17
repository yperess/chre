/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "chpp/services.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "chpp/app.h"
#include "chpp/log.h"
#include "chpp/macros.h"
#include "chpp/memory.h"
#include "chpp/mutex.h"
#ifdef CHPP_SERVICE_ENABLED_GNSS
#include "chpp/services/gnss.h"
#endif
#ifdef CHPP_SERVICE_ENABLED_WIFI
#include "chpp/services/wifi.h"
#endif
#ifdef CHPP_SERVICE_ENABLED_WWAN
#include "chpp/services/wwan.h"
#endif
#include "chpp/transport.h"

/************************************************
 *  Public Functions
 ***********************************************/

void chppRegisterCommonServices(struct ChppAppState *context) {
  CHPP_DEBUG_NOT_NULL(context);
  UNUSED_VAR(context);

#ifdef CHPP_SERVICE_ENABLED_WWAN
  if (context->clientServiceSet.wwanService) {
    chppRegisterWwanService(context);
  }
#endif

#ifdef CHPP_SERVICE_ENABLED_WIFI
  if (context->clientServiceSet.wifiService) {
    chppRegisterWifiService(context);
  }
#endif

#ifdef CHPP_SERVICE_ENABLED_GNSS
  if (context->clientServiceSet.gnssService) {
    chppRegisterGnssService(context);
  }
#endif
}

void chppDeregisterCommonServices(struct ChppAppState *context) {
  CHPP_DEBUG_NOT_NULL(context);
  UNUSED_VAR(context);

#ifdef CHPP_SERVICE_ENABLED_WWAN
  if (context->clientServiceSet.wwanService) {
    chppDeregisterWwanService(context);
  }
#endif

#ifdef CHPP_SERVICE_ENABLED_WIFI
  if (context->clientServiceSet.wifiService) {
    chppDeregisterWifiService(context);
  }
#endif

#ifdef CHPP_SERVICE_ENABLED_GNSS
  if (context->clientServiceSet.gnssService) {
    chppDeregisterGnssService(context);
  }
#endif
}

void chppRegisterService(struct ChppAppState *appContext, void *serviceContext,
                         struct ChppServiceState *serviceState,
                         struct ChppOutgoingRequestState *outReqStates,
                         const struct ChppService *newService) {
  CHPP_DEBUG_NOT_NULL(appContext);
  CHPP_DEBUG_NOT_NULL(serviceContext);
  CHPP_DEBUG_NOT_NULL(serviceState);
  CHPP_DEBUG_NOT_NULL(newService);

  const uint8_t numServices = appContext->registeredServiceCount;

  serviceState->openState = CHPP_OPEN_STATE_CLOSED;
  serviceState->appContext = appContext;
  serviceState->outReqStates = outReqStates;

  if (numServices >= CHPP_MAX_REGISTERED_SERVICES) {
    CHPP_LOGE("Max services registered: # %" PRIu8, numServices);
    serviceState->handle = CHPP_HANDLE_NONE;
    return;
  }

  serviceState->handle = CHPP_SERVICE_HANDLE_OF_INDEX(numServices);

  appContext->registeredServices[numServices] = newService;
  appContext->registeredServiceStates[numServices] = serviceState;
  appContext->registeredServiceContexts[numServices] = serviceContext;
  appContext->registeredServiceCount++;

  chppMutexInit(&serviceState->syncResponse.mutex);
  chppConditionVariableInit(&serviceState->syncResponse.condVar);

  char uuidText[CHPP_SERVICE_UUID_STRING_LEN];
  chppUuidToStr(newService->descriptor.uuid, uuidText);
  CHPP_LOGD("Registered service # %" PRIu8
            " on handle %d"
            " with name=%s, UUID=%s, version=%" PRIu8 ".%" PRIu8 ".%" PRIu16
            ", min_len=%" PRIuSIZE " ",
            numServices, serviceState->handle, newService->descriptor.name,
            uuidText, newService->descriptor.version.major,
            newService->descriptor.version.minor,
            newService->descriptor.version.patch, newService->minLength);
}

struct ChppAppHeader *chppAllocServiceNotification(size_t len) {
  return chppAllocNotification(CHPP_MESSAGE_TYPE_SERVICE_NOTIFICATION, len);
}

struct ChppAppHeader *chppAllocServiceRequest(
    struct ChppServiceState *serviceState, size_t len) {
  CHPP_DEBUG_NOT_NULL(serviceState);
  return chppAllocRequest(CHPP_MESSAGE_TYPE_SERVICE_REQUEST,
                          serviceState->handle, &serviceState->transaction,
                          len);
}

struct ChppAppHeader *chppAllocServiceRequestCommand(
    struct ChppServiceState *serviceState, uint16_t command) {
  struct ChppAppHeader *request =
      chppAllocServiceRequest(serviceState, sizeof(struct ChppAppHeader));

  if (request != NULL) {
    request->command = command;
  }
  return request;
}

bool chppServiceSendTimestampedRequestOrFail(
    struct ChppServiceState *serviceState,
    struct ChppOutgoingRequestState *outReqState, void *buf, size_t len,
    uint64_t timeoutNs) {
  return chppSendTimestampedRequestOrFail(serviceState->appContext,
                                          &serviceState->syncResponse,
                                          outReqState, buf, len, timeoutNs);
}

bool chppServiceSendTimestampedRequestAndWait(
    struct ChppServiceState *serviceState,
    struct ChppOutgoingRequestState *outReqState, void *buf, size_t len) {
  return chppServiceSendTimestampedRequestAndWaitTimeout(
      serviceState, outReqState, buf, len, CHPP_REQUEST_TIMEOUT_DEFAULT);
}

bool chppServiceSendTimestampedRequestAndWaitTimeout(
    struct ChppServiceState *serviceState,
    struct ChppOutgoingRequestState *outReqState, void *buf, size_t len,
    uint64_t timeoutNs) {
  CHPP_DEBUG_NOT_NULL(serviceState);

  bool result = chppServiceSendTimestampedRequestOrFail(
      serviceState, outReqState, buf, len, CHPP_REQUEST_TIMEOUT_INFINITE);

  if (!result) {
    return false;
  }

  return chppWaitForResponseWithTimeout(&serviceState->syncResponse,
                                        outReqState, timeoutNs);
}

void chppServiceRecalculateNextTimeout(struct ChppAppState *context) {
  CHPP_DEBUG_NOT_NULL(context);

  context->nextServiceRequestTimeoutNs = CHPP_TIME_MAX;

  for (uint8_t serviceIdx = 0; serviceIdx < context->registeredServiceCount;
       serviceIdx++) {
    uint16_t reqCount = context->registeredServices[serviceIdx]->outReqCount;
    struct ChppOutgoingRequestState *reqStates =
        context->registeredServiceStates[serviceIdx]->outReqStates;
    for (uint16_t cmdIdx = 0; cmdIdx < reqCount; cmdIdx++) {
      struct ChppOutgoingRequestState *reqState = &reqStates[cmdIdx];

      if (reqState->requestState == CHPP_REQUEST_STATE_REQUEST_SENT) {
        context->nextServiceRequestTimeoutNs =
            MIN(context->nextServiceRequestTimeoutNs, reqState->responseTimeNs);
      }
    }
  }

  CHPP_LOGD("nextReqTimeout=%" PRIu64,
            context->nextServiceRequestTimeoutNs / CHPP_NSEC_PER_MSEC);
}

void chppServiceCloseOpenRequests(struct ChppServiceState *serviceState,
                                  const struct ChppService *service,
                                  bool clearOnly) {
  CHPP_DEBUG_NOT_NULL(serviceState);
  CHPP_DEBUG_NOT_NULL(service);

  bool recalcNeeded = false;

  for (uint16_t cmdIdx = 0; cmdIdx < service->outReqCount; cmdIdx++) {
    if (serviceState->outReqStates[cmdIdx].requestState ==
        CHPP_REQUEST_STATE_REQUEST_SENT) {
      recalcNeeded = true;

      CHPP_LOGE("Closing open req #%" PRIu16 " clear %d", cmdIdx, clearOnly);

      if (clearOnly) {
        serviceState->outReqStates[cmdIdx].requestState =
            CHPP_REQUEST_STATE_RESPONSE_TIMEOUT;
      } else {
        struct ChppAppHeader *response =
            chppMalloc(sizeof(struct ChppAppHeader));
        if (response == NULL) {
          CHPP_LOG_OOM();
        } else {
          // Simulate receiving a timeout response.
          response->handle = serviceState->handle;
          response->type = CHPP_MESSAGE_TYPE_CLIENT_RESPONSE;
          response->transaction =
              serviceState->outReqStates[cmdIdx].transaction;
          response->error = CHPP_APP_ERROR_TIMEOUT;
          response->command = cmdIdx;

          chppAppProcessRxDatagram(serviceState->appContext,
                                   (uint8_t *)response,
                                   sizeof(struct ChppAppHeader));
        }
      }
    }
  }
  if (recalcNeeded) {
    chppServiceRecalculateNextTimeout(serviceState->appContext);
  }
}