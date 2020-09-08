/*
 * Copyright (C) 2016 The Android Open Source Project
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

#ifndef CHRE_UTIL_ENTRY_POINTS_H_
#define CHRE_UTIL_ENTRY_POINTS_H_

#include <stdbool.h>
#include <stdint.h>

//! @see nanoappStart()
typedef bool(chreNanoappStartFunction)();

//! @see nanoappHandleEvent()
typedef void(chreNanoappHandleEventFunction)(uint32_t senderInstanceId,
                                             uint16_t eventType,
                                             const void *eventData);

//! @see nanoappEnd()
typedef void(chreNanoappEndFunction)();

#endif  // CHRE_UTIL_ENTRY_POINTS_H_
