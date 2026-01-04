#include<iostream>
#include<vector>
#include<cstdint>
#include<map>
#include<limits>
#include<stdexcept>

template <uint32_t BlockSize>
class CacheBlock {
    public:


        uint32_t tag;
        bool valid;        //Block Validity 
        //bool dirty;
        //bool prefetch_bit;
        int repl_counter; // Counter for replacement policy

        std::vector<int> data; //Block of data

        //Default Constructor
        CacheBlock()
            : tag(0)
            , valid(false)
            , repl_counter(-1)
            , data(BlockSize, 0)
        {}

        //Constructor to create a Cache Block by passing tag, valid state, repl_counter value and data
        CacheBlock(uint32_t tag_, bool valid_, int repl_counter_, const std::vector<int>& data_)
            : tag(tag_)
            , valid(valid_)
            , repl_counter(repl_counter_)
            {
                if(data_.size() == BlockSize)
                    data = data_;
                else
                    throw std::length_error("data does not fit the block size");
            }

};


// set = list of blocks
// each block has fields - valid, repl count, data 

// LOAD
// 1. insert a block 
    //if hit - read
    //if miss - 
        // find empty spot - add
        // find invalid entries - add
        // find victim - add
// 2. delete a block
        // if invalid - delete
        // if victim - delete
// 3. invalidate a block - valid = 0
    //
// 4. find victim
// 5. remove invalid blocks - valid = 0

// Should I create another class for reporting Misses - Conflict, Compulsory, Capacity and Hits

template <uint32_t BlockSize>
class Cache {
    private:

        //Cache access count
        uint64_t access_count;
        
        //Define size and ways to define the cache
        uint32_t size;
        uint32_t ways;

        

        //each set consists of map of (tag, block)
        std::vector< std::vector< CacheBlock<BlockSize> > > sram_array;
        

        uint32_t total_blocks; // size / BlockSize
        uint32_t num_sets; // total_blocks / ways

        uint32_t offset_bits; // log2(BlockSize)
        uint32_t index_bits; // log2(num_sets)
        uint32_t tag_bits; // total address bits - (offset_bits + index_bits)

        //Cache Policies
        bool write_back; // true for write back, false for write through
        bool write_miss_allocate; // true for write miss allocate, false for write miss non allocate

    public:
        Cache(int size_, int ways_, bool write_back_ = false, bool write_miss_allocate_ = false) 
            : size(size_)
            , ways(ways_)
            , access_count(0)
            , write_back(write_back_)
            , write_miss_allocate(write_miss_allocate_)
        {

            total_blocks = size/BlockSize;
            num_sets = total_blocks/ways;

            offset_bits = __builtin_ctz(BlockSize);
            index_bits = __builtin_ctz(num_sets);
            tag_bits = 32 - (offset_bits + index_bits);

            //sram_array.assign(num_sets, std::map<uint32_t, CacheBlock<BlockSize>>{});
            //sram_array.resize(num_sets);
            sram_array.assign(num_sets, std::vector< CacheBlock<BlockSize> >(ways));

        }

        uint32_t get_tag(uint32_t address){
            return address >> (offset_bits+index_bits);
        }

        uint32_t get_index(uint32_t address){
            uint32_t index_mask = (1u << index_bits) - 1;
            return (address >> offset_bits) & index_mask;
        }

        uint32_t get_offset(uint32_t address)
        {
            uint32_t offset_mask = (1u << offset_bits) - 1;
            return address & offset_mask;
        }

        int find_victim(const std::vector< CacheBlock<BlockSize> >& set)
        {
            int min_count = std::numeric_limits<int>::max();
            int min_way = 0;
            for(int way=0; way<ways ; way++){
                auto &cache_block = set[way];
                if(!cache_block.valid){
                    return way;
                }
                else{
                    if(cache_block.repl_counter < min_count){
                        min_count = cache_block.repl_counter;
                        min_way = way;
                    }
                }
            } 
                return min_way;
        }

        std::pair<int,bool> load(uint32_t address, const std::vector<int>& data DRAM){
            uint32_t index = get_index(address);
            uint32_t tag = get_tag(address);
            uint32_t offset = get_offset(address);

            access_count++;
            
            auto &set = sram_array[index];

            for(int i=0; i<ways; i++){
                auto &cache_block = set[i];
                if(cache_block.tag == tag && cache_block.valid == true){
                    cache_block.repl_counter = access_count;
                    return {cache_block.data[offset], true};
                }
            }

            int set_size = set.size();
            if(set_size == ways){
                int way_index = find_victim(set);
                set[way_index] = CacheBlock<BlockSize>(tag, true, access_count, data DRAM);
            }
            else{
                set.push_back(CacheBlock<BlockSize>(tag, true, access_count, data DRAM));
            }
            return {data DRAM[offset], false};
        }
        
        bool store(uint32_t address, int data_proc, std::vector<int>& data DRAM){
            uint32_t index = get_index(address);
            uint32_t tag = get_tag(address);
            uint32_t offset = get_offset(address);

            access_count++;
            
            auto &set = sram_array[index];

            for(int i=0; i<ways; i++){
                auto &cache_block = set[i];
                if(cache_block.tag == tag && cache_block.valid == true){
                    cache_block.repl_counter = access_count;
                    cache_block.data[offset] = data_proc;

                    if(!write_back) data DRAM[offset] = data_proc; //write through
                    
                    return true;
                }
            }

            
            if(write_miss_allocate){

                auto data DRAM_temp = data DRAM;
                data DRAM_temp[offset] = data_proc;
                int set_size = set.size();
                if(set_size == ways){
                    int way_index = find_victim(set);
                    
                    set[way_index] = CacheBlock<BlockSize>(tag, true, access_count, data DRAM_temp);
                }
                else{
                set.push_back(CacheBlock<BlockSize>(tag, true, access_count, data DRAM_temp));
                }
            }

            if (!write_back) { 
                data DRAM[offset] = data_proc;
            }

            return false;
        }

        void debug_print() const {
            std::cout << "L1 cache state:\n";
            for (uint32_t i = 0; i < num_sets; ++i) {
                const auto &set = sram_array[i];
                std::cout << " Set[" << i << "]:";
                if (set.empty()) {
                    std::cout << " (empty)\n";
                    continue;
                }
                std::cout << "\n";
                for (const auto &blk : set) {
                    std::cout << "  tag=0x" << std::hex << blk.tag << std::dec
                              << " valid=" << blk.valid
                              << " repl=" << blk.repl_counter
                              << " data=[";
                    for (size_t w = 0; w < blk.data.size(); ++w) {
                        if (w) std::cout << ",";
                        std::cout << blk.data[w];
                    }
                    std::cout << "]\n";
                }
            }
            std::cout << std::flush;
        }

};


// Helper: size of dummy DRAM in bytes and ints
static constexpr uint32_t DRAM_BYTES  = 0x00100000;          // 1 MiB
static constexpr uint32_t WORD_SIZE = 4;                   // assume 4-byte int
static constexpr uint32_t DRAM_WORDS  = DRAM_BYTES / WORD_SIZE;

// For this test, choose BlockSize = 4 "words" (just an example)
static constexpr uint32_t TEST_BLOCK_SIZE = 4;

// Return a reference to the DRAM block that contains "address"
// This gives your Cache the per-block vector<int> it expects.
// Return a reference to the DRAM block that contains "address" (address in bytes)
std::vector<int>& get_dram_block(std::vector< std::vector<int> >& DRAM_blocks, uint32_t address) {
    // compute block index from byte address:
    uint32_t word_index = address / WORD_SIZE;
    uint32_t block_address = word_index / TEST_BLOCK_SIZE;
    if (block_address >= DRAM_blocks.size())
        throw std::out_of_range("DRAM address out of range");
    return DRAM_blocks[block_address];
}

int main() {
    using std::cout;
    using std::endl;

    // 1. Create dummy DRAM data
    // Represent DRAM as contiguous ints, then group into blocks.
    std::vector<int> DRAM_flat(DRAM_WORDS);
    for (uint32_t i = 0; i < DRAM_WORDS; ++i) {
        // Example pattern: value = address/4 (word index)
        DRAM_flat[i] = static_cast<int>(i);
    }

    // Group DRAM into blocks of TEST_BLOCK_SIZE ints.
    const uint32_t total_blocks = DRAM_WORDS / TEST_BLOCK_SIZE;
    std::vector< std::vector<int> > DRAM_blocks(total_blocks,
                                              std::vector<int>(TEST_BLOCK_SIZE));
    for (uint32_t b = 0; b < total_blocks; ++b) {
        for (uint32_t w = 0; w < TEST_BLOCK_SIZE; ++w) {
            DRAM_blocks[b][w] = DRAM_flat[b * TEST_BLOCK_SIZE + w];
        }
    }

    // Print initial DRAM contents
    std::cout << "Initial DRAM blocks:\n";
    for (uint32_t b = 0; b < 10; ++b) {
        std::cout << " Block[" << b << "]:" 
        << " addr=0x" << std::hex << b*16;
        for (uint32_t w = 0; w < TEST_BLOCK_SIZE; ++w) {
            std::cout << " " << DRAM_blocks[b][w];
        }
        std::cout << "\n";
    }
    std::cout << std::flush;

    // 2. Instantiate cache:
    //    size = 32 ints, 2-way, block_size = TEST_BLOCK_SIZE
    Cache<TEST_BLOCK_SIZE> L1D_cache(
        /*size_*/       16,
        /*ways_*/       2,
        /*write_back_*/ true,
        /*write_miss_allocate_*/ true
    );

    // Helper lambdas to drive the cache with a clear print
    auto do_load = [&](uint32_t address) {
        cout << "--------------------------------------" <<endl;
        uint32_t word_index = address / WORD_SIZE;
        cout << L1D_cache.get_tag(word_index) << " + " << L1D_cache.get_index(word_index) << " + " << L1D_cache.get_offset(word_index) <<endl;
        auto &block = get_dram_block(DRAM_blocks, address);
        std::pair<int, bool> load_value = L1D_cache.load(word_index, block);
        cout << "Load  addr=0x" << std::hex << address
             << " -> value=" << std::dec << load_value.first 
             << (load_value.second ? " (hit)" : " (miss)") << endl;
             L1D_cache.debug_print();
        cout << "--------------------------------------" <<endl<<endl;

    };

    auto do_store = [&](uint32_t address, int value) {
        cout << "--------------------------------------" <<endl;
        uint32_t word_index = address / WORD_SIZE;
        cout << L1D_cache.get_tag(word_index) << " + " << L1D_cache.get_index(word_index) << " + " << L1D_cache.get_offset(word_index) <<endl;
        auto &block = get_dram_block(DRAM_blocks, address);
        bool hit = L1D_cache.store(word_index, value, block);
        cout << "Store addr=0x" << std::hex << address
             << " value=" << std::dec << value
             << (hit ? " (hit)" : " (miss)") << endl;
             L1D_cache.debug_print();
        cout << "--------------------------------------" <<endl<<endl;

    };


    // Cold miss, load from DRAM
    do_load(0x00000000);
    // Same block, should be a hit
    do_load(0x00000001);

    // Different block, likely another set
    do_load(0x00000010);
    do_load(0x00000F8);
    do_load(0x00000014);

    // Store into existing block (hit)
    do_store(0x00000000, 1234);
    do_load(0x00000000);

    // Store to another block (potential miss-allocate)
    do_store(0x00000020, 5678);
    do_load(0x00000020);

    // Access enough different blocks to trigger eviction
    do_load(0x00000030);
    do_load(0x00000040);
    do_load(0x00000050);

    // Re-access an older block to see if it was evicted
    do_load(0x00000010);

    return 0;
}
