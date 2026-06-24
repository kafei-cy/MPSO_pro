#pragma once

#include <mpso/common/parameters.h>

#include <cryptoTools/Common/CuckooIndex.h>

namespace mpso {

inline u32 numCuckooBins(u64 num_elements)
{
    auto params = oc::CuckooIndex<>::selectParams(num_elements, kSsp, kCuckooStashSize, kCuckooHashes);
    return static_cast<u32>(params.numBins());
}

}
