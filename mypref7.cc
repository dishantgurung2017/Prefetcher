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
      int cur_conf = 0;
    };

    std::map<uint64_t, std::list<entry>> markov_table;
    std::list<lookahead_entry> lookahead_list;
    std::list<lookahead_entry> lookahead_list_temp;
    constexpr static int PREFETCH_DEGREE = 3;
    constexpr static int CONFIDENCE_MAX = 1000;
    constexpr static int LOOKAHEAD_QUEUE_SIZE = 16;
    constexpr static int CONFIDENCE_THRESHOLD = 100;
    constexpr static bool PREFETCH_INTERMEDIATE = false;

    void update_markov_table(CACHE* cache, uint64_t prev_address, uint64_t curr_address) {
      if (prev_address != 0) {
        if ((prev_address & ~(BLOCK_SIZE - 1)) != (curr_address & ~(BLOCK_SIZE - 1))) {
          auto& successors = markov_table[prev_address];
          auto it = std::find_if(successors.begin(), successors.end(),
                                 [&](const entry& e) { return e.address == curr_address; });
          
          if (it != successors.end()) {
            it->confidence = std::min(it->confidence + 1, CONFIDENCE_MAX);
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

          if (lookahead_list.size() >= LOOKAHEAD_QUEUE_SIZE) {
            auto entry1 = lookahead_list.front();
            lookahead_list.pop_front();
            cache->prefetch_line(entry1.address, (cache->get_mshr_occupancy_ratio() < 0.9), 0);
          }
          lookahead_list.push_back({curr_address, PREFETCH_DEGREE, it->confidence});
        }
      }
    }

    void prefetch(CACHE* cache) {
      for (auto it = lookahead_list.begin(); it != lookahead_list.end(); ) {
        if (markov_table.find(it->address) != markov_table.end()) {
          auto& successors = markov_table[it->address];
          auto max_it = std::max_element(successors.begin(), successors.end(),
                                         [](const entry& a, const entry& b) { return a.confidence < b.confidence; });
          if (max_it != successors.end()) {
            auto pf_address = max_it->address;
            int new_conf = (it->cur_conf * max_it->confidence) / CONFIDENCE_MAX;
            
            if (PREFETCH_INTERMEDIATE) {
              cache->prefetch_line(pf_address, (cache->get_mshr_occupancy_ratio() < 0.9), 0);
            }

            if (it->degree > 0) {
              lookahead_list_temp.push_back({pf_address, it->degree - 1, new_conf});
            }
            if (it->degree == 0 || it->cur_conf < CONFIDENCE_THRESHOLD) {
                cache->prefetch_line(it->address, (cache->get_mshr_occupancy_ratio() < 0.9), 0);
                lookahead_list.erase(it);
            } else {
                ++it;  
            }
          }
        }
      }
      for(auto it = lookahead_list_temp.begin(); it != lookahead_list_temp.end(); it++){
        lookahead_list.push_back(*it);
        if (lookahead_list.size() >= LOOKAHEAD_QUEUE_SIZE) {
            auto entry1 = lookahead_list.front();
            lookahead_list.pop_front();
            cache->prefetch_line(entry1.address, (cache->get_mshr_occupancy_ratio() < 0.9), 0);
          }
      }
      lookahead_list_temp.clear();
    }
  };

  std::map<CACHE*, tracker> trackers;
}

uint64_t prev_addr = 0;

void CACHE::prefetcher_initialize() {}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, uint8_t cache_hit, bool useful_prefetch, uint8_t type, uint32_t metadata_in) {
  ::trackers[this].update_markov_table(this, prev_addr, addr);
  prev_addr = addr;
  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, uint8_t prefetch, uint64_t evicted_addr, uint32_t metadata_in) {
  return metadata_in;
}

void CACHE::prefetcher_cycle_operate() {
  ::trackers[this].prefetch(this);
}

void CACHE::prefetcher_final_stats() {}
