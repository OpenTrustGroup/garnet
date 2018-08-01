// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_data.h"

namespace wlan {
namespace test_data {

std::vector<uint8_t> kBeaconFrame = {
    // clang-format off
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71,
    0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71, 0x10, 0xfe,
    0x89, 0xe0, 0x31, 0xcd, 0x16, 0x00, 0x00, 0x00,
    0x64, 0x00, 0x01, 0x11, 0x00, 0x0b, 0x47, 0x6f,
    0x6f, 0x67, 0x6c, 0x65, 0x47, 0x75, 0x65, 0x73,
    0x74, 0x01, 0x03, 0xc8, 0x60, 0x6c, 0x03, 0x01,
    0x30, 0x05, 0x04, 0x00, 0x01, 0x00, 0x00, 0x07,
    0x10, 0x55, 0x53, 0x20, 0x24, 0x04, 0x24, 0x34,
    0x04, 0x1e, 0x64, 0x0c, 0x1e, 0x95, 0x05, 0x24,
    0x00, 0x20, 0x01, 0x00, 0x23, 0x02, 0x09, 0x00,
    0x0b, 0x05, 0x13, 0x00, 0x09, 0xa1, 0x77, 0x2d,
    0x1a, 0xef, 0x09, 0x17, 0xff, 0xff, 0xff, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x3d, 0x16, 0x30, 0x0f, 0x04,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x7f, 0x08, 0x04, 0x00, 0x08,
    0x00, 0x00, 0x00, 0x00, 0x40, 0xbf, 0x0c, 0x91,
    0x59, 0x82, 0x0f, 0xea, 0xff, 0x00, 0x00, 0xea,
    0xff, 0x00, 0x00, 0xc0, 0x05, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xc3, 0x03, 0x01, 0x24, 0x24, 0xdd,
    0x07, 0x00, 0x0b, 0x86, 0x01, 0x04, 0x08, 0x09,
    0xdd, 0x16, 0x00, 0x0b, 0x86, 0x01, 0x03, 0x00,
    0x75, 0x73, 0x2d, 0x73, 0x65, 0x61, 0x2d, 0x77,
    0x61, 0x74, 0x2d, 0x30, 0x33, 0x2d, 0x30, 0x36,
    0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
    0x80, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4,
    0x00, 0x00, 0x42, 0x43, 0x5e, 0x00, 0x62, 0x32,
    0x2f, 0x00,
    // clang-format on
};

std::vector<uint8_t> kBlockAckUnsupportedFrame = {
    // clang-format off
    0x94, 0x00, 0x00, 0x00, 0x40, 0xe3, 0xd6, 0xbf,
    0xf1, 0x71, 0x94, 0x65, 0x2d, 0xdc, 0x01, 0x00
    // clang-format on
};

std::vector<uint8_t> kDeauthFrame = {
    // clang-format off
    0xc1, 0x20, 0x79, 0xce, 0x01, 0xc8, 0x69, 0x4e, 0x01, 0x08, 0x79,
    0x4e, 0x01, 0x00, 0xe9, 0x46, 0x09, 0x84, 0xf9, 0x4e, 0x21, 0x08,
    0x79, 0x4e, 0x85, 0xc0, 0xb9, 0xce, 0x80, 0xc8
    // clang-format on
};

std::vector<uint8_t> kDeauthFrame10BytePadding = {
    // clang-format off
    // Management header:
    0xc1, 0x20, 0x79, 0xce, 0x01, 0xc8, 0x69, 0x4e,
    0x01, 0x08, 0x79, 0x4e, 0x01, 0x00, 0xe9, 0x46,
    0x09, 0x84, 0xf9, 0x4e, 0x21, 0x08, 0x79, 0x4e,
    // 16 bytes padding:
    0xFF, 0xFF, 0xFF, 0xFF,0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF,0xFF, 0xFF, 0xFF, 0xFF,
    // Deauthentication body:
    0x85, 0xc0
    // clang-format on
};

std::vector<uint8_t> kActionFrame = {
    // clang-format off
    0xd0, 0x00, 0x28, 0x00, 0xf4, 0xf5, 0xd8, 0xcf, 0xd8, 0x3f, 0x40,
    0xe3, 0xd6, 0xbf, 0xf1, 0x71, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71,
    0xe0, 0xf9, 0x03, 0x00, 0x31, 0x1b, 0x10, 0x00, 0x00, 0xd0, 0x3e,
    // clang-format on
};

std::vector<uint8_t> kProbeRequestFrame = {
    // clang-format off
    0x40, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xb4,
    0xf1, 0xda, 0xeb, 0x3c, 0xab, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x70, 0x05, 0x00, 0x00, 0x01, 0x08, 0x0c, 0x12, 0x18, 0x24, 0x30,
    0x48, 0x60, 0x6c, 0x2d, 0x1a, 0xef, 0x01, 0x13, 0xff, 0xff, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7f, 0x09,
    0x04, 0x00, 0x0a, 0x02, 0x01, 0x00, 0x00, 0x40, 0x80, 0xbf, 0x0c,
    0xb2, 0x79, 0x91, 0x33, 0xfa, 0xff, 0x0c, 0x03, 0xfa, 0xff, 0x0c,
    0x03, 0xdd, 0x07, 0x00, 0x50, 0xf2, 0x08, 0x00, 0x23, 0x00, 0xff,
    0x03, 0x02, 0x00, 0x1c,
    // clang-format on
};

std::vector<uint8_t> kAssocReqFrame = {
    // clang-format off
    0x00, 0x00, 0x3a, 0x01, 0x08, 0x02, 0x8e, 0xe0, 0x5e, 0xe8, 0x78,
    0xf8, 0x82, 0xa4, 0x08, 0x47, 0x08, 0x02, 0x8e, 0xe0, 0x5e, 0xe8,
    0x60, 0x97, 0x31, 0x14, 0x01, 0x00, 0x00, 0x11, 0x46, 0x55, 0x43,
    0x48, 0x53, 0x49, 0x41, 0x2d, 0x54, 0x45, 0x53, 0x54, 0x2d, 0x57,
    0x50, 0x41, 0x32, 0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x12, 0x24,
    0x48, 0x6c, 0x32, 0x04, 0x0c, 0x18, 0x30, 0x60, 0x30, 0x14, 0x01,
    0x00, 0x00, 0x0f, 0xac, 0x04, 0x01, 0x00, 0x00, 0x0f, 0xac, 0x04,
    0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, 0x00, 0x00, 0x2d, 0x1a, 0xad,
    0x01, 0x1f, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xdd, 0x07, 0x00, 0x50, 0xf2, 0x02, 0x00, 0x01,
    0x00, 0x7f, 0x09, 0x04, 0x00, 0x0a, 0x02, 0x01, 0x00, 0x00, 0x00,
    0x80,
    // clang-format on
};

std::vector<uint8_t> kAssocRespFrame = {
    // clang-format off
    0x10, 0x00, 0x30, 0x01, 0x78, 0xf8, 0x82, 0xa4, 0x08, 0x47, 0x08,
    0x02, 0x8e, 0xe0, 0x5e, 0xe8, 0x08, 0x02, 0x8e, 0xe0, 0x5e, 0xe8,
    0xd0, 0xad, 0x31, 0x04, 0x00, 0x00, 0x02, 0xc0, 0x01, 0x08, 0x82,
    0x84, 0x8b, 0x96, 0x12, 0x24, 0x48, 0x6c, 0x32, 0x04, 0x0c, 0x18,
    0x30, 0x60, 0xdd, 0x18, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01, 0x00,
    0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0xa4, 0x00, 0x00, 0x42, 0x43,
    0x5e, 0x00, 0x62, 0x32, 0x2f, 0x00, 0x2d, 0x1a, 0x6e, 0x11, 0x16,
    0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x3d, 0x16, 0x0b, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xdd, 0x1e, 0x00, 0x90, 0x4c, 0x33, 0x6e, 0x11,
    0x16, 0xff, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
    0x00, 0x00, 0xdd, 0x1a, 0x00, 0x90, 0x4c, 0x34, 0x0b, 0x00, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x0e, 0x14,
    0x00, 0x0a, 0x00, 0x2c, 0x01, 0xc8, 0x00, 0x14, 0x00, 0x05, 0x00,
    0x19, 0x00, 0x7f, 0x01, 0x01, 0xdd, 0x07, 0x00, 0x0c, 0x43, 0x04,
    0x00, 0x00, 0x00,
    // clang-format on
};

std::vector<uint8_t> kAuthFrame = {
    // clang-format off
    0xb0, 0x00, 0x3a, 0x01, 0x08, 0x02, 0x8e, 0xe0, 0x5e, 0xe8, 0x78,
    0xf8, 0x82, 0xa4, 0x08, 0x47, 0x08, 0x02, 0x8e, 0xe0, 0x5e, 0xe8,
    0x50, 0x97, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    // clang-format on
};

std::vector<uint8_t> kDisassocFrame = {
    // clang-format off
    0xa0, 0xa9, 0x2f, 0x16, 0x96, 0xca, 0xf7, 0x1b, 0x67, 0x89, 0x74,
    0x95, 0x5f, 0x82, 0xfe, 0x5e, 0x81, 0x08, 0xfb, 0x98, 0x4f, 0x70,
    0x9b, 0x40, 0xe6, 0xb7, 0x65, 0xde, 0x8d, 0x1d, 0x7f, 0xb3, 0xdd,
    0x0c, 0x4e, 0xde, 0x2e, 0x80, 0x22, 0x4a, 0x3c, 0xfc, 0xa7, 0xba,
    0xa6, 0x33, 0xbf, 0xae, 0x2c, 0xc5, 0x99, 0xe3, 0xcb, 0xbe, 0x05,
    0xba, 0x6b, 0x63, 0xe1, 0x6c, 0xe0, 0xc9, 0x8d, 0xa8, 0x6f, 0x9b,
    0x5c, 0x5f, 0x17, 0x17, 0x2f, 0xc4, 0xf9, 0xfa, 0x9c, 0xc3, 0x6b,
    0xb1, 0x3d, 0x5f,
    // clang-format on
};

std::vector<uint8_t> kAddBaReqFrame = {
    // clang-format off
    0xd0, 0x00, 0x28, 0x00, 0xf4, 0xf5, 0xd8, 0xcf, 0xd8, 0x3f, 0x40,
    0xe3, 0xd6, 0xbf, 0xf1, 0x71, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71,
    0x10, 0xca, 0x03, 0x00, 0xce, 0x1f, 0x10, 0x00, 0x00, 0xf0, 0xbe,
    // clang-format on
};

std::vector<uint8_t> kAddBaReqBody = {
    // clang-format off
    0x03, 0x00, 0xce, 0x1f, 0x10, 0x00, 0x00, 0xf0, 0xbe,
    // clang-format on
};

std::vector<uint8_t> kAddBaRespFrame = {
    // clang-format off
    0xd0, 0x00, 0x28, 0x00, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71, 0xf4,
    0xf5, 0xd8, 0xcf, 0xd8, 0x3f, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71,
    0x50, 0x38, 0x03, 0x01, 0xce, 0x25, 0x00, 0x1f, 0x10, 0x00, 0x00,
    // clang-format on
};

std::vector<uint8_t> kAddBaRespBody = {
    // clang-format off
    0x03, 0x01, 0xce, 0x25, 0x00, 0x1f, 0x10, 0x00, 0x00,
    // clang-format on
};

std::vector<uint8_t> kQosDataFrame = {
    // clang-format off
    0x88, 0x42, 0x2c, 0x00, 0x78, 0x4f, 0x43, 0x94, 0x34, 0x07, 0x40,
    0xe3, 0xd6, 0xbf, 0xf1, 0x70, 0x10, 0x0e, 0x7e, 0x3a, 0x67, 0xc1,
    0x40, 0x10, 0x00, 0x00, 0x8d, 0xfe, 0x00, 0x20, 0x04, 0x00, 0x00,
    0x00, 0x52, 0x36, 0x07, 0xe5, 0x1a, 0x61, 0xde, 0xfe, 0x84, 0xd9,
    0x68, 0xde, 0xe7, 0xac, 0x15, 0xd4, 0x6f, 0xdd, 0xc7, 0x60, 0xeb,
    0xbd, 0xa7, 0x36, 0x03, 0x21, 0x96, 0x1c, 0x5b, 0x30, 0x48, 0x9a,
    0x1b, 0x27, 0xed, 0xe5, 0xcb, 0xde, 0x00, 0x35, 0x32, 0x1c, 0x62,
    0xa1, 0xb2, 0x52, 0xd4, 0x04, 0x5d, 0xc7, 0xff, 0xc3, 0x9b, 0x15,
    0x32, 0x6a, 0xc4, 0x94, 0x1e, 0x45, 0xc3, 0xe0, 0x3a, 0xa8, 0xc1,
    0xda, 0xad, 0x91, 0x79, 0xed, 0xce, 0xb9, 0xb3, 0xf9, 0x0d, 0x15,
    0x78, 0x58, 0x39, 0x5c,
    // clang-format on
};

std::vector<uint8_t> kQosNullDataFrame = {
    // clang-format off
    0xc8, 0x01, 0x2c, 0x00, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71, 0x94,
    0x65, 0x2d, 0xdc, 0x01, 0x00, 0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71,
    0xb0, 0x47, 0x00, 0x00,
    // clang-format on
};

std::vector<uint8_t> kDataFrame = {
    // clang-format off
    0x08, 0x41, 0xb8, 0x40, 0x40, 0xe3, 0xd6, 0xbf,
    0xf9, 0x60, 0x78, 0x4f, 0x43, 0x94, 0x34, 0x03,
    0x40, 0x00, 0x5e, 0xc1, 0x0b, 0x51, 0xd0, 0x4b,
    0x01, 0x00, 0x2e, 0xbc, 0x00, 0x20, 0x02, 0x00,
    0x00, 0x80, 0xdc, 0xde, 0x00, 0x50, 0xb3, 0xa4,
    0xbf, 0xed, 0x33, 0x57, 0x6b, 0x99, 0x63, 0x99,
    0xeb, 0xc4, 0xfa, 0x53, 0x1c, 0x1b, 0x01, 0xf9,
    0xa4, 0xea, 0xe0, 0x28, 0x98, 0xf3, 0x78, 0x32,
    0x5b, 0xda, 0xb6, 0x3e, 0x66, 0xb6, 0xf7, 0xea,
    0x66, 0x2e, 0x39, 0x3e, 0x61, 0x36, 0x54, 0xcb,
    0xd2, 0x64, 0x32, 0x68, 0x1d, 0x9e, 0xe2, 0x90,
    0xab, 0x25, 0x11, 0x97, 0x50, 0x92, 0x11, 0xe5,
    0xdd, 0x1a, 0xb4, 0xd9, 0x87, 0x42, 0x36, 0x24,
    0x57, 0x07, 0xb4, 0xd9, 0x99, 0xf2, 0x32, 0x9c,
    0xb7, 0x44, 0xa8, 0x76, 0xc2, 0xc0, 0x1c, 0xe4,
    0x29, 0x36, 0x40, 0x0c, 0x2c, 0x8e, 0x77, 0xdc,
    0xc8, 0xb9, 0xce, 0x9d, 0x9a, 0x41,
    // clang-format on
};

std::vector<uint8_t> kNullDataFrame = {
    // clang-format off
    0x48, 0x09, 0x2c, 0x00, 0x40, 0xe3, 0xd6, 0xbf,
    0xf1, 0x71, 0x00, 0x61, 0x71, 0xc6, 0xda, 0xa4,
    0x40, 0xe3, 0xd6, 0xbf, 0xf1, 0x71, 0x10, 0xd8,
    // clang-format on
};

std::vector<uint8_t> kPsPollFrame = {
    // clang-format off
    0xa4, 0x10, 0x03, 0xc0, 0x40, 0xe3, 0xd6, 0xbf,
    0xf1, 0x70, 0x8c, 0x85, 0x90, 0x27, 0x8c, 0x08,
    // clang-format on
};

std::vector<uint8_t> kPsPollHtcUnsupportedFrame = {
    // clang-format off
    0x76, 0xf8, 0x6b, 0x93, 0x04, 0xd9, 0x6c, 0x5e,
    0x3a, 0xfc, 0xa5, 0x4a, 0xca, 0xc9, 0x4c, 0xfe,
    0xea, 0x99, 0xe9, 0x4b, 0x02, 0x1f
    // clang-format on
};

std::vector<uint8_t> kEthernetFrame = {
    // clang-format off
    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    0x09, 0x0a, 0x0b, 0x0c, 0xcd, 0xab, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00
    // clang-format on
};

std::vector<uint8_t> kAmsduDataFrame = {
    // clang-format off
    // Data frame header
    0x88, 0x01, 0x30, 0x00, 0x7a, 0x8a, 0x20, 0x88,
    0x33, 0x16, 0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab,
    0x7a, 0x8a, 0x20, 0x88, 0x33, 0x16, 0x50, 0x01,
    // QoS control
    0x80, 0x00,
    // A-MSDU Subframe #1
    0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03,
    0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab,
    // MSDU length
    0x00, 0x74,
    // LLC header
    0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
    // Payload
    0x45, 0x00, 0x00, 0x6c, 0xd0, 0xb5, 0x40, 0x00,
    0x40, 0x06, 0xcc, 0x00, 0xc0, 0xa8, 0x2a, 0x30,
    0xac, 0xd9, 0x06, 0x24, 0x9e, 0x6c, 0x01, 0xbb,
    0x57, 0x21, 0x94, 0x93, 0xe7, 0xa0, 0xb8, 0xfa,
    0x80, 0x18, 0x01, 0x5b, 0xb7, 0x4e, 0x00, 0x00,
    0x01, 0x01, 0x08, 0x0a, 0x00, 0x06, 0x41, 0x33,
    0xcf, 0x8e, 0x43, 0xef, 0x17, 0x03, 0x03, 0x00,
    0x33, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x03, 0x6b, 0xfd, 0xb2, 0x00, 0x33, 0x27, 0xa7,
    0x18, 0xca, 0x7d, 0x00, 0xeb, 0xea, 0x24, 0x01,
    0x3c, 0xe7, 0x59, 0x69, 0xb0, 0x37, 0xfe, 0x52,
    0xeb, 0x05, 0xff, 0x10, 0xa8, 0x5c, 0x89, 0x7f,
    0xfa, 0x97, 0x47, 0x53, 0x92, 0x7a, 0x06, 0xac,
    0x76, 0x11, 0xd1, 0x2a,
    // Padding
    0x00, 0x00,
    // A-MSDU Subframe #2
    0x78, 0x8a, 0x20, 0x0d, 0x67, 0x03,
    0xb4, 0xf7, 0xa1, 0xbe, 0xb9, 0xab,
    // MSDU length
    0x00, 0x66,
    // LLC header
    0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x08, 0x00,
    // Payload
    0x45, 0x00, 0x00, 0x5e, 0xd0, 0xb6, 0x40, 0x00,
    0x40, 0x06, 0xcc, 0x0d, 0xc0, 0xa8, 0x2a, 0x30,
    0xac, 0xd9, 0x06, 0x24, 0x9e, 0x6c, 0x01, 0xbb,
    0x57, 0x21, 0x94, 0xcb, 0xe7, 0xa0, 0xb8, 0xfa,
    0x80, 0x18, 0x01, 0x5b, 0x1d, 0x09, 0x00, 0x00,
    0x01, 0x01, 0x08, 0x0a, 0x00, 0x06, 0x41, 0x33,
    0xcf, 0x8e, 0x43, 0xef, 0x17, 0x03, 0x03, 0x00,
    0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x04, 0xec, 0x35, 0xbb, 0x1c, 0xa7, 0x86, 0x2d,
    0x89, 0xcb, 0xad, 0x75, 0x20, 0x5d, 0x7b, 0xae,
    0x6b, 0x54, 0x11, 0x71, 0xaf, 0x8b, 0x11, 0xfc,
    0x89, 0xf0, 0xbd, 0xb5, 0xc3, 0x2a
    // clang-format on
};

}  // namespace test_data
}  // namespace wlan