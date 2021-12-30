#include <cstdio>
#include <algorithm>
#include "simCache.hh"
#include "helper.hh"
#include "globals.hh"


extern bool enableStackDepth;

std::ostream &operator<<(std::ostream &out, const simCache &cache) {
  double total = static_cast<double>(cache.hits+cache.misses);
  double rate = cache.misses / total;
  
  out << cache.name << ":\n";
  out << "total_cache_size = " << cache.total_cache_size << "\n";
  out << "bytes_per_line = " << cache.bytes_per_line << "\n";
  out << "assoc = " << cache.assoc << "\n";
  out << "num_sets = " << cache.num_sets<< "\n";
  out << "miss_rate = " << rate << "\n";
  out << "total_access = " << total  << "\n";
  out << "hits = " << cache.hits << "\n";
  out << "misses = " << cache.misses << "\n";
  out << "read_hits = "<< cache.rw_hits[0] << "\n";
  out << "read_misses = "<< cache.rw_misses[0] << "\n";
  out << "write_hits = "<< cache.rw_hits[1] << "\n";
  out << "write_misses = "<< cache.rw_misses[1] << "\n";

  if(enableStackDepth) {
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

  if(enableStackDepth) {
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

highAssocCache::highAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
			       std::string name, int latency, simCache *next_level) :
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level) {
  valid = new uint8_t*[num_sets];
  lru = new uint8_t*[num_sets];
  tag = new uint32_t*[num_sets];
  allvalid = new uint8_t[num_sets];
  for(size_t i =0; i < num_sets; i++) {
    valid[i] = new uint8_t[assoc];
    tag[i] = new uint32_t[assoc];
    lru[i] = new uint8_t[(assoc/2)+2];
    allvalid[i] = 0;
    memset(valid[i],0,sizeof(uint8_t)*assoc);
    memset(tag[i],0,sizeof(uint32_t)*assoc);
    memset(lru[i],0,sizeof(uint8_t)*((assoc/2)+2));
  }
}

highAssocCache::~highAssocCache() {
  for(size_t i =0; i < num_sets; i++) {
    delete [] valid[i];
    delete [] tag[i];
    delete [] lru[i];
  }
  delete [] lru;
  delete [] tag;
  delete [] valid;
  delete [] allvalid;
}


lowAssocCache::lowAssocCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
			     std::string name, int latency, simCache *next_level) :
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level){
  valid = new uint64_t[num_sets];
  lru = new uint64_t[num_sets];
  tag = new uint32_t*[num_sets];
  allvalid = new uint8_t[num_sets];
  for(size_t i =0; i < num_sets; i++){
    allvalid[i] = 0;
    tag[i] = new uint32_t[assoc];
    lru[i] = 0;
    valid[i] = 0;
    memset(tag[i],0,sizeof(uint32_t)*assoc);
  }
}

lowAssocCache::~lowAssocCache() {
  for(size_t i =0; i < num_sets; i++)
    delete [] tag[i];
  
  delete [] allvalid;
  delete [] lru;
  delete [] tag;
  delete [] valid;
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

void highAssocCache::updateLRU(uint32_t idx, uint32_t w)
{
  int32_t last_idx = (idx+assoc);
  int32_t lru_idx = last_idx/2;
  while(lru_idx > 0) {
    lru[w][lru_idx] = (last_idx & 0x1) ? 2 : 1;
    last_idx = lru_idx;
    lru_idx = lru_idx / 2;
  }
}

int32_t highAssocCache::findLRU(uint32_t w) {
  int32_t lru_idx = 1;
  while(true) {
    if(lru_idx >= assoc) {
      int offs = (lru_idx - assoc);
      if(offs >= assoc) {
	abort();
      }
      return offs;
    }
    
    switch(lru[w][lru_idx])
      {
      case 0:
	/* invalid line, default go left */
	lru[w][lru_idx] = 1;
	lru_idx = 2*lru_idx + 0;
	break;
      case 1:
	/* last went left */
	lru[w][lru_idx] = 2;
	lru_idx = 2*lru_idx + 1;
	break;
      case 2:
	/* last went right */
	lru[w][lru_idx] = 1;
	lru_idx = 2*lru_idx + 0;
	break;
      default:
	printf("TREE LRU broken!\n");
	abort();
	break;
      }
  }
  return -1;
}

directMappedCache::directMappedCache(size_t bytes_per_line, size_t assoc, size_t num_sets,
				     std::string name, int latency, simCache *next_level) : 
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level) {
  assert(assoc == 1);
  tags.resize(num_sets);
  valid.resize(num_sets, false);


  
}

directMappedCache::~directMappedCache(){}

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

  if(enableStackDepth) {
    update_distance_stack(addr,hit);
  }
  
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
  if(enableStackDepth) {
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

void setAssocCache::access(uint32_t addr, uint32_t num_bytes, opType o) {
  uint32_t w,t;
  uint32_t b = index(addr, w, t);
  bool hit = sets[w]->access(t,o);
  if(enableStackDepth) {
    update_distance_stack(addr, hit);
  }
}


void fullRandAssocCache::access(uint32_t addr, uint32_t num_bytes, opType o) {
  uint32_t w,t;
  uint32_t b = index(addr, w, t);
  auto it = std::find(entries.begin(), entries.end(), t);
  if(it != entries.end()) {
    hits++;
    auto d = std::distance(entries.begin(),it);
    rw_hits[(opType::WRITE==o) ? 1 : 0]++;
  }
  else {
    if(entries.size() != assoc) {
      entries.insert(t);
      tags.push_back(t);
    }
    else {
      size_t pos = rand() % assoc;
      auto pit = entries.find(tags[pos]);
      entries.erase(pit);
      tags[pos] = t;
      entries.insert(t);
    }
    misses++;
    rw_misses[(opType::WRITE==o) ? 1 : 0]++;
  }
}


void highAssocCache::access(uint32_t addr, uint32_t num_bytes, opType o)
{
  
  /* way and tag of current address */
  uint32_t w,t;
  uint32_t a = assoc+1;
  uint32_t b = index(addr, w, t);
  
  /* search cache ways */
  if((allvalid[w]))
    {
      for(size_t i = 0; i < assoc; i++)
	{
	  if(tag[w][i]==t)
	    {
	      a = i;
	      break;
	    }
	}
    }
  else
    {
      for(size_t i = 0; i < assoc; i++)
	{
	  if(valid[w][i] && (tag[w][i]==t))
	    {
	      a = i;
	      break;
	    }
	}
    }
  
  /* cache miss .. handle it */
  if( a == (assoc+1))
    {
      if(next_level)
	{
	  /* mask off to align */
	  size_t reload_addr = addr & (~(bytes_per_line-1));
	  next_level->access(reload_addr, bytes_per_line, o);
	}
      
      misses++;
      rw_misses[(opType::WRITE==o) ? 1 : 0]++;
      
      if(allvalid[w]) 
	{
	  int32_t offs = findLRU(w);
	  valid[w][offs] = 1;
	  tag[w][offs] = t;
	} 
      else
	{
	  size_t offs = 0;
	  for(size_t i = 0; i < assoc; i++)
	    {
	      if(valid[w][i]==0) {
		offs = i;
		break;
	      }
	    }
	  valid[w][offs] = 1;
	  tag[w][offs] = t;
	  updateLRU((uint32_t)offs,w);
	  uint8_t allV = 1;
	  for(size_t i = 0; i < assoc; i++)
	    {
	      allV &= valid[w][i];
	    }
	  allvalid[w] = allV;
	}
    }
  else
    {
      hits++;
      rw_hits[(opType::WRITE==o) ? 1 : 0]++;
      updateLRU(a,w);
    }
}

void lowAssocCache::updateLRU(uint32_t idx, uint32_t w)
{
  int32_t last_idx = (idx+assoc);
  int32_t lru_idx = last_idx/2;
  uint64_t t = lru[w];
  while(lru_idx > 0)
    {
      //printf("lru_idx = %d\n", lru_idx);
      uint64_t m = 1UL << lru_idx;

      if(last_idx & 0x1) {
	/* clear bit */
	t  &= ~m; 
      } else {
	/* set bit */
	t |= m;
      }
      last_idx = lru_idx;
      lru_idx = lru_idx / 2;
    }
  lru[w] = t;
}

int32_t lowAssocCache::findLRU(uint32_t w)
{
  int32_t lru_idx = 1;
  uint64_t t = lru[w];
  while(true)
    {
      if(lru_idx >= assoc)
	{
	  int offs = (lru_idx - assoc);
	  if(offs >= assoc) {
	    abort();
	  }
	  lru[w] = t;
	  return offs;
	}

      uint64_t m = 1UL << lru_idx;
      if( ((t >> lru_idx) & 0x1) ) 
	{
	  t &= ~m; 
	  /* set bit .. now clear it*/
	  lru_idx = 2*lru_idx + 0;
	} 
      else 
	{
	  t |= m;
	  /* unset bit .. now set it*/
	  lru_idx = 2*lru_idx + 1;
	}
    }
  return -1;
}

void lowAssocCache::access(uint32_t addr, uint32_t num_bytes, opType o){
  /* way and tag of current address */
  uint32_t w,t;
  uint32_t a = assoc+1;
  uint32_t b = index(addr, w, t);
  
  if((allvalid[w])) {
    for(size_t i = 0; i < assoc; i++) {
      if(tag[w][i]==t) {
	a = i;
	break;
      }
    }
  }
  else
    {
      uint64_t v = valid[w];
      while(v != 0) {
	uint32_t i = __builtin_ffsl(v)-1;
	if(tag[w][i] == t) {
	  a = i;
	  break;
	}
	v &= ( ~(1UL << i) );
      }
    }
  
  /* cache miss .. handle it */
  if( a == (assoc+1))
    {
      if(next_level)
	{
	  /* mask off to align */
	  size_t reload_addr = addr & (~(bytes_per_line-1));
	  next_level->access(reload_addr, bytes_per_line, o);
	}
      
      misses++;
      rw_misses[(opType::WRITE==o) ? 1 : 0]++;

      int32_t offs = -1;
      if((allvalid[w])) {
	offs = findLRU(w);
      }
      else {
	uint64_t nv = ~valid[w];
	offs = __builtin_ffsl(nv)-1;
	updateLRU(offs,w);
	valid[w] |= 1UL << offs;
	allvalid[w] = (__builtin_popcountl(valid[w]) == assoc);
      }
      tag[w][offs] = t;
    }
  else {
    hits++;
    rw_hits[(opType::WRITE==o) ? 1 : 0]++;
    updateLRU(a,w);
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



realLRUCache::realLRUCache(size_t bytes_per_line, size_t assoc, size_t num_sets, 
			       std::string name, int latency, simCache *next_level) :
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level)
{
  valid = new uint8_t*[num_sets];
  lru = new uint64_t*[num_sets];
  tag = new uint32_t*[num_sets];
  allvalid = new uint8_t[num_sets];

  for(size_t i =0; i < num_sets; i++)
    {
      valid[i] = new uint8_t[assoc];
      tag[i] = new uint32_t[assoc];
      lru[i] = new uint64_t[assoc];
      allvalid[i] = 0;
      memset(valid[i],0,sizeof(uint8_t)*assoc);
      memset(tag[i],0,sizeof(uint32_t)*assoc);
      memset(lru[i],0,sizeof(uint64_t)*assoc);
    }
}

realLRUCache::~realLRUCache()
{
  for(size_t i =0; i < num_sets; i++)
    {
      delete [] valid[i];
      delete [] tag[i];
      delete [] lru[i];
    }
  delete [] lru;
  delete [] tag;
  delete [] valid;
  delete [] allvalid;
}

randomReplacementCache::randomReplacementCache(size_t bytes_per_line, 
					       size_t assoc, size_t num_sets, 
					       std::string name, int latency, 
					       simCache *next_level) :
  simCache(bytes_per_line, assoc, num_sets, name, latency, next_level)
{
  valid = new uint8_t*[num_sets];
  tag = new uint32_t*[num_sets];
  allvalid = new uint8_t[num_sets];

  for(size_t i =0; i < num_sets; i++)
    {
      valid[i] = new uint8_t[assoc];
      tag[i] = new uint32_t[assoc];
      allvalid[i] = 0;
      memset(valid[i],0,sizeof(uint8_t)*assoc);
      memset(tag[i],0,sizeof(uint32_t)*assoc);
    }
}

randomReplacementCache::~randomReplacementCache()
{
  for(size_t i =0; i < num_sets; i++)
    {
      delete [] valid[i];
      delete [] tag[i];
    }
  delete [] tag;
  delete [] valid;
  delete [] allvalid;
}

void randomReplacementCache::access(uint32_t addr, uint32_t num_bytes, opType o)
{
  
  /* way and tag of current address */
  uint32_t w,t;
  uint32_t a = assoc+1;
  uint32_t b = index(addr, w, t);
  
  /* search cache ways */
  for(size_t i = 0; i < assoc; i++)
    {
      if(valid[w][i] && (tag[w][i]==t))
	{
	  a = i;
	  break;
	}
    }
  
  /* cache miss .. handle it */
  if( a == (assoc+1))
    {
      if(next_level)
	{
	  /* mask off to align */
	  size_t reload_addr = addr & (~(bytes_per_line-1));
	  next_level->access(reload_addr, bytes_per_line, o);
	}
      
      misses++;
      rw_misses[(o==opType::WRITE) ? 1 : 0]++;
      
      if(allvalid[w]) 
	{
	  uint32_t offs = rand() % assoc;
	  valid[w][offs] = 1;
	  tag[w][offs] = t;
	} 
      else
	{
	  size_t offs = 0;
	  for(size_t i = 0; i < assoc; i++)
	    {
	      if(valid[w][i]==0) {
		offs = i;
		break;
	      }
	    }
	  valid[w][offs] = 1;
	  tag[w][offs] = t;
	  uint8_t allV = 1;
	  for(size_t i = 0; i < assoc; i++)
	    {
	      allV &= valid[w][i];
	    }
	  allvalid[w] = allV;
	}
    }
  else
    {
      hits++;
      rw_hits[(opType::WRITE==o) ? 1 : 0]++;
    }
}



void realLRUCache::access(uint32_t addr, uint32_t num_bytes, opType o)
{
  
  /* way and tag of current address */
  uint32_t w,t;
  uint32_t a = assoc+1;
  uint32_t b = index(addr, w, t);
  
  /* search cache ways */
  for(size_t i = 0; i < assoc; i++)
    {
      if(valid[w][i] && (tag[w][i]==t))
	{
	  a = i;
	  break;
	}
    }
  
  /* cache miss .. handle it */
  if( a == (assoc+1))
    {
      if(next_level)
	{
	  /* mask off to align */
	  size_t reload_addr = addr & (~(bytes_per_line-1));
	  next_level->access(reload_addr, bytes_per_line, o);
	}
      
      misses++;
      rw_misses[(o==opType::WRITE) ? 1 : 0]++;
      
      if(allvalid[w]) {
	uint32_t offs = 0;
	int64_t now = globals::state->icnt;
	int64_t maxDelta = 0;
	for(uint32_t i = 0; i < assoc; i++) {
	  int64_t d = (now - lru[w][i]);
	  if(d > maxDelta) {
	    offs = i;
	    maxDelta = d;
	  }
	}
	valid[w][offs] = 1;
	tag[w][offs] = t;
      } 
      else {
	size_t offs = 0;
	for(size_t i = 0; i < assoc; i++){
	  if(valid[w][i]==0) {
	    offs = i;
	    break;
	  }
	}
	  valid[w][offs] = 1;
	  tag[w][offs] = t;
	  lru[w][offs] = globals::state->icnt;
	  uint8_t allV = 1;
	  for(size_t i = 0; i < assoc; i++)
	    {
	      allV &= valid[w][i];
	    }
	  allvalid[w] = allV;
	}
    }
  else
    {
      hits++;
      rw_hits[(o==opType::WRITE)?1:0]++;
      lru[w][a] = globals::state->icnt;
    }
}
