#include "branch_predictor.hh"
#include "globals.hh"

branch_predictor::branch_predictor():
  n_branches(0), n_mispredicts(0) {}

void branch_predictor::get_stats(uint64_t &n_br, uint64_t &n_mis) const {
  n_br = n_branches;
  n_mis = n_mispredicts;
}

branch_predictor::~branch_predictor() {}

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

void gshare::update(uint32_t addr, uint64_t idx, bool prediction, bool taken) {
  pht->update(idx, taken);
  n_branches++;
  n_mispredicts += (prediction != taken);
}

bimodal::bimodal(uint32_t lg_c_pht_entries, uint32_t lg_pht_entries) :
  branch_predictor(), lg_c_pht_entries(lg_c_pht_entries), lg_pht_entries(lg_pht_entries) {
  c_pht = new twobit_counter_array(1U<<lg_c_pht_entries);
  nt_pht = new twobit_counter_array(1U<<lg_pht_entries);
  t_pht = new twobit_counter_array(1U<<lg_pht_entries);
}

bimodal::~bimodal() {
  double x = static_cast<double>(c_pht->count_valid()) / static_cast<double>(c_pht->get_nentries());
  std::cout << (100.0*x) << "% of choice pht entries valid\n";
  double y = static_cast<double>(nt_pht->count_valid()) / static_cast<double>(nt_pht->get_nentries());
  std::cout << (100.0*y) << "% of not taken pht entries valid\n";
  double z = static_cast<double>(t_pht->count_valid()) / static_cast<double>(t_pht->get_nentries());
  std::cout << (100.0*z) << "% of taken pht entries valid\n";

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

void bimodal::update(uint32_t addr, uint64_t idx, bool prediction, bool taken) {
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
  n_branches++;
  n_mispredicts += (prediction != taken);
}


std::ostream &operator<<(std::ostream &out, const branch_predictor& bp) {
  uint64_t n_br=0,n_mis=0;
  bp.get_stats(n_br,n_mis);
  double br_r = static_cast<double>(n_br-n_mis) / n_br;
  out << (100.0*br_r) << "\% of branches predicted correctly";
  return out;
}
