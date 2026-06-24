#pragma once

#include <mpso/common/runtime.h>
#include <mpso/common/binary_io.h>
#include <mpso/common/offline_paths.h>
#include <mpso/shuffle/shuffle_types.h>

#include <fstream>
#include <string>
#include <type_traits>

namespace mpso::shuffle {

inline std::string filePath(const std::string& name, u32 num_parties, u32 num_elements, u32 party_index)
{
    return mpso::shuffleFile(name, num_parties, num_elements, party_index);
}

inline bool fileExists(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    return in.good();
}

template<typename T>
T identity()
{
    if constexpr (std::is_same_v<T, block>) {
        return ZeroBlock;
    } else {
        return T{};
    }
}

template<typename T, ShareMode Mode>
void add(T& lhs, const T& rhs)
{
    if constexpr (Mode == ShareMode::Xor) {
        lhs ^= rhs;
    } else {
        lhs += rhs;
    }
}

template<typename T, ShareMode Mode>
void sub(T& lhs, const T& rhs)
{
    if constexpr (Mode == ShareMode::Xor) {
        lhs ^= rhs;
    } else {
        lhs -= rhs;
    }
}

template<typename T, ShareMode Mode>
T inverse(const T& value)
{
    if constexpr (Mode == ShareMode::Xor) {
        return value;
    } else {
        return -value;
    }
}

}
