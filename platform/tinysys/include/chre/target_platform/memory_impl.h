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

#ifndef CHRE_PLATFORM_TINYSYS_MEMORY_H_
#define CHRE_PLATFORM_TINYSYS_MEMORY_H_

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

#include "encoding.h"
#include "portable.h"
#include "sensorhub/heap.h"

#ifdef __cplusplus
}  // extern "C"
#endif

namespace chre {

inline isInDram(const void *pointer) {
  return reinterpret_cast<uintptr_t>(pointer) > CFG_L1C_DRAM_ADDR;
}

inline void *memoryAlloc(size_t size) {
  void *address = heap_alloc(size);
  if (address == nullptr) {
    // Try dram if allocation from sram fails
    address = pvPortDramMalloc(size);
  }
  return address;
}

inline void memoryFree(void *pointer) {
  if (isInDram(pointer)) {
    vPortDramFree(pointer);
  } else {
    heap_free(pointer);
  }
}

}  // namespace chre

#endif  // CHRE_PLATFORM_TINYSYS_MEMORY_H_