#ifndef __GLOBALSH__
#define __GLOBALSH__

#include "sim_bitvec.hh"

class branch_predictor;

namespace globals {
  extern bool enClockFuncts;
  extern int sysArgc;
  extern char **sysArgv;
  extern bool isMipsEL;
  extern sim_bitvec* bhr;
  extern branch_predictor *bpred;
  extern uint64_t num_br;
  extern uint64_t correct_br_pred;
};

#endif
