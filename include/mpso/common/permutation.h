#pragma once

#include <mpso/common/types.h>

#include <vector>

namespace mpso {

enum class PermuteSafety {
    Check,
    Trust,
};

bool isPermutation(const std::vector<u32>& pi, u64 size);

void validatePermutation(const std::vector<u32>& pi, u64 size);

template<typename T>
void permute(
    const std::vector<u32>& pi,
    std::vector<T>& data,
    PermuteSafety safety = PermuteSafety::Check);

extern template void permute<u64>(
    const std::vector<u32>& pi,
    std::vector<u64>& data,
    PermuteSafety safety);

extern template void permute<block>(
    const std::vector<u32>& pi,
    std::vector<block>& data,
    PermuteSafety safety);

}
