#ifndef __bpred_hh__
#define __bpred_hh__

#include <cstdint>
#include <map>
#include "counter2b.hh"

class sim_state;

class branch_predictor {
public:
  branch_predictor();
  virtual ~branch_predictor();
  virtual bool predict(uint32_t, uint64_t &) const;
  virtual void update(uint32_t, uint64_t, bool);
};

class gshare : public branch_predictor {
protected:
  uint32_t lg_pht_entries = 0;
  twobit_counter_array *pht = nullptr;
public:
  gshare(uint32_t);
  ~gshare();
  bool predict(uint32_t, uint64_t &) const override;
  void update(uint32_t addr, uint64_t idx, bool taken) override;
};


class bimodal : public branch_predictor {
protected:
  uint32_t lg_c_pht_entries = 0;
  uint32_t lg_pht_entries = 0 ;
  twobit_counter_array *c_pht = nullptr;
  twobit_counter_array *t_pht = nullptr;
  twobit_counter_array *nt_pht = nullptr;
public:
  bimodal(uint32_t,uint32_t);
  ~bimodal();
  bool predict(uint32_t, uint64_t &) const override;
  void update(uint32_t addr, uint64_t idx, bool taken) override;
};


#endif
