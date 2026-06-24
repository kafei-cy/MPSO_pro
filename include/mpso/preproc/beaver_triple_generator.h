#pragma once

#include <mpso/common/runtime.h>

#include <string>
#include <vector>

namespace mpso::preproc {

struct BeaverTripleSpec {
    u32 parties = 0;
    u32 elements = 0;
    u32 subgroup_size = 0;
    std::string suffix;
};

inline BeaverTripleSpec makeBeaverTripleSpec(
    u32 parties,
    u32 elements,
    u32 subgroup_size,
    std::string suffix = "")
{
    return BeaverTripleSpec{parties, elements, subgroup_size, std::move(suffix)};
}

void generateMpsuBeaverTriples(u32 parties, u32 log_num_elements);

template<typename T>
void generateBeaverTriples(
    u32 parties,
    u32 subgroup_size,
    u32 log_num_elements,
    const std::string& suffix = "");

template<typename T>
void multiplyWithBeaverTriples(
    u32 party_index,
    const std::vector<T>& input,
    std::vector<Socket>& channels,
    std::vector<T>& output,
    const BeaverTripleSpec& spec);

extern template void generateBeaverTriples<block>(
    u32 parties,
    u32 subgroup_size,
    u32 log_num_elements,
    const std::string& suffix);

extern template void generateBeaverTriples<u64>(
    u32 parties,
    u32 subgroup_size,
    u32 log_num_elements,
    const std::string& suffix);

extern template void multiplyWithBeaverTriples<block>(
    u32 party_index,
    const std::vector<block>& input,
    std::vector<Socket>& channels,
    std::vector<block>& output,
    const BeaverTripleSpec& spec);

extern template void multiplyWithBeaverTriples<u64>(
    u32 party_index,
    const std::vector<u64>& input,
    std::vector<Socket>& channels,
    std::vector<u64>& output,
    const BeaverTripleSpec& spec);

}
