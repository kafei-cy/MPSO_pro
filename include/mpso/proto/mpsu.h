#pragma once

#include <mpso/common/runtime.h>

#include <vector>

namespace mpso::proto {

std::vector<block> runMpsu(
    u32 party_index,
    u32 num_parties,
    u32 num_elements,
    std::vector<block>& set,
    u32 num_threads = 1);

}
