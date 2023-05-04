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

#ifndef CHRE_PLATFORM_SHARED_TRACING_H_
#define CHRE_PLATFORM_SHARED_TRACING_H_

#include "pw_trace/trace.h"

#define CHRE_TRACE_INSTANT(str, ...) PW_TRACE_INSTANT(str, ##__VA_ARGS__)

#define CHRE_TRACE_START(str, ...) PW_TRACE_START(str, ##__VA_ARGS__)

#define CHRE_TRACE_END(str, ...) PW_TRACE_END(str, ##__VA_ARGS__)

#define CHRE_TRACE_INSTANT_DATA(str, ...) \
  PW_TRACE_INSTANT_DATA(str, ##__VA_ARGS__)

#define CHRE_TRACE_START_DATA(str, ...) PW_TRACE_START_DATA(str, ##__VA_ARGS__)

#define CHRE_TRACE_END_DATA(str, ...) PW_TRACE_END_DATA(str, ##__VA_ARGS__)

#endif  // CHRE_PLATFORM_SHARED_TRACING_H_
