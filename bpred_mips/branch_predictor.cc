#define KEEP_BPRED_IMPL_IMPL
#include "branch_predictor.hh"
#include "globals.hh"


branch_predictor::branch_predictor(uint64_t &icnt):
  icnt(icnt), n_branches(0), n_mispredicts(0) {}

void branch_predictor::get_stats(uint64_t &n_br,
				 uint64_t &n_mis,
				 uint64_t &n_insns) const {
  n_br = n_branches;
  n_mis = n_mispredicts;
  n_insns = icnt;
}

branch_predictor::~branch_predictor() {}

gshare::gshare(uint64_t &icnt, uint32_t lg_pht_entries) :
  branch_predictor(icnt),
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

gtagged::gtagged(uint64_t &icnt) :
  branch_predictor(icnt) {}
gtagged::~gtagged() {}

bool gtagged::predict(uint32_t addr, uint64_t &idx) const {
  uint64_t hbits = static_cast<uint64_t>(globals::bhr->to_integer());
  hbits &= ((1UL<<32)-1);
  hbits <<= 32;
  idx = (addr>>2) | hbits;
  const auto it = pht.find(idx);
  if(it == pht.cend()) {
    return false;
  }
  return (it->second > 1);
}

void gtagged::update(uint32_t addr, uint64_t idx, bool prediction, bool taken) {
  uint8_t &e = pht[idx];  
  if(taken) {
    e = (e==3) ? 3 : (e + 1);
  }
  else {
    e = (e==0) ? 0 : e-1;
  }
  n_branches++;
  n_mispredicts += (prediction != taken);
}



bimodal::bimodal(uint64_t &icnt, uint32_t lg_c_pht_entries, uint32_t lg_pht_entries) :
  branch_predictor(icnt), lg_c_pht_entries(lg_c_pht_entries), lg_pht_entries(lg_pht_entries) {
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
  uint64_t n_br=0,n_mis=0, icnt = 0;
  bp.get_stats(n_br,n_mis,icnt);
  double br_r = static_cast<double>(n_br-n_mis) / n_br;
  out << (100.0*br_r) << "\% of branches predicted correctly\n";
  out << 1000.0 * (static_cast<double>(n_mis) / icnt)
      << " mispredicts per kilo insn\n";
  return out;
}


branch_predictor::bpred_impl branch_predictor::lookup_impl(const std::string& impl_name) {
  auto it = bpred_impl_map.find(impl_name);
  if(it == bpred_impl_map.end()) {
    return branch_predictor::bpred_impl::unknown;
  }
  return it->second;
}

#define PAIR(X) {#X, branch_predictor::bpred_impl::X},
const std::map<std::string, branch_predictor::bpred_impl> branch_predictor::bpred_impl_map = {
  BPRED_IMPL_LIST(PAIR)
};
#undef PAIR
