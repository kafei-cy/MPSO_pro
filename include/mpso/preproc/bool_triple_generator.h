#pragma once

#include <mpso/common/types.h>

namespace mpso::preproc {

void generateMpsuBoolTriples(
    u32 party_index,
    u32 num_parties,
    u32 log_num_elements,
    u32 num_threads = 1);

}
