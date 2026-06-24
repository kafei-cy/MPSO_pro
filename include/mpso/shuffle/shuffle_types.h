#pragma once

#include <mpso/common/runtime.h>

namespace mpso::shuffle {

enum class ShareMode {
    Xor,
    Add,
};

template<typename T, ShareMode Mode>
class Online;

template<typename T, ShareMode Mode>
class Correlation;

using BlockOnline = Online<block, ShareMode::Xor>;
using XorOnline = Online<u64, ShareMode::Xor>;
using AddOnline = Online<u64, ShareMode::Add>;

using BlockCorrelation = Correlation<block, ShareMode::Xor>;
using XorCorrelation = Correlation<u64, ShareMode::Xor>;
using AddCorrelation = Correlation<u64, ShareMode::Add>;

}
