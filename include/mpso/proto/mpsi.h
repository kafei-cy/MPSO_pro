#pragma once

#include <mpso/common/runtime.h>

#include <vector>

namespace mpso::proto {

std::vector<u64> runMpsi(
    u32 party_index,
    u32 num_parties,
    u32 num_elements,
    std::vector<block>& set,
    u32 num_threads = 1);

u32 runMpsic(
    u32 party_index,
    u32 num_parties,
    u32 num_elements,
    std::vector<block>& set,
    u32 num_threads = 1);

u64 runMpsics(
    u32 party_index,
    u32 num_parties,
    u32 num_elements,
    std::vector<block>& set,
    u32 num_threads = 1);

}
