#include "branch_predictor.hh"
#include "globals.hh"

branch_predictor::branch_predictor() {}
branch_predictor::~branch_predictor() {}

bool branch_predictor::predict(uint32_t addr, uint64_t &idx) const {
  return false;
}
void branch_predictor::update(uint32_t addr, uint64_t idx, bool taken) {}

gshare::gshare(uint32_t lg_pht_entries) :
  branch_predictor(),
  lg_pht_entries(lg_pht_entries) {
  pht = new twobit_counter_array(1U<<lg_pht_entries);
}

gshare::~gshare() {
  delete pht;
}

bool gshare::predict(uint32_t addr, uint64_t &idx) const {
  idx = (addr>>2) ^ globals::bhr->to_integer();
  idx &= (1UL << lg_pht_entries) - 1;
  return pht->get_value(idx) > 1;
}

void gshare::update(uint32_t addr, uint64_t idx, bool taken) {
  pht->update(idx, taken);
}

bimodal::bimodal(uint32_t lg_c_pht_entries, uint32_t lg_pht_entries) :
  branch_predictor(), lg_c_pht_entries(lg_c_pht_entries), lg_pht_entries(lg_pht_entries) {
  c_pht = new twobit_counter_array(1U<<lg_c_pht_entries);
  nt_pht = new twobit_counter_array(1U<<lg_pht_entries);
  t_pht = new twobit_counter_array(1U<<lg_pht_entries);
}

bimodal::~bimodal() {
  delete c_pht;
  delete nt_pht;
  delete t_pht;
}

bool bimodal::predict(uint32_t addr, uint64_t &idx) const {
  uint32_t c_idx = (addr>>2) & ((1U<<lg_c_pht_entries)-1);
  idx = ((addr>>2) ^ globals::bhr->to_integer()) & ((1U<<lg_pht_entries)-1);
  if(c_pht->get_value(c_idx) < 2) {
    return nt_pht->get_value(idx)>1;
  }
  return t_pht->get_value(idx)>1;
}

void bimodal::update(uint32_t addr, uint64_t idx, bool taken) {
  uint32_t c_idx = (addr>>2) & ((1U<<lg_c_pht_entries)-1);
  if(c_pht->get_value(c_idx) < 2) {
    if(not(taken and (nt_pht->get_value(idx) > 1))) {
      c_pht->update(c_idx, taken);
    }
    nt_pht->update(idx, taken);
  }
  else {
    if(not(not(taken) and (t_pht->get_value(idx) < 2))) {
      c_pht->update(c_idx, taken);
    }
    t_pht->update(idx,taken);
  }
}
