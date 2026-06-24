#pragma once

#include <mpso/common/types.h>

#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Common/CLP.h>
#include <cryptoTools/Common/Timer.h>
#include <cryptoTools/Crypto/PRNG.h>
#include <coproto/coproto.h>

namespace mpso {

using CLP = oc::CLP;
using Proto = coproto::task<void>;
using PRNG = oc::PRNG;
using BitVector = oc::BitVector;
using Timer = oc::Timer;
using Socket = coproto::Socket;

using oc::sysRandomSeed;

}
