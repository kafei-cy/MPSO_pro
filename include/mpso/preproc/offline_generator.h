#pragma once

#include <mpso/common/types.h>

namespace mpso::preproc {

struct OfflineConfig {
    u32 party_index = 0;
    u32 num_parties = 0;
    u32 log_num_elements = 0;
    u32 num_threads = 1;
};

void generateMpsiOffline(const OfflineConfig& config);

void generateMpsicOffline(const OfflineConfig& config);

void generateMpsicsOffline(const OfflineConfig& config);

void generateMpsuOffline(const OfflineConfig& config);

}
