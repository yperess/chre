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

#include "chre_host/bt_snoop_log_parser.h"

#include <endian.h>
#include <string.h>
#include <sys/stat.h>
#include <filesystem>
#include <fstream>

#include "chre/util/time.h"
#include "chre_host/daemon_base.h"
#include "chre_host/file_stream.h"
#include "chre_host/log.h"
#include "chre_host/log_message_parser.h"

namespace android {
namespace chre {

namespace {

// Some code in this script are copied from the BT HAL snoop log implementation
// in packages/modules/Bluetooth/system/gd/hal. We didn't share the code
// directly because currently we only need a subset of the entire snoop log
// functionalities.
using HciPacket = std::vector<uint8_t>;

std::string kSnoopLogFilePath = "/data/vendor/chre/chre_btsnoop_hci.log";

constexpr size_t kDefaultBtSnoopMaxPacketsPerFile = 0xffff;

const size_t PACKET_TYPE_LENGTH = 1;

constexpr uint32_t kBytesToTest = 0x12345678;
constexpr uint8_t kFirstByte = (const uint8_t &)kBytesToTest;
constexpr bool isLittleEndian = kFirstByte == 0x78;
constexpr uint32_t BTSNOOP_VERSION_NUMBER = isLittleEndian ? 0x01000000 : 1;
constexpr uint32_t BTSNOOP_DATALINK_TYPE =
    isLittleEndian ? 0xea030000
                   : 0x03ea;  // Datalink Type code for HCI UART (H4) is 1002

// Epoch in microseconds since 01/01/0000.
constexpr uint64_t kBtSnoopEpochDelta = 0x00dcddb30f2f8000ULL;

struct FileHeaderType {
  uint8_t identification_pattern[8];
  uint32_t version_number;
  uint32_t datalink_type;
} __attribute__((packed));

static constexpr FileHeaderType kBtSnoopFileHeader = {
    .identification_pattern = {'b', 't', 's', 'n', 'o', 'o', 'p', 0x00},
    .version_number = BTSNOOP_VERSION_NUMBER,
    .datalink_type = BTSNOOP_DATALINK_TYPE};

uint64_t htonll(uint64_t ll) {
  if constexpr (isLittleEndian) {
    return static_cast<uint64_t>(htonl(ll & 0xffffffff)) << 32 |
           htonl(ll >> 32);
  } else {
    return ll;
  }
}

}  // namespace

void BtSnoopLogParser::init() {
  openSnoopLogFile();
}

size_t BtSnoopLogParser::log(const char *buffer) {
  const auto *message = reinterpret_cast<const BtSnoopLog *>(buffer);
  capture(message->packet, static_cast<size_t>(message->packetSize),
          static_cast<BtSnoopDirection>(message->direction));
  return message->packetSize + 2 * sizeof(uint8_t);
}

void BtSnoopLogParser::capture(const uint8_t *packet, size_t packetSize,
                               BtSnoopDirection direction) {
  uint64_t timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();
  std::bitset<32> flags = 0;
  PacketType type = PacketType::CMD;
  switch (direction) {
    case BtSnoopDirection::OUTGOING_TO_ARBITER:
      flags.set(0, false);
      flags.set(1, true);
      type = PacketType::CMD;
      break;
    case BtSnoopDirection::INCOMING_FROM_BT_CONTROLLER:
      flags.set(0, true);
      flags.set(1, true);
      type = PacketType::EVT;
      break;
  }

  uint32_t length = packetSize + /* type byte */ PACKET_TYPE_LENGTH;
  PacketHeaderType header = {
      .length_original = htonl(length),
      .length_captured = htonl(length),
      .flags = htonl(static_cast<uint32_t>(flags.to_ulong())),
      .dropped_packets = 0,
      .timestamp = htonll(timestamp + kBtSnoopEpochDelta),
      .type = type};

  if (length != ntohl(header.length_original)) {
    header.length_captured = htonl(length);
  }

  mode_t prevmask = umask(0);
  umask(prevmask);
  mPacketCounter++;
  if (mPacketCounter > kDefaultBtSnoopMaxPacketsPerFile) {
    closeSnoopLogFile();
    LOGW("Snoop Log file reached maximum size");
  } else {
    if (!mBtSnoopOstream.write(reinterpret_cast<const char *>(&header),
                               sizeof(PacketHeaderType))) {
      LOGE("Failed to write packet header for btsnoop, error: \"%s\"",
           strerror(errno));
    }
    if (!mBtSnoopOstream.write(reinterpret_cast<const char *>(packet),
                               length - 1)) {
      LOGE("Failed to write packet payload for btsnoop, error: \"%s\"",
           strerror(errno));
    }
  }

  if (!mBtSnoopOstream.flush()) {
    LOGE("Failed to flush, error: \"%s\"", strerror(errno));
  }
}

void BtSnoopLogParser::openSnoopLogFile() {
  mode_t prevMask = umask(0);
  umask(prevMask);

  mBtSnoopOstream.open(kSnoopLogFilePath, std::ios::binary | std::ios::out);
  if (!mBtSnoopOstream.write(
          reinterpret_cast<const char *>(&kBtSnoopFileHeader),
          sizeof(FileHeaderType))) {
    LOGE("Unable to write file header to \"%s\", error: \"%s\"",
         kSnoopLogFilePath.c_str(), strerror(errno));
  }
  if (!mBtSnoopOstream.flush()) {
    LOGE("File header failed to flush, error: \"%s\"", strerror(errno));
  }
}

void BtSnoopLogParser::closeSnoopLogFile() {
  if (mBtSnoopOstream.is_open()) {
    mBtSnoopOstream.flush();
    mBtSnoopOstream.close();
  }
  mPacketCounter = 0;
}

}  // namespace chre
}  // namespace android
