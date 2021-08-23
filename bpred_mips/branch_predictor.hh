#ifndef __bpred_hh__
#define __bpred_hh__

#include <cstdint>
#include <ostream>
#include <map>
#include "counter2b.hh"

class sim_state;

#define BPRED_IMPL_LIST(BA) \
  BA(unknown)		    \
  BA(gshare)		    \
  BA(bimodal)		    \
  BA(gtagged)

class branch_predictor {
public:
#define ITEM(X) X,
  enum class bpred_impl {
    BPRED_IMPL_LIST(ITEM)
  };
#undef ITEM
  static const std::map<std::string, bpred_impl> bpred_impl_map;
protected:
  uint64_t &icnt;
  uint64_t n_branches;
  uint64_t n_mispredicts;
public:
  branch_predictor(uint64_t &icnt);
  virtual ~branch_predictor();
  virtual void get_stats(uint64_t &n_br, uint64_t &n_mis, uint64_t &n_inst) const;
  virtual bool predict(uint32_t, uint64_t &) const = 0;
  virtual void update(uint32_t, uint64_t, bool, bool) = 0;
  static bpred_impl lookup_impl(const std::string& impl_name);
};

class gshare : public branch_predictor {
protected:
  uint32_t lg_pht_entries = 0;
  twobit_counter_array *pht = nullptr;
public:
  gshare(uint64_t &, uint32_t);
  ~gshare();
  bool predict(uint32_t, uint64_t &) const override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};

class gtagged : public branch_predictor {
protected:
  std::map<uint64_t, uint8_t> pht;
public:
  gtagged(uint64_t &);
  ~gtagged();
  bool predict(uint32_t, uint64_t &) const override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};


class bimodal : public branch_predictor {
protected:
  uint32_t lg_c_pht_entries = 0;
  uint32_t lg_pht_entries = 0 ;
  twobit_counter_array *c_pht = nullptr;
  twobit_counter_array *t_pht = nullptr;
  twobit_counter_array *nt_pht = nullptr;
public:
  bimodal(uint64_t &,uint32_t,uint32_t);
  ~bimodal();
  bool predict(uint32_t, uint64_t &) const override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};

std::ostream &operator<<(std::ostream &, const branch_predictor&);

#ifndef KEEP_BPRED_IMPL_IMPL
#undef BPRED_IMPL_IMPL
#endif

#endif
