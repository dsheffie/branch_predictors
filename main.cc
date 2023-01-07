#include <cstdio>
#include <iostream>
#include <fstream>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <signal.h>
#include <cxxabi.h>

#include <cstdint>
#include <cstdlib>
#include <string>
#include <cstring>
#include <cassert>
#include <boost/program_options.hpp>

#include "loadelf.hh"
#include "saveState.hh"
#include "helper.hh"
#include "parseMips.hh"
#include "profileMips.hh"
#include "globals.hh"
#include "sim_bitvec.hh"
#include "branch_predictor.hh"
#include "simCache.hh"

extern const char* githash;

char **globals::sysArgv = nullptr;
int globals::sysArgc = 0;
bool globals::enClockFuncts = false;
bool globals::isMipsEL = false;
uint32_t globals::rsb_sz = 0;
uint32_t globals::rsb_tos = 0;
uint32_t * globals::rsb = nullptr;
uint64_t globals::num_jr_r31 = 0;
uint64_t globals::num_jr_r31_mispred = 0;

sim_bitvec* globals::bhr = nullptr;
branch_predictor* globals::bpred = nullptr;
state_t* globals::state = nullptr;
simCache* globals::L1D = nullptr;
bool globals::enableStackDepth = false;

template<typename X, typename Y>
static inline void dump_histo(const std::string &fname,
			      const std::map<X,Y> &histo,
			      const state_t *s) {
  std::vector<std::pair<X,Y>> sorted_by_cnt;
  for(auto &p : histo) {
    sorted_by_cnt.emplace_back(p.second, p.first);
  }
  std::ofstream out(fname);
  std::sort(sorted_by_cnt.begin(), sorted_by_cnt.end());
  for(auto it = sorted_by_cnt.rbegin(), E = sorted_by_cnt.rend(); it != E; ++it) {
    uint32_t r_inst = *reinterpret_cast<uint32_t*>(globals::state->mem + it->second);
    r_inst = bswap<false>(r_inst);	
    auto s = getAsmString(r_inst, it->second);
    out << std::hex << it->second << ":"
  	      << s << ","
  	      << std::dec << it->first << "\n";
  }
  out.close();
}


static int buildArgcArgv(const char *filename, const std::string &sysArgs, char **&argv){
  int cnt = 0;
  std::vector<std::string> args;
  char **largs = 0;
  args.push_back(std::string(filename));

  char *ptr = nullptr;
  char *c_str = strdup(sysArgs.c_str());
  if(sysArgs.size() != 0)
    ptr = strtok(c_str, " ");

  while(ptr && (cnt<MARGS)) {
    args.push_back(std::string(ptr));
    ptr = strtok(nullptr, " ");
    cnt++;
  }
  largs = new char*[args.size()];
  for(size_t i = 0; i < args.size(); i++) {
    const std::string & s = args[i];
    size_t l = strlen(s.c_str());
    largs[i] = new char[l+1];
    memset(largs[i],0,sizeof(char)*(l+1));
    memcpy(largs[i],s.c_str(),sizeof(char)*l);
  }
  argv = largs;
  free(c_str);
  return (int)args.size();
}

int main(int argc, char *argv[]) {
  bool bigEndianMips = true;
  namespace po = boost::program_options; 

  std::cerr << KGRN
	    << "MIPS INTERP : built "
	    << __DATE__ << " " << __TIME__
    	    << ",hostname="<<gethostname()
	    << ",pid="<< getpid() << "\n"
    	    << "git hash=" << githash
	    << KNRM << "\n";
  
  size_t pgSize = getpagesize();
  std::string sysArgs, filename, bpred_impl;
  uint64_t maxinsns = ~(0UL);
  bool hash = false,loaddump = false;
  int32_t assoc, l1d_sets, line_len;
  size_t bhr_len;
  uint32_t lg_pht_sz, lg_c_pht_sz, lg_rsb_sz,pc_shift;
  po::options_description desc("Options");
  po::variables_map vm;
  
  try {
    desc.add_options() 
      ("help", "Print help messages") 
      ("args,a", po::value<std::string>(&sysArgs), "arguments to mips binary") 
      ("clock,c", po::value<bool>(&globals::enClockFuncts), "enable wall-clock")
      ("dump,d", po::value<bool>(&loaddump)->default_value(false), "load a binary blob")
      ("file,f", po::value<std::string>(&filename), "mips binary")      
      ("hash,h", po::value<bool>(&hash), "hash memory at end of execution")
      ("maxicnt,m", po::value<uint64_t>(&maxinsns), "max instructions to execute")
      ("bhr_len", po::value<size_t>(&bhr_len)->default_value(64), "branch history length")
      ("lg_pht_sz", po::value<uint32_t>(&lg_pht_sz)->default_value(16), "lg2(pht) sz")
      ("lg_rsb_sz", po::value<uint32_t>(&lg_rsb_sz)->default_value(2), "lg2(rsb) sz")
      ("lg_c_pht_sz", po::value<uint32_t>(&lg_c_pht_sz)->default_value(16), "lg2(choice pht) sz (bimodal predictor)")
      ("bpred_impl", po::value<std::string>(&bpred_impl), "branch predictor (string)")
      ("assoc", po::value<int32_t>(&assoc)->default_value(-1), "cache associativity")
      ("sets", po::value<int32_t>(&l1d_sets)->default_value(64), "cache sets")
      ("line_len", po::value<int32_t>(&line_len)->default_value(16), "cache line length")
      ("pc_shift", po::value<uint32_t>(&pc_shift)->default_value(0), "shift dist pc in gshare")
      ; 
    po::store(po::parse_command_line(argc, argv, desc), vm);
    po::notify(vm); 
  }
  catch(po::error &e) {
    std::cerr << KRED << "command-line error : " << e.what() << KNRM << "\n";
    return -1;
  }

  if(vm.count("help")) {
    std::cout << desc << "\n";
    return 0;
  }

  globals::rsb_sz = 1U << lg_rsb_sz;
  globals::rsb = new uint32_t[globals::rsb_sz];
  globals::rsb_tos = (globals::rsb_sz - 1) & (globals::rsb_sz - 1);
  
  memset(globals::rsb, 0, sizeof(uint32_t)*globals::rsb_sz);
  
  if(filename.size()==0) {
    std::cerr << "INTERP : no file\n";
    return -1;
  }
   
  /* Build argc and argv */
  globals::sysArgc = buildArgcArgv(filename.c_str(),sysArgs,globals::sysArgv);
  initParseTables();

  int rc = posix_memalign((void**)&globals::state, pgSize, pgSize); 
  initState(globals::state);
  globals::state->maxicnt = maxinsns;



  switch(branch_predictor::lookup_impl(bpred_impl))
    {
    case branch_predictor::bpred_impl::bimodal:
      globals::bpred = new bimodal(globals::state->icnt,lg_c_pht_sz,lg_pht_sz);
      break;      
    case branch_predictor::bpred_impl::gtagged:
      globals::bpred = new gtagged(globals::state->icnt);
      break;
    case branch_predictor::bpred_impl::uberhistory:
      globals::bpred = new uberhistory(globals::state->icnt,lg_pht_sz);
      break;
    case branch_predictor::bpred_impl::gshare:
      globals::bpred = new gshare(globals::state->icnt,lg_pht_sz,pc_shift);
      break;
    case branch_predictor::bpred_impl::tage:
    default:      
      globals::bpred = new tage(globals::state->icnt,lg_pht_sz);
      break;            
    }

  if(globals::bpred->needed_history_length()) {
    bhr_len = globals::bpred->needed_history_length();
  }
  globals::bhr = new sim_bitvec(bhr_len);
  
  
#ifdef __linux__
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
#else
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS , -1, 0);
#endif
  assert(mempt != reinterpret_cast<void*>(-1));
  assert(madvise(mempt, 1UL<<32, MADV_DONTNEED)==0);
  globals::state->mem = reinterpret_cast<uint8_t*>(mempt);
  if(globals::state->mem == nullptr) {
    std::cerr << "INTERP : couldn't allocate backing memory!\n";
    exit(-1);
  }

  if(loaddump) {
    loadState(*globals::state, filename.c_str());
    globals::state->icnt = 0;
    //std::cout << "state loaded with " << globals::state->icnt << " executed insns\n";
 }
 else {
   load_elf(filename.c_str(), globals::state);
   mkMonitorVectors(globals::state);
 }
  if(assoc <= -0) {
    globals::L1D = new simCache(line_len, 1, l1d_sets, "l1D", 1, nullptr);
  }
  else if(assoc==1) {
    globals::L1D = new directMappedCache(line_len, 1, l1d_sets, "l1D", 1, nullptr);
  }
  else if(l1d_sets==1) {
    globals::L1D = new fullAssocCache(line_len, assoc, 1, "l1D", 1, nullptr);
  }
  else {
    globals::L1D = new setAssocCache(line_len, assoc, l1d_sets,  "l1D", 1, nullptr);
  }
  
  double runtime = timestamp();
  if(globals::isMipsEL) {
    while(globals::state->brk==0 and (globals::state->icnt < globals::state->maxicnt)) {
      execMipsEL(globals::state);
    }
  }
  else {
    while(globals::state->brk==0 and (globals::state->icnt < globals::state->maxicnt)) {
      execMips(globals::state);
    }
  }
  runtime = timestamp()-runtime;
  
  if(hash) {
    std::fflush(nullptr);
    std::cerr << *globals::state << "\n";
    std::cerr << "crc32=" << std::hex
	      << crc32(globals::state->mem, 1UL<<32)<<std::dec
	      << "\n";
  } 
  std::cerr << KGRN << "INTERP: "
	    << runtime << " sec, "
	    << globals::state->icnt << " ins executed, "
	    << (globals::state->icnt/runtime)*1e-6 << "  megains / sec"
	    << KNRM  << "\n";
    
  std::cerr <<  *(globals::bpred) << "\n";

  std::cerr << "num jr r31 = " << globals::num_jr_r31 << "\n";
  std::cerr << "num mispredicted jr r31 = " << globals::num_jr_r31_mispred
	    << "\n";

  dump_histo("mispredicts.txt", globals::bpred->getMap(), globals::state);
  
  munmap(mempt, 1UL<<32);
  if(globals::sysArgv) {
    for(int i = 0; i < globals::sysArgc; i++) {
      delete [] globals::sysArgv[i];
    }
    delete [] globals::sysArgv;
  }
  free(globals::state);
  delete globals::bhr;
  delete globals::bpred;
  delete [] globals::rsb;
  delete globals::L1D;

  return 0;
}


