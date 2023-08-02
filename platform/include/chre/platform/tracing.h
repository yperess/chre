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

#ifndef CHRE_PLATFORM_TRACING_H_
#define CHRE_PLATFORM_TRACING_H_

#include <cstdint>

#ifdef CHRE_TRACING_ENABLED

#include "chre/target_platform/tracing.h"

/**
 * All tracing macros to be used in CHRE
 */
#ifndef CHRE_TRACE_INSTANT
#error "CHRE_TRACE_INSTANT must be defined by chre/target_platform/tracing.h"
#endif

#ifndef CHRE_TRACE_START
#error "CHRE_TRACE_START must be defined by chre/target_platform/tracing.h"
#endif

#ifndef CHRE_TRACE_END
#error "CHRE_TRACE_END must be defined by chre/target_platform/tracing.h"
#endif

#ifndef CHRE_TRACE_INSTANT_DATA
#error \
    "CHRE_TRACE_INSTANT_DATA must be defined by chre/target_platform/tracing.h"
#endif

#ifndef CHRE_TRACE_START_DATA
#error "CHRE_TRACE_START_DATA must be defined by chre/target_platform/tracing.h"
#endif

#ifndef CHRE_TRACE_END_DATA
#error "CHRE_TRACE_END_DATA must be defined by chre/target_platform/tracing.h"
#endif

#else

#define CHRE_TRACE_INSTANT(str, ...)
#define CHRE_TRACE_START(str, ...)
#define CHRE_TRACE_END(str, ...)
#define CHRE_TRACE_INSTANT_DATA(str, ...)
#define CHRE_TRACE_START_DATA(str, ...)
#define CHRE_TRACE_END_DATA(str, ...)

#endif  // CHRE_TRACING_ENABLED

#endif  // CHRE_PLATFORM_TRACING_H_
