#ifndef __SIM_CACHE_H__
#define __SIM_CACHE_H__
#include <string>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <cassert>
#include <cstdint>
#include <array>
#include <list>
#include <unordered_set>
#include <boost/dynamic_bitset.hpp>

#include "mylist.hh"

enum class opType {READ,WRITE};

#ifndef print_var
#define print_var(x) { std::cout << #x << " = " << x << "\n"; }
#endif

class simCache {
protected:
  /* cache size stats */
  size_t bytes_per_line;
  size_t assoc;
  size_t num_sets;
  std::string name;
  int latency;
  simCache *next_level;
  size_t hits,misses;
  
  size_t total_cache_size = 0;
  size_t ln2_tag_bits = 0;
  size_t ln2_offset_bits = 0;
  size_t ln2_num_sets = 0;
  size_t ln2_bytes_per_line = 0;

  /* total stats */
  std::array<size_t,2> rw_hits;
  std::array<size_t,2> rw_misses;

  size_t max_stack_size = 0;
  mylist<uint32_t> stack;
  std::vector<uint64_t> stack_hits, stack_misses;
  void update_distance_stack(uint32_t addr, bool hit);
  
public:
  friend std::ostream &operator<<(std::ostream &out, const simCache &cache);
  simCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
	   std::string name, int latency, simCache *next_level);
  virtual ~simCache();
  
  void set_next_level(simCache *next_level);
  
  uint32_t index(uint32_t addr, uint32_t &l, uint32_t &t);
  virtual void access(uint32_t addr, uint32_t num_bytes, opType o)=0;
  
  void read(uint32_t addr, uint32_t num_bytes);
  void write(uint32_t addr, uint32_t num_bytes);

  const size_t &getHits() const {
    return hits;
  }
  const size_t &getMisses() const {
    return misses;
  }
  
  std::string getStats(std::string &fName);
  void getStats();
  double computeAMAT();

};

std::ostream &operator<<(std::ostream &out, const simCache &cache);

class randomReplacementCache : public simCache {
public:
  randomReplacementCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
			 std::string name, int latency, simCache *next_level);
  ~randomReplacementCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;
  
private:
  /* all bits are valid */
  uint8_t *allvalid;
  /* cache valid bits */
  uint8_t **valid;
  /* cache tag bits */
  uint32_t **tag;
};

class directMappedCache : public simCache {
private:
  std::vector<uint32_t> tags;
  boost::dynamic_bitset<> valid;
public:
  directMappedCache(size_t bytes_per_line, size_t assoc, size_t num_sets,
		    std::string name, int latency, simCache *next_level);
  ~directMappedCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;
};

class fullAssocCache: public simCache {
 private:
  mylist<uint32_t> entries;
  std::vector<uint64_t> hitdepth;
public:
  fullAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		std::string name, int latency, simCache *next_level) :
    simCache(bytes_per_line, assoc, num_sets, name, latency, next_level) {
    hitdepth.resize(assoc);
    std::fill(hitdepth.begin(), hitdepth.end(), 0);
  }
  ~fullAssocCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;
};

class lowAssocCache : public simCache {
 public:
  lowAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		 std::string name, int latency, simCache *next_level);
  ~lowAssocCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;

 private:
  void updateLRU(uint32_t idx,uint32_t w);
  int32_t findLRU(uint32_t w);
  
  uint8_t *allvalid;

  /* cache valid bits */
   uint64_t *valid;
   
  /* tree-lru bits */
   uint64_t *lru;
  
   /* cache tag bits */
   uint32_t **tag;

};

class setAssocCache: public simCache {
 private:
  class cacheset {
  private:
    int32_t id;
    size_t assoc;
    std::array<size_t,2> &rw_hits;
    std::array<size_t,2> &rw_misses;
    size_t &hits;
    size_t &misses;
    size_t lhits,lmisses;
    mylist<uint32_t> entries;
  public:
    cacheset(int32_t id, size_t assoc,
	     size_t &hits, size_t &misses,
	     std::array<size_t, 2> &rw_hits,
	     std::array<size_t, 2> &rw_misses) :
      id(id), assoc(assoc), hits(hits), misses(misses),
      rw_hits(rw_hits), rw_misses(rw_misses),
      lhits(0), lmisses(0) {
    }
    bool access(uint32_t tag, opType o) {
      auto it = entries.find(tag);
      if(it != entries.end()) {
	rw_hits[(opType::WRITE==o) ? 1 : 0]++;
	hits++;
	lhits++;
	entries.move_to_head(it);
	return true;
      }
      else {
	rw_misses[(opType::WRITE==o) ? 1 : 0]++;
	misses++;
	lmisses++;
    	if(entries.size() == assoc) {
	  entries.pop_back();
	}
	entries.push_front(tag);
	return false;
      }
    }
    size_t size() const {
      return entries.size();
    }
  };
  cacheset **sets;
  
public:
  setAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		std::string name, int latency, simCache *next_level);
  ~setAssocCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;
};


class fullRandAssocCache: public simCache {
 private:
  std::unordered_set<uint32_t> entries;
  std::vector<uint32_t> tags;
public:
  fullRandAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		std::string name, int latency, simCache *next_level) :
    simCache(bytes_per_line, assoc, num_sets, name, latency, next_level) {
  }
  ~fullRandAssocCache() {}
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;
};


class realLRUCache : public simCache {
 public:
  realLRUCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		 std::string name, int latency, simCache *next_level);
  ~realLRUCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;
private:
  /* all bits are valid */
  uint8_t *allvalid;
  /* cache valid bits */
   uint8_t **valid;
   /* tree-lru bits */
   uint64_t **lru;
   /* cache tag bits */
   uint32_t **tag;
};

/* This is too expensive to be used in practice */
class highAssocCache : public simCache {
 public:
  highAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		 std::string name, int latency, simCache *next_level);
  ~highAssocCache();
  void access(uint32_t addr, uint32_t num_bytes, opType o) override;

 private:
  void updateLRU(uint32_t idx,uint32_t w);
  int32_t findLRU(uint32_t w);
  
  uint8_t *allvalid;
  /* cache valid bits */
   uint8_t **valid;
   
  /* tree-lru bits */
   uint8_t **lru;
  
   /* cache tag bits */
   uint32_t **tag;

};

#endif



