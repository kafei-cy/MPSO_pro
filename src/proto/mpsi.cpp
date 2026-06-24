
#include <mpso/proto/mpsi.h>

#include <mpso/common/cuckoo_layout.h>
#include <mpso/common/network.h>
#include <mpso/common/offline_paths.h>
#include <mpso/preproc/beaver_triple_generator.h>
#include <mpso/shuffle/online_shuffle.h>
#include <mpso/proto/pmt.h>
#include <mpso/proto/proto_common.h>

#include <iomanip>
#include <iostream>
#include <exception>
#include <thread>

using mpso::BitVector;
using mpso::block;
using mpso::PRNG;
using mpso::sysRandomSeed;
using mpso::Timer;
using mpso::u32;
using mpso::u64;

namespace mpso::proto {

// P_0 cuckoo, others simple hash
std::vector<u64> runMpsi(u32 party_index, u32 num_parties, u32 num_elements, std::vector<block> &set, u32 num_threads)
{
    u32 num_bins = mpso::numCuckooBins(num_elements);


    Timer timer;
    timer.setTimePoint("start");

    auto channels = mpso::connectParties(party_index, num_parties);

    PRNG prng(sysRandomSeed());
    block cuckoo_seed = mpso::fixedCuckooSeed();
        

    // for opprf            
    std::vector<std::thread>  opprf_threads(num_parties - 1);
    std::vector<std::exception_ptr> opprf_errors(num_parties - 1);
    std::vector<std::vector<block>> simple_hash_table;
    std::vector<block> cuckoo_hash_table;
    std::vector<std::vector<u64>> opprf_vector_by_party(num_parties - 1);// for P_0
    std::vector<u64> opprf_values(num_bins);// before btMul
    std::vector<u64> beaver_product(num_bins);

    if (party_index != 0) {
        simple_hash_table = buildSimpleHashTable(set, num_bins, num_elements, cuckoo_seed);
    }

    if(party_index == 0){
        cuckoo_hash_table = buildCuckooHashTable(set, num_bins, num_elements, cuckoo_seed);

        for (u32 i = 1; i < num_parties; ++i)
        {             
            std::string opprf_path = voleFile(num_parties, num_elements, party_index, i, "PSI");
            opprf_threads[i - 1] = std::thread([&, i, opprf_path]() {
                try {
                    receivePsiOpprf(num_elements, cuckoo_seed, cuckoo_hash_table, channels[i - 1], opprf_vector_by_party[i - 1], opprf_path, num_threads);
                } catch (...) {
                    opprf_errors[i - 1] = std::current_exception();
                }
            });                               
        }
        joinThreads(opprf_threads, opprf_errors);
        timer.setTimePoint("opprf done"); 
 
        // add all the OPPRF and then ssROT values
        for (u32 i = 0; i < num_bins; ++i){
            for (u32 j = 0; j < num_parties-1; ++j){
                opprf_values[i] ^= opprf_vector_by_party[j][i];
            }
        } 

        const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
            num_parties,
            num_elements,
            num_parties,
            "PSI64");
        mpso::preproc::multiplyWithBeaverTriples<u64>(party_index, opprf_values, channels, beaver_product, beaver_spec);
        timer.setTimePoint("beaver multiplication done");        
        
        // compute share = beaver_product
        std::vector<u64> share_sum(beaver_product);
                    
        receiveAndXorShares(channels, share_sum, num_parties);
        timer.setTimePoint("reconstruct done");

        // check MAC
        std::vector<u64> set_union;
        for (size_t i = 0; i < num_bins; i++){
            if (share_sum[i] == 0){
                set_union.emplace_back(cuckoo_hash_table[i].mData[1]);                
            }
        }
        timer.setTimePoint("end");


        // close sockets
        const double comm = mpso::commMiB(channels);
        mpso::closeChannels(channels);
        
        std::cout << "P1 communication cost = " << std::fixed << std::setprecision(3) << comm << " MB" << std::endl;
        std::cout << timer << std::endl;
        return set_union;  

    }
    else{

        std::string opprf_path = voleFile(num_parties, num_elements, party_index, 0, "PSI");
        sendPsiOpprf(num_elements, cuckoo_seed, simple_hash_table, channels[0], opprf_values, opprf_path, num_threads);

        const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
            num_parties,
            num_elements,
            num_parties,
            "PSI64");
        mpso::preproc::multiplyWithBeaverTriples<u64>(party_index, opprf_values, channels, beaver_product, beaver_spec);

        coproto::sync_wait(channels[0].send(beaver_product));

        // close sockets
        mpso::closeChannels(channels);
        return std::vector<u64>();
    }
}


// P_0 cuckoo, others simple hash
u32 runMpsic(u32 party_index, u32 num_parties, u32 num_elements, std::vector<block> &set, u32 num_threads)
{
    u32 num_bins = mpso::numCuckooBins(num_elements);

    mpso::shuffle::XorOnline shuffle(party_index, num_parties, num_bins);
    shuffle.load("psic");

    Timer timer;
    timer.setTimePoint("start");

    auto channels = mpso::connectParties(party_index, num_parties);

    PRNG prng(sysRandomSeed());
    block cuckoo_seed = mpso::fixedCuckooSeed();
        

    // for opprf            
    std::vector<std::thread>  opprf_threads(num_parties - 1);
    std::vector<std::exception_ptr> opprf_errors(num_parties - 1);
    std::vector<std::vector<block>> simple_hash_table;
    std::vector<block> cuckoo_hash_table;
    std::vector<std::vector<u64>> opprf_vector_by_party(num_parties - 1);// for P_0
    std::vector<u64> opprf_values(num_bins);// before btMul
    std::vector<u64> beaver_product(num_bins);

    if (party_index != 0) {
        simple_hash_table = buildSimpleHashTable(set, num_bins, num_elements, cuckoo_seed);
    }

    if(party_index == 0){
        cuckoo_hash_table = buildCuckooHashTable(set, num_bins, num_elements, cuckoo_seed);

        for (u32 i = 1; i < num_parties; ++i)
        {             
            std::string opprf_path = voleFile(num_parties, num_elements, party_index, i, "PSI");
            opprf_threads[i - 1] = std::thread([&, i, opprf_path]() {
                try {
                    receivePsiOpprf(num_elements, cuckoo_seed, cuckoo_hash_table, channels[i - 1], opprf_vector_by_party[i - 1], opprf_path, num_threads);
                } catch (...) {
                    opprf_errors[i - 1] = std::current_exception();
                }
            });                               
        }
        joinThreads(opprf_threads, opprf_errors);
        timer.setTimePoint("opprf done"); 

        
        // add all the OPPRF and then ssROT values
        for (u32 i = 0; i < num_bins; ++i){
            for (u32 j = 0; j < num_parties-1; ++j){
                opprf_values[i] ^= opprf_vector_by_party[j][i];
            }
        } 


        const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
            num_parties,
            num_elements,
            num_parties,
            "PSI64");
        mpso::preproc::multiplyWithBeaverTriples<u64>(party_index, opprf_values, channels, beaver_product, beaver_spec);
        timer.setTimePoint("beaver multiplication done");        
        
        // compute share = beaver_product
        std::vector<u64> share_sum(beaver_product);
       
        coproto::sync_wait(shuffle.run(channels, share_sum));
    	timer.setTimePoint("mshuffle done");  

        receiveAndXorShares(channels, share_sum, num_parties);
        timer.setTimePoint("reconstruct done");

        // check MAC
        
        u64 intersection_count = 0;
        for (size_t i = 0; i < num_bins; i++){
            if (share_sum[i] == 0){
                intersection_count += 1;                    
            }
        }
        timer.setTimePoint("end");


        // close sockets
        const double comm = mpso::commMiB(channels);
        mpso::closeChannels(channels);
        
        std::cout << "P1 communication cost = " << std::fixed << std::setprecision(3) << comm << " MB" << std::endl;
        std::cout << timer << std::endl;
        return intersection_count;  

    }
    else{

        std::string opprf_path = voleFile(num_parties, num_elements, party_index, 0, "PSI");
        sendPsiOpprf(num_elements, cuckoo_seed, simple_hash_table, channels[0], opprf_values, opprf_path, num_threads);

        const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
            num_parties,
            num_elements,
            num_parties,
            "PSI64");
        mpso::preproc::multiplyWithBeaverTriples<u64>(party_index, opprf_values, channels, beaver_product, beaver_spec);

        // shuffle.run overwrites its input, so send a shuffled copy.
        std::vector<u64> shuffled_beaver_product(beaver_product);
        coproto::sync_wait(shuffle.run(channels, shuffled_beaver_product));
        coproto::sync_wait(channels[0].send(shuffled_beaver_product));

        // close sockets
        mpso::closeChannels(channels);
        return 0;
    }
}

// P_0 cuckoo, others simple hash
u64 runMpsics(u32 party_index, u32 num_parties, u32 num_elements, std::vector<block> &set, u32 num_threads)
{
    u32 num_bins = mpso::numCuckooBins(num_elements);

    mpso::shuffle::XorOnline indicator_shuffle(party_index, num_parties, num_bins);
    mpso::shuffle::AddOnline sum_shuffle(party_index, num_parties, num_bins);
    indicator_shuffle.load("psics1");
    sum_shuffle.load("psics2");

    Timer timer;
    timer.setTimePoint("start");

    auto channels = mpso::connectParties(party_index, num_parties);

    PRNG prng(sysRandomSeed());
    block cuckoo_seed = mpso::fixedCuckooSeed();

    // for computing cuckoo set sum
    // std::vector<u64> set64(num_bins, 0);
    BitVector bit_indi(num_bins);

    // for opprf            
    std::vector<std::thread>  opprf_threads(num_parties - 1);
    std::vector<std::exception_ptr> opprf_errors(num_parties - 1);
    std::vector<std::vector<block>> simple_hash_table;
    std::vector<block> cuckoo_hash_table;
    std::vector<std::vector<u64>> opprf_vector_by_party(num_parties - 1);// use as bit_indi
    std::vector<std::vector<u64>> sum_opprf_vector_by_party(num_parties - 1); // for item sum
    std::vector<u64> opprf_values(num_bins);// before btMul
    std::vector<u64> item_sum(num_bins);// before btMul
    std::vector<u64> beaver_product(num_bins);

    if (party_index != 0) {
        simple_hash_table = buildSimpleHashTable(set, num_bins, num_elements, cuckoo_seed);
    }

    if(party_index == 0){
        cuckoo_hash_table = buildCuckooHashTable(set, num_bins, num_elements, cuckoo_seed);

        for (u32 i = 1; i < num_parties; ++i)
        {             
            std::string opprf_path_1 = voleFile(num_parties, num_elements, party_index, i, "PSICS1");
            std::string opprf_path_2 = voleFile(num_parties, num_elements, party_index, i, "PSICS2");
            opprf_threads[i - 1] = std::thread([&, i, opprf_path_1, opprf_path_2]() {
                try {
                    receivePsiOpprf(num_elements, cuckoo_seed, cuckoo_hash_table, channels[i - 1], opprf_vector_by_party[i - 1], opprf_path_1, num_threads);
                    receiveSumOpprf(num_elements, cuckoo_seed, cuckoo_hash_table, channels[i - 1], sum_opprf_vector_by_party[i - 1], opprf_path_2, num_threads);
                } catch (...) {
                    opprf_errors[i - 1] = std::current_exception();
                }
            });                               
        }
        joinThreads(opprf_threads, opprf_errors);

        timer.setTimePoint("opprf done"); 

        
        // add all the OPPRF and then ssROT values
        for (u32 i = 0; i < num_bins; ++i){
            for (u32 j = 0; j < num_parties-1; ++j){
                opprf_values[i] ^= opprf_vector_by_party[j][i];
                item_sum[i] += sum_opprf_vector_by_party[j][i];
            }
        } 


        const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
            num_parties,
            num_elements,
            num_parties,
            "PSI64");
        mpso::preproc::multiplyWithBeaverTriples<u64>(party_index, opprf_values, channels, beaver_product, beaver_spec);

        timer.setTimePoint("beaver multiplication done");
     
        // compute share = beaver_product
        std::vector<u64> share_sum(beaver_product);     

        coproto::sync_wait(indicator_shuffle.run(channels, share_sum));
        coproto::sync_wait(sum_shuffle.run(channels, item_sum));
        
    	timer.setTimePoint("mshuffle done");         

        receiveAndXorShares(channels, share_sum, num_parties);

        // check bit_indi
        u64 intersection_sum = 0;
        for (size_t i = 0; i < num_bins; i++){
            if (share_sum[i] == 0){    
                bit_indi[i] = 1;
                intersection_sum += item_sum[i];
            }
        }


        for (u32 i = 0; i < num_parties - 1; ++i){
            coproto::sync_wait(channels[i].send(bit_indi));
        }


        std::vector<u64> received_sums(num_parties - 1);
        for (size_t i = 0; i < num_parties - 1; i++){
            coproto::sync_wait(channels[i].recv(received_sums[i]));
        }
    
        for (size_t i = 0; i < num_parties - 1; i++){
            intersection_sum += received_sums[i];
        }

        timer.setTimePoint("end");


        // close sockets
        const double comm = mpso::commMiB(channels);
        mpso::closeChannels(channels);
        
        std::cout << "P1 communication cost = " << std::fixed << std::setprecision(3) << comm << " MB" << std::endl;
        std::cout << timer << std::endl;
        return intersection_sum;
    }
    else{

        std::string opprf_path_1 = voleFile(num_parties, num_elements, party_index, 0, "PSICS1");
        std::string opprf_path_2 = voleFile(num_parties, num_elements, party_index, 0, "PSICS2");
        sendPsiOpprf(num_elements, cuckoo_seed, simple_hash_table, channels[0], opprf_values, opprf_path_1, num_threads);
        sendSumOpprf(num_elements, cuckoo_seed, simple_hash_table, channels[0], item_sum, opprf_path_2, num_threads);

        const auto beaver_spec = mpso::preproc::makeBeaverTripleSpec(
            num_parties,
            num_elements,
            num_parties,
            "PSI64");
        mpso::preproc::multiplyWithBeaverTriples<u64>(party_index, opprf_values, channels, beaver_product, beaver_spec);


        // shuffle.run overwrites its input, so send a shuffled copy.
        std::vector<u64> shuffled_beaver_product(beaver_product);
        coproto::sync_wait(indicator_shuffle.run(channels, shuffled_beaver_product));
        coproto::sync_wait(sum_shuffle.run(channels, item_sum));
        

        coproto::sync_wait(channels[0].send(shuffled_beaver_product));

        // recv bit_indi from P0
        coproto::sync_wait(channels[0].recv(bit_indi));

        u64 partial_sum = 0;
        // compute set sum, send to P0
        for (size_t i = 0; i < num_bins; i++){
            if(bit_indi[i]){
                partial_sum += item_sum[i];
            }            
        }

        coproto::sync_wait(channels[0].send(partial_sum));

        // close sockets
        mpso::closeChannels(channels);
        return 0;

    }

}

}
