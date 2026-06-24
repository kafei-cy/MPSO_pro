#include <mpso/common/permutation.h>

#include <stdexcept>

namespace mpso {

bool isPermutation(const std::vector<u32>& pi, u64 size)
{
    if (pi.size() != size) {
        return false;
    }

    std::vector<u8> seen(pi.size());
    for (const auto index : pi) {
        if (index >= pi.size() || seen[index] != 0) {
            return false;
        }
        seen[index] = 1;
    }

    return true;
}

void validatePermutation(const std::vector<u32>& pi, u64 size)
{
    if (!isPermutation(pi, size)) {
        throw std::invalid_argument("invalid permutation");
    }
}

template<typename T>
void permute(const std::vector<u32>& pi, std::vector<T>& data, PermuteSafety safety)
{
    if (pi.size() != data.size()) {
        throw std::invalid_argument("permutation size must match data size");
    }

    if (safety == PermuteSafety::Check) {
        validatePermutation(pi, data.size());
    }

    std::vector<T> result(data.size());
    for (size_t i = 0; i < pi.size(); ++i) {
        result[i] = data[pi[i]];
    }
    data.swap(result);
}

template void permute<u64>(
    const std::vector<u32>& pi,
    std::vector<u64>& data,
    PermuteSafety safety);

template void permute<block>(
    const std::vector<u32>& pi,
    std::vector<block>& data,
    PermuteSafety safety);

}
