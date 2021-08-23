#include <cstdio>
#include <iostream>
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
#include "helper.hh"
#include "parseMips.hh"
#include "profileMips.hh"
#include "globals.hh"
#include "sim_bitvec.hh"
#include "branch_predictor.hh"

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

static state_t *s = nullptr;

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
  bool hash = false;
  uint32_t lg_pht_sz, lg_c_pht_sz, lg_rsb_sz;
  po::options_description desc("Options");
  po::variables_map vm;

  try {
    desc.add_options() 
      ("help", "Print help messages") 
      ("args,a", po::value<std::string>(&sysArgs), "arguments to mips binary") 
      ("clock,c", po::value<bool>(&globals::enClockFuncts), "enable wall-clock")
      ("hash,h", po::value<bool>(&hash), "hash memory at end of execution")
      ("file,f", po::value<std::string>(&filename), "mips binary")
      ("maxinsns,m", po::value<uint64_t>(&maxinsns), "max instructions to execute")
      ("lg_pht_sz", po::value<uint32_t>(&lg_pht_sz)->default_value(20), "lg2(pht) sz")
      ("lg_rsb_sz", po::value<uint32_t>(&lg_rsb_sz)->default_value(2), "lg2(rsb) sz")
      ("lg_c_pht_sz", po::value<uint32_t>(&lg_c_pht_sz)->default_value(16), "lg2(choice pht) sz (bimodal predictor)")
      ("bpred_impl", po::value<std::string>(&bpred_impl), "branch predictor (string)")
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
  globals::bhr = new sim_bitvec(64);
  
  /* Build argc and argv */
  globals::sysArgc = buildArgcArgv(filename.c_str(),sysArgs,globals::sysArgv);
  initParseTables();

  int rc = posix_memalign((void**)&s, pgSize, pgSize); 
  initState(s);
  s->maxicnt = maxinsns;



  switch(branch_predictor::lookup_impl(bpred_impl))
    {
    case branch_predictor::bpred_impl::bimodal:
      globals::bpred = new bimodal(s->icnt,lg_c_pht_sz,lg_pht_sz);
      break;
    case branch_predictor::bpred_impl::gtagged:
      globals::bpred = new gtagged(s->icnt);
      break;
    case branch_predictor::bpred_impl::gshare:
    default:
      globals::bpred = new gshare(s->icnt,lg_pht_sz);
      break;
    }
  

  
#ifdef __linux__
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
#else
  void* mempt = mmap(nullptr, 1UL<<32, PROT_READ | PROT_WRITE,
		     MAP_PRIVATE | MAP_ANONYMOUS , -1, 0);
#endif
  assert(mempt != reinterpret_cast<void*>(-1));
  assert(madvise(mempt, 1UL<<32, MADV_DONTNEED)==0);
  s->mem = reinterpret_cast<uint8_t*>(mempt);
  if(s->mem == nullptr) {
    std::cerr << "INTERP : couldn't allocate backing memory!\n";
    exit(-1);
  }
  
  load_elf(filename.c_str(), s);
  mkMonitorVectors(s);

  double runtime = timestamp();
  if(globals::isMipsEL) {
    while(s->brk==0 and (s->icnt < s->maxicnt)) {
      execMipsEL(s);
    }
  }
  else {
    while(s->brk==0 and (s->icnt < s->maxicnt)) {
      execMips(s);
    }
  }
  runtime = timestamp()-runtime;
  
  if(hash) {
    std::fflush(nullptr);
    std::cerr << *s << "\n";
    std::cerr << "crc32=" << std::hex
	      << crc32(s->mem, 1UL<<32)<<std::dec
	      << "\n";
  } 
  std::cerr << KGRN << "INTERP: "
	    << runtime << " sec, "
	    << s->icnt << " ins executed, "
	    << (s->icnt/runtime)*1e-6 << "  megains / sec"
	    << KNRM  << "\n";
    
  std::cerr << KGRN << *(globals::bpred) << KNRM << "\n";

  std::cerr << "num jr r31 = " << globals::num_jr_r31 << "\n";
  std::cerr << "num mispredicted jr r31 = " << globals::num_jr_r31_mispred
	    << "\n";
  
  munmap(mempt, 1UL<<32);
  if(globals::sysArgv) {
    for(int i = 0; i < globals::sysArgc; i++) {
      delete [] globals::sysArgv[i];
    }
    delete [] globals::sysArgv;
  }
  free(s);
  delete globals::bhr;
  delete globals::bpred;
  delete [] globals::rsb;
  return 0;
}


