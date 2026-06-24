#pragma once

#include <mpso/common/runtime.h>

#include <string>
#include <vector>

namespace mpso::proto {

void sendPmtOt(
    u32 num_parties,
    u32 num_elements,
    block cuckoo_seed,
    std::vector<std::vector<block>>& simple_hash_table,
    Socket& channel,
    std::vector<block>& ssrot_output,
    const std::string& pair_tag,
    u32 num_threads = 1);

void receivePmtOt(
    u32 num_parties,
    u32 num_elements,
    block cuckoo_seed,
    std::vector<block>& cuckoo_hash_table,
    Socket& channel,
    std::vector<block>& ssrot_output,
    const std::string& pair_tag,
    u32 num_threads = 1);

void sendPsiOpprf(
    u32 num_elements,
    block cuckoo_seed,
    std::vector<std::vector<block>>& simple_hash_table,
    Socket& channel,
    std::vector<u64>& opprf_output,
    const std::string& vole_path,
    u32 num_threads = 1);

void receivePsiOpprf(
    u32 num_elements,
    block cuckoo_seed,
    std::vector<block>& cuckoo_hash_table,
    Socket& channel,
    std::vector<u64>& opprf_output,
    const std::string& vole_path,
    u32 num_threads = 1);

void sendSumOpprf(
    u32 num_elements,
    block cuckoo_seed,
    std::vector<std::vector<block>>& simple_hash_table,
    Socket& channel,
    std::vector<u64>& opprf_output,
    const std::string& vole_path,
    u32 num_threads = 1);

void receiveSumOpprf(
    u32 num_elements,
    block cuckoo_seed,
    std::vector<block>& cuckoo_hash_table,
    Socket& channel,
    std::vector<u64>& opprf_output,
    const std::string& vole_path,
    u32 num_threads = 1);

}
