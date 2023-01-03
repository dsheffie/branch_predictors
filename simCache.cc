#include <cstdio>
#include <algorithm>
#include "simCache.hh"
#include "helper.hh"
#include "globals.hh"



std::ostream &operator<<(std::ostream &out, const simCache &cache) {
  double total = static_cast<double>(cache.hits+cache.misses);
  double rate = cache.misses / total;
  
  out << cache.name << ":\n";
  out << "total_cache_size = " << cache.total_cache_size << "\n";
  out << "bytes_per_line = " << cache.bytes_per_line << "\n";
  out << "assoc = " << cache.assoc << "\n";
  out << "num_sets = " << cache.num_sets<< "\n";
  out << "hit_rate = " << (1.0 - rate) << "\n";
  out << "total_access = " << (cache.hits+cache.misses)  << "\n";
  out << "hits = " << cache.hits << "\n";
  out << "misses = " << cache.misses << "\n";
  out << "read_hits = "<< cache.rw_hits[0] << "\n";
  out << "read_misses = "<< cache.rw_misses[0] << "\n";
  out << "write_hits = "<< cache.rw_hits[1] << "\n";
  out << "write_misses = "<< cache.rw_misses[1] << "\n";

  if(globals::enableStackDepth) {
    for(size_t i = 0; i < cache.max_stack_size; i++) {
      out << i << "," << cache.stack_hits.at(i) << ","
	  << (cache.stack_hits.at(i)+cache.stack_misses.at(i))
	  << "\n";
    }
  }
  
  if(cache.next_level)
    out << *(cache.next_level);
  
  return out;
}

simCache::simCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
		   std::string name, int latency, simCache *next_level) :
  bytes_per_line(bytes_per_line), assoc(assoc), num_sets(num_sets),
  name(name), latency(latency), next_level(next_level),
  hits(0), misses(0) {
  
  rw_hits.fill(0);
  rw_misses.fill(0);
  
  if(!(isPow2(bytes_per_line) && isPow2(num_sets) && isPow2(assoc))) {
    printf("JIT: all cache parameters must be a power of 2\n");
    exit(-1);
  }
  
  total_cache_size = num_sets * bytes_per_line * assoc;
  ln2_num_sets = 0;
  ln2_bytes_per_line = 0;
  while((1<<ln2_bytes_per_line) < bytes_per_line) {
    ln2_bytes_per_line++;
  }
  while((1<<ln2_num_sets) < num_sets) {
    ln2_num_sets++;
  }
  
  ln2_offset_bits = ln2_num_sets + ln2_bytes_per_line;
  ln2_tag_bits = 8*sizeof(uint32_t) - ln2_offset_bits;

  if(globals::enableStackDepth) {
    max_stack_size = num_sets*assoc*4;
    stack_hits.resize(max_stack_size);
    stack_misses.resize(max_stack_size);
  }
}

simCache::~simCache() {}


void simCache::update_distance_stack(uint32_t addr, bool hit) {
  uint32_t cl = addr >> ln2_bytes_per_line;
  auto it = stack.find(cl);
  if(it == stack.end()) {
    if(stack.size()==max_stack_size) {
      stack.pop_back();
    }
    stack.push_front(cl);
  }
  else {
    ssize_t d = stack.distance(it);
    if(hit) {
      stack_hits.at(d)++;
    }
    else {
      stack_misses.at(d)++;
    }
    stack.move_to_head(it);
  }
  
}


void simCache::set_next_level(simCache *next_level) {
  this->next_level = next_level;
}

uint32_t simCache::index(uint32_t addr, uint32_t &l, uint32_t &t) {
  //shift address by ln2_bytes_per_line
  uint32_t way_addr = addr >> ln2_bytes_per_line;
  //mask by the number of sets 
  l = way_addr & (num_sets-1);
  t = addr >> ln2_offset_bits;
  
  uint32_t byte_offs = addr & (bytes_per_line-1);

  //uint32_t nA = (t << ln2_offset_bits) | (l << ln2_bytes_per_line) | byteOffset;
  //printf("addr=%u, nA = %u\n", addr, nA);
  return byte_offs;
}


directMappedCache::directMappedCache(size_t bytes_per_line, size_t assoc, size_t num_sets,
				     std::string name, int latency, simCache *next_level) : 
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level) {
  assert(assoc == 1);
  tags.resize(num_sets);
  valid.resize(num_sets, false);


  
}

directMappedCache::~directMappedCache(){}

void directMappedCache::flush() {
  valid.reset();
}

void directMappedCache::access(uint32_t addr, uint32_t num_bytes, opType o) {
  uint32_t w,t;
  uint32_t b = index(addr, w, t);

  bool hit = false;
  if(tags[w]==t && valid[w]) {
    hits++;
    rw_hits[(opType::WRITE==o) ? 1 : 0]++;
    hit = true;
  }
  else {
    misses++;
    rw_misses[(opType::WRITE==o) ? 1 : 0]++;
    valid[w] = true;
    tags[w] = t;
    hit = false;
  }

  if(globals::enableStackDepth) {
    update_distance_stack(addr,hit);
  }
  
}

void fullAssocCache::flush() {
  entries.clear();
}

fullAssocCache::~fullAssocCache() {
  for(size_t i =0; i < assoc; i++) {
    std::cerr << "hitdepth[" << i << "] = " << hitdepth[i] << "\n";
  }
}


void fullAssocCache::access(uint32_t addr, uint32_t num_bytes, opType o) {
  uint32_t w,t;
  uint32_t b = index(addr, w, t);
  bool hit = false;
  auto it = entries.find(t);
  if(it != entries.end()) {
    hits++;
    auto d = entries.distance(it);
    hitdepth[d]++;
    rw_hits[(opType::WRITE==o) ? 1 : 0]++;
    entries.move_to_head(it);
    hit = true;
  }
  else {
    misses++;
    rw_misses[(opType::WRITE==o) ? 1 : 0]++;
    if(entries.size() == assoc) {
      entries.pop_back();
    }
    entries.push_front(t);
  }
  if(globals::enableStackDepth) {
    update_distance_stack(addr, hit);
  }
}

setAssocCache::setAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
			     std::string name, int latency, simCache *next_level) :
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level) {
  sets = new cacheset*[num_sets];
  for(size_t i = 0; i < num_sets; ++i) {
    sets[i] = new cacheset(i,assoc,hits,misses,rw_hits,rw_misses);
  }
}
setAssocCache::~setAssocCache() {
  size_t cap = 0;
  for(size_t i = 0; i < num_sets; ++i) {
    cap += bytes_per_line * sets[i]->size();
    delete sets[i];
  }
  delete [] sets;
  print_var(cap);
}

void setAssocCache::flush() {
  for(size_t i = 0; i < num_sets; ++i) {
    sets[i]->clear();
  }
}

void setAssocCache::access(uint32_t addr, uint32_t num_bytes, opType o) {
  uint32_t w,t;
  uint32_t b = index(addr, w, t);
  bool hit = sets[w]->access(t,o);
  if(globals::enableStackDepth) {
    update_distance_stack(addr, hit);
  }
}




void simCache::read(uint32_t addr, uint32_t num_bytes)
{
  access(addr,num_bytes,opType::READ);
}

void simCache::write(uint32_t addr, uint32_t num_bytes)
{
  access(addr,num_bytes,opType::WRITE);
}



void simCache::getStats() {
  std::string s;
  size_t total = hits+misses;
  double rate = ((double)misses) / ((double)total);
  s = name + ":\n";
  s += "total_cache_size = " + std::to_string(total_cache_size) + "\n";
  s += "miss_rate = " + std::to_string(rate) + "\n";
  s += "total_access = " + std::to_string(total)  + "\n";
  s += "hits = " + std::to_string(hits) + "\n";
  s += "misses = " + std::to_string(misses) + "\n";
  s += "read_hits = "+  std::to_string(rw_hits[0]) + "\n";
  s += "read_misses = "+  std::to_string(rw_misses[0]) + "\n";
  s += "write_hits = "+  std::to_string(rw_hits[1]) + "\n";
  s += "write_misses = "+  std::to_string(rw_misses[1]) + "\n";

  std::stringstream ss;
  for(size_t i = 0; i < max_stack_size; i++) {
    ss << "," << stack_hits.at(i) << ","
       << (stack_hits.at(i)+stack_misses.at(i))
       << "\n";
  }
  s += ss.str();
  
  std::cout << s << std::endl;
  if(next_level)
    next_level->getStats();
}

std::string simCache::getStats(std::string &fName) {
  std::string s;
  size_t total = hits+misses;
  double rate = ((double)misses) / ((double)total);
  
  fName = name + ".txt";
  s = name + ":\n";
  s += "total_cache_size = " + std::to_string(total_cache_size) + "\n";
  s += "miss_rate = " + std::to_string(rate) + "\n";
  s += "total_access = " + std::to_string(total)  + "\n";
  s += "hits = " + std::to_string(hits) + "\n";
  s += "misses = " + std::to_string(misses) + "\n";
  
  s += "read_hits = "+  std::to_string(rw_hits[0]) + "\n";
  s += "read_misses = "+  std::to_string(rw_misses[0]) + "\n";
  s += "write_hits = "+  std::to_string(rw_hits[1]) + "\n";
  s += "write_misses = "+  std::to_string(rw_misses[1]) + "\n";
  
  return s;
}


double simCache::computeAMAT() {
  size_t total = hits+misses;
  double rate = ((double)misses) / ((double)total);
  double nextLevelLat = 100.0;
  if(next_level)
    nextLevelLat = next_level->computeAMAT();
  
  double x = (nextLevelLat * rate)  + 
    ((double)latency * (1.0 - rate));
  
  /* printf("AMAT = %g\n", x); */
  
  return x;
}


