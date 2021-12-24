#ifndef __statehh__
#define __statehh__

#include <cstdint>

struct state_t {
  uint32_t pc;
  uint32_t last_pc;
  int32_t gpr[32];
  int32_t lo;
  int32_t hi;
  uint32_t cpr0[32];
  uint32_t cpr1[32];
  uint32_t fcr1[5];
  uint64_t icnt;
  uint8_t *mem;
  uint8_t brk;
  uint64_t maxicnt;
};

#endif
