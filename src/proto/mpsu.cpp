
#include <mpso/proto/mpsu.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/common/network.h>
#include <mpso/common/offline_paths.h>
#include <mpso/preproc/beaver_triple_generator.h>
#include <mpso/shuffle/online_shuffle.h>
#include <mpso/proto/pmt.h>
#include <mpso/proto/proto_common.h>

#include <cryptoTools/Common/CuckooIndex.h>
#include <cryptoTools/Crypto/RandomOracle.h>

#include <iomanip>
#include <iostream>
#include <exception>
#include <thread>

using mpso::block;
using mpso::PRNG;
using mpso::sysRandomSeed;
using mpso::Timer;
using mpso::u32;
using mpso::u64;
using mpso::ZeroBlock;

namespace mpso::proto {

std::vector<block> runMpsu(u32 party_index, u32 num_parties, u32 num_elements, std::vector<block> &set, u32 num_threads)
{
    u32 num_bins = mpso::numCuckooBins(num_elements);
    
    mpso::shuffle::BlockOnline shuffle(party_index, num_parties, num_bins * (num_parties - 1));
    shuffle.load("psu");

    Timer timer;
    timer.setTimePoint("start");

    auto channels = mpso::connectParties(party_index, num_parties);
    
    PRNG prng(sysRandomSeed());
    oc::RandomOracle hash64(8); 
    block cuckoo_seed = mpso::fixedCuckooSeed();
    std::vector<block> share((num_parties - 1) * num_bins, ZeroBlock);
        
    std::vector<std::thread>  pmt_ot_threads(num_parties - 1);
    std::vector<std::exception_ptr> pmt_ot_errors(num_parties - 1);
    std::vector<std::vector<block>> simple_hash_table;
    std::vector<std::vector<block>> ssrot_vector_by_party(num_parties - 1);

    std::vector<std::vector<block>> beaver_shares(num_parties - 1);
 
    if (party_index != num_parties - 1) {
        simple_hash_table = buildSimpleHashTable(set, num_bins, num_elements, cuckoo_seed);
    }
    
    if(party_index == 0)
    {                              
        for (u32 i = 1; i < num_parties; ++i)
        {             
            std::string pair_tag = pairTag(num_parties, num_elements, party_index, i);
            pmt_ot_threads[i - 1] = std::thread([&, i, pair_tag]() {
                try {
                    sendPmtOt(num_parties, num_elements, cuckoo_seed, simple_hash_table, channels[i - 1], ssrot_vector_by_party[i - 1], pair_tag, num_threads);
                } catch (...) {
                    pmt_ot_errors[i - 1] = std::current_exception();
                }
            });                               
        }
        joinThreads(pmt_ot_threads, pmt_ot_errors);

        timer.setTimePoint("pmt add ot done"); 
        
        for (u32 j = 1; j < num_parties; ++j)
        {             
            const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
                num_parties,
                num_elements,
                j + 1);
            mpso::preproc::multiplyWithBeaverTriples<block>(party_index, ssrot_vector_by_party[j-1], channels, beaver_shares[j-1], beaver_spec);
        }
        timer.setTimePoint("beaver multiplication done");
        
        // compute share by uVector
        for(u32 i = 0; i < num_parties - 1; ++i)
        {
            for(u32 j = 0; j < num_bins; ++j)	
            {
            	share[i*num_bins + j] ^= beaver_shares[i][j];            
            }                
        }
                
        coproto::sync_wait(shuffle.run(channels, share));
    	timer.setTimePoint("mshuffle done");         

        receiveAndXorShares(channels, share, num_parties);
        // check MAC
        std::vector<block> set_union(set);
        for (size_t i = 0; i < share.size(); i++){
            long long mac;
            hash64.Update(share[i].mData[0]);
            hash64.Final(mac);
            hash64.Reset();
            if (share[i].mData[1] == mac){
                set_union.emplace_back(share[i].mData[0]);                
            }
        }
        timer.setTimePoint("end");


        u64 total_comm_bytes = mpso::commBytes(channels);
        for (u32 party = 1; party < num_parties; ++party) {
            u64 party_comm_bytes = 0;
            coproto::sync_wait(channels[party - 1].recv(party_comm_bytes));
            total_comm_bytes += party_comm_bytes;
        }
        const double comm = static_cast<double>(total_comm_bytes) / 2 / 1024 / 1024;
        mpso::closeChannels(channels);
        
        std::cout << "communication cost = " << std::fixed << std::setprecision(3) << comm << " MB" << std::endl;
        std::cout << timer << std::endl;
        return set_union;                                          
    }
    else
    {   
        // establish cuckoo hash table
        oc::CuckooIndex cuckoo;
        cuckoo.init(num_elements, mpso::kSsp, mpso::kCuckooStashSize, mpso::kCuckooHashes);
        cuckoo.insert(set, cuckoo_seed);
                
        oc::RandomOracle hash64(8);        
        std::vector<block> cuckoo_hash_table(num_bins); //x||z              
        std::vector<block> hashed_set(num_bins);//compute hashset: h(x)||x
        for (u32 i = 0; i < num_bins; ++i)
        {
            auto bin = cuckoo.mBins[i];
            if (bin.isEmpty() == false)
            {
                auto j = bin.hashIdx();
                auto b = bin.idx();
                u64 item_value = set[b].mData[0];                
                cuckoo_hash_table[i] = block(item_value, j);//compute x||z
                u64 item_hash;
                hash64.Update(item_value);
                hash64.Final(item_hash);
                hash64.Reset();                                
                hashed_set[i] = block(item_hash, item_value);  //h(x)||x                                                              
            }
            else
            {          	          	
          	    hashed_set[i] = prng.get();           	              
            }                                
        }
        
        // add MAC to the share
        u32 offset = (party_index - 1) * num_bins;
        std::copy(hashed_set.begin(), hashed_set.end(), share.begin() + offset);

                       
        for (u32 i = 0; i < num_parties; ++i){
            std::string pair_tag = pairTag(num_parties, num_elements, party_index, i);
            if (i < party_index){
                pmt_ot_threads[i] = std::thread([&, i, pair_tag]() {
                    try {
                        receivePmtOt(num_parties, num_elements, cuckoo_seed, cuckoo_hash_table, channels[i], ssrot_vector_by_party[i], pair_tag, num_threads);
                    } catch (...) {
                        pmt_ot_errors[i] = std::current_exception();
                    }
                });
            } else if (i > party_index){
                pmt_ot_threads[i - 1] = std::thread([&, i, pair_tag]() {
                    try {
                        sendPmtOt(num_parties, num_elements, cuckoo_seed, simple_hash_table, channels[i-1], ssrot_vector_by_party[i-1], pair_tag, num_threads);
                    } catch (...) {
                        pmt_ot_errors[i - 1] = std::current_exception();
                    }
                });
            }
        }
        joinThreads(pmt_ot_threads, pmt_ot_errors);
        timer.setTimePoint("pmt add ot done");        
        
        for (u32 j = party_index; j < num_parties; ++j)
        {             
            if (j == party_index){
                const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
                    num_parties,
                    num_elements,
                    party_index + 1);
                mpso::preproc::multiplyWithBeaverTriples<block>(party_index, ssrot_vector_by_party[j-1], channels, beaver_shares[j-1], beaver_spec);
            }else if (j > party_index){
                const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
                    num_parties,
                    num_elements,
                    j + 1);
                mpso::preproc::multiplyWithBeaverTriples<block>(party_index, ssrot_vector_by_party[j-1], channels, beaver_shares[j-1], beaver_spec);
            }                                 
        }
        timer.setTimePoint("beaver multiplication done");
        
    	
        // compute share by uVector
        for(u32 i = party_index-1; i < num_parties-1; ++i)
        {
            for(u32 j = 0; j < num_bins; ++j)	
            {
            	share[i *num_bins + j] ^= beaver_shares[i][j];            
            }                
        }   
        coproto::sync_wait(shuffle.run(channels, share));
        timer.setTimePoint("mshuffle done"); 	         

        // reconstruct output
        coproto::sync_wait(channels[0].send(share));

        timer.setTimePoint("end");

        const u64 comm_bytes = mpso::commBytes(channels);
        coproto::sync_wait(channels[0].send(comm_bytes));

        mpso::closeChannels(channels);
        return std::vector<block>();
    }
}

}
