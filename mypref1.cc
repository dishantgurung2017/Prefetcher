#include "cache.h"
#include <algorithm>
#include <unordered_map>
#include <map>
#include <vector>
#include <list>
#include <cstdint>
#include <optional>

#define CONFIDENCE_THRES 10

namespace{
  struct tracker{
    struct entry{
      uint64_t address = 0;
      int confidence = 0;
    };

    struct lookahead_entry{
      uint64_t address = 0;
      int degree = 0;
    };

    std::unordered_map<uint64_t, std::list<entry>> markov_table;
    std::optional<lookahead_entry> active_lookahead;
    
    constexpr static int PREFETCH_DEGREE = 100;
    // void insert(uint64_t address, entry successor){
    //    vector<entry> v = markov_table[address];
    //    if(v.size() < 4){
    //       v.push_back(successor);
    //    }
    //    else {
    //       v[0] = successor;
    //    }
    // }

    void update_markov_table(uint64_t prev_address, uint64_t curr_address) {
      if (markov_table.find(prev_address) != markov_table.end()) {
        auto& successors = markov_table[prev_address];

        auto it = successors.end(); 
        for (auto iter = successors.begin(); iter != successors.end(); ++iter) {
            if (iter->address == curr_address) {
                it = iter; 
                break;
            }
        }

        if (it != successors.end()) {
          it->confidence++;
        } 
        else {
          if(successors.size() < 4)
            successors.push_front({curr_address, 1});
          else{
              //  successors[0] = {curr_address, 1};
              successors.push_front({curr_address, 1});
              successors.pop_back(); 
          } 
        }
      } 
      else {
        markov_table[prev_address] = {{curr_address, 1}};
      }
      active_lookahead = {prev_address, PREFETCH_DEGREE};
    }


    // void prefetch(uint64_t trigger, CACHE* cache){
    //   //int max = 0;
    //   uint64_t pf_address = 0;
    //   auto it = markov_table.find(trigger);
    //   if(it != markov_table.end()){
    //       std::list<entry>& successors = it->second;
    //       for(auto& succ : successors){
    //         //if(succ.confidence > CONFIDENCE_THRES){
    //           //if(succ.confidence > max){
    //               //max = succ.confidence;
    //               pf_address = succ.address;
    //               cache->prefetch_line(pf_address, true, 0);
    //           //}
    //         //}
    //       }
    //   }
    void prefetch(CACHE* cache){
      //int max = 0;
      if(active_lookahead.has_value()){
        auto [old_pf_address, degree] = active_lookahead.value();
        auto& successors = markov_table[old_pf_address];
        for (auto iter = successors.begin(); iter != successors.end(); ++iter){
          if(iter->confidence > CONFIDENCE_THRES){
             auto pf_address = iter->address;
          
            if (cache->virtual_prefetch || (pf_address >> LOG2_PAGE_SIZE) == (old_pf_address >> LOG2_PAGE_SIZE)){
              bool success = cache->prefetch_line(pf_address, true, 0);
              if(success)
                active_lookahead = {pf_address, degree - 1};
              if(active_lookahead->degree == 0)
                active_lookahead.reset();

            }
            else active_lookahead.reset();
          }
          
        }
        
      }
    }
  };
  std::map<CACHE*, tracker> trackers;
}



void CACHE::prefetcher_initialize() {}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in)
{
  static uint64_t prev_addr = 0;
  ::trackers[this].update_markov_table(prev_addr, addr);
  prev_addr = addr;
  
  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {
  ::trackers[this].prefetch(this);
}

void CACHE::prefetcher_final_stats() {}
