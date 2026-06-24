#pragma once

#include <mpso/common/types.h>

namespace mpso {

constexpr u16 kPortBase = 1212;

constexpr u32 kPortStride = 100;

constexpr u32 kMaxLocalParties = kPortStride;

constexpr u32 kOkvsWeight = 3;

constexpr u32 kSsp = 40;

constexpr u32 kCuckooHashes = 3;

constexpr u32 kCuckooStashSize = 0;

constexpr u32 kSsOtFieldBits = 5;

inline block fixedCuckooSeed()
{
    return block(0x235677879795a931, 0x784915879d3e658a);
}

}
