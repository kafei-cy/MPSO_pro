#pragma once

#include <mpso/common/types.h>

#include <string>

namespace mpso {

inline std::string pairTag(
    u32 num_parties,
    u32 num_elements,
    u32 first_party_index,
    u32 second_party_index)
{
    return std::to_string(num_parties) + "_"
        + std::to_string(num_elements) + "_P"
        + std::to_string(first_party_index + 1)
        + std::to_string(second_party_index + 1);
}

inline std::string voleFile(const std::string& party_pair_tag, const std::string& suffix = "")
{
    return "./offline/vole_" + party_pair_tag + suffix;
}

inline std::string voleFile(
    u32 num_parties,
    u32 num_elements,
    u32 first_party_index,
    u32 second_party_index,
    const std::string& suffix = "")
{
    return voleFile(pairTag(num_parties, num_elements, first_party_index, second_party_index), suffix);
}

inline std::string randomOtFile(const std::string& party_pair_tag)
{
    return "./offline/rot_" + party_pair_tag;
}

inline std::string gmwTripleFile(const std::string& party_pair_tag)
{
    return "./offline/triple_" + party_pair_tag;
}

inline std::string beaverTripleFile(
    u32 num_parties,
    u32 num_elements,
    u32 party_index,
    u32 subgroup_size,
    const std::string& suffix = "")
{
    return "./offline/bt_" + std::to_string(num_parties) + "_"
        + std::to_string(num_elements) + "_P"
        + std::to_string(party_index + 1)
        + std::to_string(subgroup_size)
        + suffix;
}

inline std::string shuffleFile(const std::string& name, u32 num_parties, u32 num_elements, u32 party_index)
{
    return "./offline/sc_" + name + "_" + std::to_string(num_parties) + "_"
        + std::to_string(num_elements) + "_P" + std::to_string(party_index + 1);
}

}
