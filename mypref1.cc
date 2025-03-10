#include "cache.h"
#include <algorithm>
#include <map>
#include <vector>
#include <list>
#include <cstdint>
#include <optional>

namespace {
  struct tracker {
    struct entry {
      uint64_t address = 0;
      int confidence = 0;
    };

    struct lookahead_entry {
      uint64_t address = 0;
      int degree = 0;
    };

    std::map<uint64_t, std::list<entry>> markov_table;
    std::optional<lookahead_entry> active_lookahead;
    int prefetch_counter = 0;
    constexpr static int PREFETCH_DEGREE = 10;
    constexpr static int CONFIDENCE_MAX = 1000;

    void update_markov_table(uint64_t prev_address, uint64_t curr_address) {
        if(prev_address != 0){
            if((prev_address & ~(BLOCK_SIZE - 1)) != (curr_address & ~(BLOCK_SIZE - 1))){
                if (markov_table.find(prev_address) != markov_table.end()) {
                auto& successors = markov_table[prev_address];
                auto it = std::find_if(successors.begin(), successors.end(), 
                                        [&](const entry& e) { return e.address == curr_address; });
                
                if (it != successors.end()) {
                    it->confidence = (it->confidence + 1 > CONFIDENCE_MAX) ? CONFIDENCE_MAX : it->confidence + 1;
                } else {
                    if (successors.size() < 4) {
                    successors.push_front({curr_address, 1});
                    } else {
                    auto min_it = std::min_element(successors.begin(), successors.end(), 
                                                    [](const entry& a, const entry& b) { return a.confidence < b.confidence; });
                    if (min_it != successors.end()) {
                        successors.erase(min_it);
                    }
                    successors.push_front({curr_address, 1});
                    }
                }
            

                }
                else {
                    markov_table[prev_address] = {{curr_address, 1}};
                }

                if(active_lookahead->degree == 0)
                   active_lookahead = {curr_address, PREFETCH_DEGREE};
            }
            
            
        }
    }

    void prefetch(CACHE* cache) {
      if (active_lookahead.has_value()) {
        auto [old_pf_address, degree] = active_lookahead.value();
        
        auto& succ = markov_table[old_pf_address];
      
        if (!succ.empty()) {
          auto max_it = std::max_element(succ.begin(), succ.end(), 
                                        [](const entry& a, const entry& b) { return a.confidence < b.confidence; });
          if (max_it != succ.end()) {
            auto pf_address = max_it->address;
            max_it->confidence++;
            auto pf_addr = pf_address & ~(BLOCK_SIZE - 1);
            cache->prefetch_line(pf_addr, (cache->get_mshr_occupancy_ratio() < 0.9), 0);
            active_lookahead = {pf_address, degree - 1};
            if (active_lookahead->degree == 0) {
              active_lookahead.reset();
            }  
          }
        }
        if (++prefetch_counter >= CONFIDENCE_MAX) {
          for (auto& [_, successors] : markov_table) {
            for (auto& suc : successors) {
              suc.confidence = 0;
            }
          }
          prefetch_counter = 0;
        }
      }
      
      
    }
  };
  
  std::map<CACHE*, tracker> trackers;
}

uint64_t prev_addr = 0;

void CACHE::prefetcher_initialize() {}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in) {
  ::trackers[this].update_markov_table(prev_addr, addr);
  prev_addr = addr;
  ::trackers[this].prefetch(this);
  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in) {
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {
}

void CACHE::prefetcher_final_stats() {}
