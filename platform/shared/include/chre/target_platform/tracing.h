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

/**
 * Traces an instantaneous event.
 *
 * @param str A string literal which describes the trace.
 * @param group <optional> A string literal to group traces together.
 * @param trace_id <optional>  A uint32_t which groups this trace with others
 *                             with the same group and trace_id.
 *                             Every trace with a trace_id must also have a
 *                             group.
 * @see https://pigweed.dev/pw_trace/#trace-macros
 */
#define CHRE_TRACE_INSTANT(str, ...) PW_TRACE_INSTANT(str, ##__VA_ARGS__)

/**
 * Used to start tracing an event, should be paired with a CHRE_TRACE_END (or
 * CHRE_TRACE_END_DATA) with the same module/label/group/trace_id.
 *
 * @param str A string literal which describes the trace.
 * @param group <optional> A string literal to group traces together.
 * @param trace_id <optional>  A uint32_t which groups this trace with others
 *                             with the same group and trace_id.
 *                             Every trace with a trace_id must also have a
 *                             group.
 * @see https://pigweed.dev/pw_trace/#trace-macros
 */
#define CHRE_TRACE_START(str, ...) PW_TRACE_START(str, ##__VA_ARGS__)

/**
 * Used to end tracing an event, should be paired with a CHRE_TRACE_START (or
 * CHRE_TRACE_START_DATA) with the same module/label/group/trace_id.
 *
 * @param str A string literal which describes the trace.
 * @param group <optional> A string literal to group traces together.
 * @param trace_id <optional>  A uint32_t which groups this trace with others
 *                             with the same group and trace_id.
 *                             Every trace with a trace_id must also have a
 *                             group.
 * @see https://pigweed.dev/pw_trace/#trace-macros
 */
#define CHRE_TRACE_END(str, ...) PW_TRACE_END(str, ##__VA_ARGS__)

// TODO(b/294116163): Update implementation once data trace macros are finalized
#define CHRE_TRACE_INSTANT_DATA(str, ...)

// TODO(b/294116163): Update implementation once data trace macros are finalized
#define CHRE_TRACE_START_DATA(str, ...)

// TODO(b/294116163): Update implementation once data trace macros are finalized
#define CHRE_TRACE_END_DATA(str, ...)

#endif  // CHRE_PLATFORM_SHARED_TRACING_H_
