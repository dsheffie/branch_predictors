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
  //std::cerr << "addr = " << std::hex << addr << std::dec << "\n";
  //std::cerr << "lg_pht_entries = " << lg_pht_entries << "\n";
  idx = (addr>>2) ^ globals::bhr->to_integer();
  idx &= (1UL << lg_pht_entries) - 1;
  return pht->get_value(idx) > 1;
}

void gshare::update(uint32_t addr, uint64_t idx, bool taken) {
  pht->update(idx, taken);
}
