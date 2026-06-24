#include <algorithm>
#include <iostream>
#include <mpso/common/cuckoo_layout.h>
#include <mpso/preproc/offline_generator.h>
#include <mpso/proto/mpsi.h>

using mpso::block;
using mpso::CLP;
using mpso::u32;
using mpso::u64;

void MPSICradSum_test(u32 idx, u32 numElements, u32 numParties, u32 numThreads){
    
    u32 numBins = mpso::numCuckooBins(numElements);

    std::vector<block> set(numElements);
    for (u32 i = 0; i < numElements; i++){
        set[i] = oc::toBlock(idx + i + 1);
    }

    u64 realSum = 0;
    for (u32 i = numParties; i < numElements + 1; ++i){
        realSum += i;
    }

    if (idx == 0){   
        u64 outSum = mpso::proto::runMpsics(idx, numParties, numElements, set, numThreads);
        outSum /= numParties-1;
        if(outSum == realSum){
            u32 nn =  oc::log2ceil(numElements); 
            std::cout <<"k_" << numParties << ", nn_" << nn <<  ": MPSI Card Sum success! " << outSum << std::endl;
        }
        else{
            std::cout << "wrong sum: " << outSum << std::endl;
            std::cout << "realsum: " << realSum << std::endl;
        }
        
    } else {
        mpso::proto::runMpsics(idx, numParties, numElements, set, numThreads);
    }
    return ;
}



int main(int agrc, char** argv){
    CLP cmd;
    cmd.parse(agrc, argv);
    u32 nn = cmd.getOr("nn", 14);
    u32 n = cmd.getOr("n", 1ull << nn);
    u32 k = cmd.getOr("k", 3);
    u32 nt = cmd.getOr("nt", 1);
    u32 idx = cmd.getOr("r", -1);

    bool preGen = cmd.isSet("preGen");
    bool help = cmd.isSet("h");
    if (help){
        std::cout << "protocol: multi-party private set intersection cardinality sum" << std::endl;
        std::cout << "parameters" << std::endl;
        std::cout << "    -n:           number of elements in each set, default 1024" << std::endl;
        std::cout << "    -nn:          logarithm of the number of elements in each set, default 10" << std::endl;
        std::cout << "    -k:           number of parties, default 3" << std::endl;
        std::cout << "    -nt:          number of threads, default 1" << std::endl;
        std::cout << "    -r:           index of party" << std::endl;
        std::cout << "    -preGen:       set up offline stage" << std::endl;
        return 0;
    }    

    if ((idx >= k || idx < 0)){
        std::cout << "wrong idx of party, please use -h to print help information" << std::endl;
        return 0;
    }

    if (preGen){
        mpso::preproc::generateMpsicsOffline({idx, k, nn, nt});
    } 
    else MPSICradSum_test(idx, n, k, nt);

    return 0;
}
