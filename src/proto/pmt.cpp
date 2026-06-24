#include <mpso/proto/pmt.h>

#include <mpso/common/binary_io.h>
#include <mpso/common/offline_paths.h>
#include <mpso/common/parameters.h>
#include <mpso/proto/proto_common.h>

#include <array>
#include <cstring>
#include <cryptoTools/Common/BitVector.h>
#include <cryptoTools/Crypto/AES.h>
#include <cryptoTools/Crypto/RandomOracle.h>
#include <stdexcept>
#include <string>
#include <volePSI/GMW/Gmw.h>
#include <volePSI/Paxos.h>

template<typename T>
using Matrix = oc::Matrix<T>;

template<typename T>
using Paxos = volePSI::Paxos<T>;

using PaxosParam = volePSI::PaxosParam;
using mpso::BitVector;
using mpso::block;
using mpso::PRNG;
using mpso::Socket;
using mpso::sysRandomSeed;
using mpso::u8;
using mpso::u32;
using mpso::u64;
using mpso::ZeroBlock;

namespace {

void requirePath(const std::string& path, const std::string& label)
{
    if (path.empty()) {
        throw std::invalid_argument("empty " + label);
    }
}

u32 pmtKeyBitLength(u32 num_parties, u32 num_bins)
{
    const u64 interaction_count =
        (static_cast<u64>(num_parties) * (num_parties - 1)) / 2;
    return mpso::kSsp + oc::log2ceil(static_cast<u64>(num_bins) * interaction_count);
}

}

namespace mpso::proto {

// for MPSU, if x doesn't exist in Y, get same string
// get the OPPRF key
void sendPmtOt(u32 num_parties, u32 num_elements, block cuckoo_seed, std::vector<std::vector<block>>& simple_hash_table, Socket& channel, std::vector<block>& ssrot_output, const std::string& pair_tag, u32 num_threads)
{
    PRNG prng(sysRandomSeed());
    const u32 num_bins = checkedU32(simple_hash_table.size(), "PMT bin count");

    requirePath(pair_tag, "party pair tag");
    const auto vole_path = voleFile(pair_tag);
    const auto triple_path = gmwTripleFile(pair_tag);
    const auto random_ot_path = randomOtFile(pair_tag);

    auto vole_input_file = mpso::openInput(vole_path);
    auto triple_input_file = mpso::openInput(triple_path);
    auto random_ot_input_file = mpso::openInput(random_ot_path);

    block mD;
    std::vector<block> mB(num_bins, ZeroBlock);
    mpso::readBinary(vole_input_file, &mD, 1, vole_path);
    mpso::readBinary(vole_input_file, mB, vole_path);

    // correlation_difference received
    std::vector<block> correlation_difference(num_bins);
    coproto::sync_wait(channel.recv(correlation_difference));

    // send paxos_seed
    block paxos_seed = prng.get();    
    coproto::sync_wait(channel.send(paxos_seed));


    // set for PRF(k, x||z)    
    oc::AES hasher;
    hasher.setKey(cuckoo_seed);

    const u32 key_bit_length = pmtKeyBitLength(num_parties, num_bins);
    const u32 key_byte_length = oc::divCeil(key_bit_length, 8);

    // get random labels
    Matrix<u8> mLabel(num_bins, key_byte_length);
    prng.get(mLabel.data(), mLabel.size());
    Paxos<u32> mPaxos;
    mPaxos.init(num_elements * mpso::kOkvsWeight, mpso::kOkvsWeight, mpso::kSsp, PaxosParam::Binary, paxos_seed);
    std::vector<block> vecK(num_elements * mpso::kOkvsWeight);
    Matrix<u8> values(num_elements * mpso::kOkvsWeight, key_byte_length);

    u32 value_index = 0;
    for (u32 i = 0; i < num_bins; ++i)
    {
    	auto bin_size = simple_hash_table[i].size();
    	for(u32 p = 0; p < bin_size; ++p)
    	{
    	    auto hyj = simple_hash_table[i][p];
    	    vecK[value_index] = hyj;
    	    hyj ^= correlation_difference[i];
    	    auto masked_key = mB[i] ^ (hyj.gf128Mul(mD));
    	    masked_key = hasher.hashBlock(masked_key);
    	    memcpy(&values(value_index, 0), &masked_key, key_byte_length);
    	    for(u64 byte_index = 0; byte_index < key_byte_length; ++byte_index)
    	    {
    	    	values(value_index, byte_index) ^= mLabel(i, byte_index);
    	    }
    	    value_index += 1;
    	}        
    }
    
    Matrix<u8> okvs(mPaxos.size(), key_byte_length);
    mPaxos.setInput(vecK);
    mPaxos.encode<u8>(values, okvs);
    // when using multi threads for okvs, there might be some problems    
    coproto::sync_wait(channel.send(okvs));
    
    // call gmw
    volePSI::BetaCircuit cir = volePSI::isZeroCircuit(key_bit_length);
    volePSI::Gmw cmp;
    cmp.init(mLabel.rows(), cir, num_threads, 1, prng.get());
    std::vector<block> a, b, c, d;
    u64 num_triples = cmp.mNumOts / 2;
    
    a.resize(num_triples / 128, ZeroBlock);
    b.resize(num_triples / 128, ZeroBlock);
    c.resize(num_triples / 128, ZeroBlock);
    d.resize(num_triples / 128, ZeroBlock);
    mpso::readBinary(triple_input_file, a, triple_path);
    mpso::readBinary(triple_input_file, b, triple_path);
    mpso::readBinary(triple_input_file, c, triple_path);
    mpso::readBinary(triple_input_file, d, triple_path);
    cmp.setTriples(a, b, c, d);    

    cmp.setInput(0, mLabel);
    coproto::sync_wait(cmp.run(channel));
    
    Matrix<u8> gmw_output;
    gmw_output.resize(num_bins, 1);
    cmp.getOutput(0, gmw_output);
    
    // get the bitvector
    BitVector out(num_bins);
    for (u32 i = 0; i < num_bins; ++i){
        out[i] = gmw_output(i, 0) & 1;
    }   

    // ssROT sender
    BitVector bit_delta(num_bins);
    oc::AlignedVector<std::array<block, 2>> sender_messages(num_bins);

    mpso::readBinary(random_ot_input_file, sender_messages.data(), num_bins, random_ot_path);
    coproto::sync_wait(channel.recv(bit_delta));

    out ^= bit_delta;
    ssrot_output.resize(num_bins);
    for (u32 i = 0; i < num_bins; ++i){
        ssrot_output[i] = sender_messages[i][out[i]];
    }

}


// get the OPPRF values
void receivePmtOt(u32 num_parties, u32 num_elements, block cuckoo_seed, std::vector<block>& cuckoo_hash_table, Socket& channel, std::vector<block>& ssrot_output, const std::string& pair_tag, u32 num_threads)
{
    PRNG prng(sysRandomSeed());
    const u32 num_bins = checkedU32(cuckoo_hash_table.size(), "PMT bin count");

    requirePath(pair_tag, "party pair tag");
    const auto vole_path = voleFile(pair_tag);
    const auto triple_path = gmwTripleFile(pair_tag);
    const auto random_ot_path = randomOtFile(pair_tag);

    auto vole_input_file = mpso::openInput(vole_path);
    auto triple_input_file = mpso::openInput(triple_path);
    auto random_ot_input_file = mpso::openInput(random_ot_path);

    // send correlation_difference
    std::vector<block> mA(num_bins, ZeroBlock);
    std::vector<block> mC(num_bins, ZeroBlock);
    mpso::readBinary(vole_input_file, mA, vole_path);
    mpso::readBinary(vole_input_file, mC, vole_path);
    std::vector<block> correlation_difference(num_bins); 
    for (u32 i = 0; i < num_bins; ++i)
    {
    	correlation_difference[i] = cuckoo_hash_table[i] ^ mC[i];
    }     
    coproto::sync_wait(channel.send(correlation_difference));

    // recv paxos_seed
    block paxos_seed;
    coproto::sync_wait(channel.recv(paxos_seed));

    oc::AES hasher;
    hasher.setKey(cuckoo_seed);    

    const u32 key_bit_length = pmtKeyBitLength(num_parties, num_bins);
    const u32 key_byte_length = oc::divCeil(key_bit_length, 8);


    Paxos<u32> mPaxos;
    mPaxos.init(num_elements * mpso::kOkvsWeight, mpso::kOkvsWeight, mpso::kSsp, PaxosParam::Binary, paxos_seed);
    Matrix<u8> values(num_bins, key_byte_length);
    Matrix<u8> okvs(mPaxos.size(), key_byte_length);
    coproto::sync_wait(channel.recv(okvs));
    mPaxos.decode<u8>(cuckoo_hash_table, values, okvs);
    
    Matrix<u8> mLabel(num_bins, key_byte_length);
    
    for (u32 i = 0; i < num_bins; ++i)
    {
    	auto prf = hasher.hashBlock(mA[i]);
    	memcpy(&mLabel(i,0), &prf, key_byte_length);
    	for(u32 byte_index = 0; byte_index < key_byte_length; ++byte_index) {
    		mLabel(i, byte_index) ^= values(i, byte_index);
        }
    }
        
    volePSI::BetaCircuit cir = volePSI::isZeroCircuit(key_bit_length);
    volePSI::Gmw cmp;
    cmp.init(mLabel.rows(), cir, num_threads, 0, prng.get());
    cmp.implSetInput(0, mLabel, mLabel.cols());;
    
    std::vector<block> a, b, c, d;
    u64 num_triples = cmp.mNumOts / 2;
    a.resize(num_triples / 128, ZeroBlock);
    b.resize(num_triples / 128, ZeroBlock);
    c.resize(num_triples / 128, ZeroBlock);
    d.resize(num_triples / 128, ZeroBlock);

    mpso::readBinary(triple_input_file, a, triple_path);
    mpso::readBinary(triple_input_file, b, triple_path);
    mpso::readBinary(triple_input_file, c, triple_path);
    mpso::readBinary(triple_input_file, d, triple_path);
    cmp.setTriples(a, b, c, d);    
  
    coproto::sync_wait(cmp.run(channel));
    
    Matrix<u8> gmw_output;
    gmw_output.resize(num_bins, 1);
    cmp.getOutput(0, gmw_output);
    
    BitVector out(num_bins); 
    for (u32 i = 0; i < num_bins; ++i){
        out[i] = gmw_output(i, 0) & 1;
    }        

    // ssROT receiver
    BitVector bit_delta(num_bins);
    ssrot_output.resize(num_bins);

    mpso::readBinary(random_ot_input_file, ssrot_output, random_ot_path);
    mpso::readBinary(random_ot_input_file, bit_delta.data(), bit_delta.sizeBytes(), random_ot_path);

    bit_delta ^= out;
    coproto::sync_wait(channel.send(bit_delta));  
}







// for MPSI/MSPIC and part of MPSICS, if x exist in Y, get same strings

// get the OPPRF key
void sendPsiOpprf(u32 num_elements, block cuckoo_seed, std::vector<std::vector<block>>& simple_hash_table, Socket& channel, std::vector<u64>& opprf_output, const std::string& vole_path, u32 num_threads)
{
    PRNG prng(sysRandomSeed());
    const u32 num_bins = checkedU32(simple_hash_table.size(), "OPPRF bin count");

    requirePath(vole_path, "VOLE path");
    auto vole_input_file = mpso::openInput(vole_path);

    // read vole from file
    block mD;
    std::vector<block> mB(num_bins, ZeroBlock);
    mpso::readBinary(vole_input_file, &mD, 1, vole_path);
    mpso::readBinary(vole_input_file, mB, vole_path);

    // correlation_difference received
    std::vector<block> correlation_difference(num_bins);
    coproto::sync_wait(channel.recv(correlation_difference));

    // send paxos_seed
    block paxos_seed = prng.get();    
    coproto::sync_wait(channel.send(paxos_seed));


    // get random labels
    std::vector<u64> mLabel(num_bins);
    prng.get(mLabel.data(), mLabel.size());
    Paxos<u32> mPaxos;
    mPaxos.init(num_elements * mpso::kOkvsWeight, mpso::kOkvsWeight, mpso::kSsp, PaxosParam::Binary, paxos_seed);
    std::vector<block> vecK(num_elements * mpso::kOkvsWeight);
    Matrix<u8> values(num_elements * mpso::kOkvsWeight, 8);
    Matrix<u8> okvs(mPaxos.size(), 8);

    u32 value_index = 0;
    oc::RandomOracle hash64(8);
    for (u32 i = 0; i < num_bins; ++i)
    {
    	auto bin_size = simple_hash_table[i].size();
    	for(u32 p = 0; p < bin_size; ++p)
    	{
    	    auto hyj = simple_hash_table[i][p];
    	    vecK[value_index] = hyj;
    	    hyj ^= correlation_difference[i];
    	    auto masked_key = mB[i] ^ (hyj.gf128Mul(mD));
            u64 hash_value;
            hash64.Update(masked_key);
            hash64.Final(hash_value);
            hash64.Reset();

            hash_value ^= mLabel[i];
            memcpy(&values(value_index, 0), &hash_value, 8);
    	    value_index += 1;
    	}        
    }
       
    mPaxos.setInput(vecK);
    mPaxos.encode<u8>(values, okvs);
    // when using multi threads for okvs, there might be some problems    
    coproto::sync_wait(channel.send(okvs));
    
    opprf_output.resize(num_bins);
    
    for (u32 i = 0; i < num_bins; ++i){
        opprf_output[i] = mLabel[i];
    }
}

//get the OPPRF values
void receivePsiOpprf(u32 num_elements, block cuckoo_seed, std::vector<block>& cuckoo_hash_table, Socket& channel, std::vector<u64>& opprf_output, const std::string& vole_path, u32 num_threads)
{
    PRNG prng(sysRandomSeed());
    const u32 num_bins = checkedU32(cuckoo_hash_table.size(), "OPPRF bin count");

    requirePath(vole_path, "VOLE path");
    auto vole_input_file = mpso::openInput(vole_path);

    // send correlation_difference
    std::vector<block> mA(num_bins, ZeroBlock);
    std::vector<block> mC(num_bins, ZeroBlock);
    mpso::readBinary(vole_input_file, mA, vole_path);
    mpso::readBinary(vole_input_file, mC, vole_path);
    std::vector<block> correlation_difference(num_bins); 
    for (u32 i = 0; i < num_bins; ++i)
    {
    	correlation_difference[i] = cuckoo_hash_table[i] ^ mC[i];
    }     
    coproto::sync_wait(channel.send(correlation_difference));

    // recv paxos_seed
    block paxos_seed;
    coproto::sync_wait(channel.recv(paxos_seed));  

    Paxos<u32> mPaxos;
    mPaxos.init(num_elements * mpso::kOkvsWeight, mpso::kOkvsWeight, mpso::kSsp, PaxosParam::Binary, paxos_seed);
    Matrix<u8> values(num_bins, 8);
    Matrix<u8> okvs(mPaxos.size(), 8);
    coproto::sync_wait(channel.recv(okvs));
    mPaxos.decode<u8>(cuckoo_hash_table, values, okvs);
    
    oc::RandomOracle hash64(8);       
    opprf_output.resize(num_bins);
    for (u32 i = 0; i < num_bins; ++i)
    {
        u64 hash_value;
        hash64.Update(mA[i]);
        hash64.Final(hash_value);
        hash64.Reset();        
        memcpy(&opprf_output[i], &values(i,0), 8);
        opprf_output[i] ^= hash_value;
    }
}

// for part of MPSICS, get OPPRF values of u64

// get the OPPRF key
void sendSumOpprf(u32 num_elements, block cuckoo_seed, std::vector<std::vector<block>>& simple_hash_table, Socket& channel, std::vector<u64>& opprf_output, const std::string& vole_path, u32 num_threads)
{
    PRNG prng(sysRandomSeed());
    const u32 num_bins = checkedU32(simple_hash_table.size(), "OPPRF bin count");

    requirePath(vole_path, "VOLE path");
    auto vole_input_file = mpso::openInput(vole_path);

    // read vole from file
    block mD;
    std::vector<block> mB(num_bins, ZeroBlock);
    mpso::readBinary(vole_input_file, &mD, 1, vole_path);
    mpso::readBinary(vole_input_file, mB, vole_path);

    // correlation_difference received
    std::vector<block> correlation_difference(num_bins);
    coproto::sync_wait(channel.recv(correlation_difference));

    // send paxos_seed
    block paxos_seed = prng.get();    
    coproto::sync_wait(channel.send(paxos_seed));

    std::vector<u64> mLabel(num_bins);
    prng.get(mLabel.data(), mLabel.size());

    Paxos<u32> mPaxos;
    mPaxos.init(num_elements * mpso::kOkvsWeight, mpso::kOkvsWeight, mpso::kSsp, PaxosParam::Binary, paxos_seed);
    std::vector<block> vecK(num_elements * mpso::kOkvsWeight);
    Matrix<u8> values(num_elements * mpso::kOkvsWeight, 8);
    Matrix<u8> okvs(mPaxos.size(), 8);

    oc::RandomOracle hash64(8); 
    u32 value_index = 0;
    for (u32 i = 0; i < num_bins; ++i)
    {
    	auto bin_size = simple_hash_table[i].size();
    	for(u32 p = 0; p < bin_size; ++p)
    	{
    	    auto hyj = simple_hash_table[i][p];
            u64 masked_value = hyj.mData[1] - mLabel[i];

    	    vecK[value_index] = hyj;
    	    hyj ^= correlation_difference[i];
    	    auto masked_key = mB[i] ^ (hyj.gf128Mul(mD));

            u64 hash_value;
            hash64.Update(masked_key);
            hash64.Final(hash_value);
            hash64.Reset();   

            masked_value ^= hash_value;
            memcpy(&values(value_index, 0), &masked_value, 8);
    	    value_index += 1;
    	}        
    }
    
    mPaxos.setInput(vecK);
    mPaxos.encode<u8>(values, okvs);
    // when using multi threads for okvs, there might be some problems    
    coproto::sync_wait(channel.send(okvs));    

    opprf_output.resize(num_bins);
    for (u32 i = 0; i < num_bins; ++i){
        opprf_output[i] = mLabel[i];
    }
}


// get OPPRF values
void receiveSumOpprf(u32 num_elements, block cuckoo_seed, std::vector<block>& cuckoo_hash_table, Socket& channel, std::vector<u64>& opprf_output, const std::string& vole_path, u32 num_threads)
{
    PRNG prng(sysRandomSeed());
    const u32 num_bins = checkedU32(cuckoo_hash_table.size(), "OPPRF bin count");

    requirePath(vole_path, "VOLE path");
    auto vole_input_file = mpso::openInput(vole_path);

    // send correlation_difference
    std::vector<block> mA(num_bins, ZeroBlock);
    std::vector<block> mC(num_bins, ZeroBlock);
    mpso::readBinary(vole_input_file, mA, vole_path);
    mpso::readBinary(vole_input_file, mC, vole_path);
    std::vector<block> correlation_difference(num_bins); 
    for (u32 i = 0; i < num_bins; ++i)
    {
    	correlation_difference[i] = cuckoo_hash_table[i] ^ mC[i];
    }     
    coproto::sync_wait(channel.send(correlation_difference));

    // recv paxos_seed
    block paxos_seed;
    coproto::sync_wait(channel.recv(paxos_seed));

    Paxos<u32> mPaxos;
    mPaxos.init(num_elements * mpso::kOkvsWeight, mpso::kOkvsWeight, mpso::kSsp, PaxosParam::Binary, paxos_seed);
    Matrix<u8> values(num_bins, 8);
    Matrix<u8> okvs(mPaxos.size(), 8);
    coproto::sync_wait(channel.recv(okvs));
    mPaxos.decode<u8>(cuckoo_hash_table, values, okvs);

    oc::RandomOracle hash64(8); 
    opprf_output.resize(num_bins);
    for (u32 i = 0; i < num_bins; ++i)
    {
        u64 hash_value;
        hash64.Update(mA[i]);
        hash64.Final(hash_value);
        hash64.Reset();          
        u64 decoded_value;
        memcpy(&decoded_value, &values(i,0), 8); 

        opprf_output[i] = decoded_value ^ hash_value;
    }
}

}
