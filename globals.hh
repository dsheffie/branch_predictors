#ifndef __GLOBALSH__
#define __GLOBALSH__

#include "sim_bitvec.hh"
#include "state.hh"

class branch_predictor;
class simCache;

namespace globals {
  extern bool enClockFuncts;
  extern int sysArgc;
  extern char **sysArgv;
  extern bool isMipsEL;
  extern sim_bitvec* bhr;
  extern branch_predictor *bpred;
  extern uint32_t rsb_sz;
  extern uint32_t rsb_tos;
  extern uint32_t *rsb;
  extern uint64_t num_jr_r31;
  extern uint64_t num_jr_r31_mispred;
  extern state_t *state;
  extern simCache *L1D;
  extern bool enableStackDepth;
};

#endif
