#ifndef __bpred_hh__x
#define __bpred_hh__

#include <cstdint>
#include <ostream>
#include <map>
#include <unordered_map>
#include "counter2b.hh"
#include "sim_bitvec.hh"

#define BPRED_IMPL_LIST(BA) \
  BA(unknown)		    \
  BA(gshare)		    \
  BA(bimodal)		    \
  BA(gtagged)		    \
  BA(uberhistory)	    \
  BA(tage)

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
  std::map<uint32_t, uint64_t> mispredict_map;
public:
  branch_predictor(uint64_t &icnt);
  virtual ~branch_predictor();
  virtual void get_stats(uint64_t &n_br, uint64_t &n_mis, uint64_t &n_inst) const;
  virtual bool predict(uint32_t, uint64_t &)  = 0;
  virtual void update(uint32_t, uint64_t, bool, bool) = 0;
  virtual const char* getTypeString() const =  0;
  static bpred_impl lookup_impl(const std::string& impl_name);
  const std::map<uint32_t, uint64_t> &getMap() const {
    return mispredict_map;
  }
  std::map<uint32_t, uint64_t> &getMap() {
    return mispredict_map;
  }
};

class gshare : public branch_predictor {
protected:
  constexpr static const char* typeString = "gshare";  
  uint32_t lg_pht_entries = 0;
  uint32_t pc_shift = 0;
  twobit_counter_array *pht = nullptr;
public:
  gshare(uint64_t & icnt, uint32_t lg_pht_entries, uint32_t pc_shift = 0);
  ~gshare();
  const char* getTypeString() const override {
    return typeString;
  }
  bool predict(uint32_t, uint64_t &) override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};


class tage : public branch_predictor {
  struct tage_entry {
    uint32_t pred : 2;
    uint32_t useful : 2;
    uint32_t tag : 12;
    void clear() {
      pred = 0;
      useful = 0;
      tag = 0;
    }
  };
protected:
  constexpr static const char* typeString = "tage";
  
  const int table_lengths[5] =  {256,128,64,32,16};
  tage_entry *tage_tables[5] = {nullptr};
  static const int n_tables = 5;
  uint64_t hashes[5] = {0};

  uint64_t pred_table[6] = {0};
  uint64_t corr_pred_table[6] = {0};
  
  uint32_t lg_pht_entries = 0;
  twobit_counter_array *pht = nullptr;

  static uint32_t pc_hash(uint32_t pc) {
    return (pc >> 2) & ((1U<<12)-1);
  }
  
public:
  tage(uint64_t & icnt, uint32_t lg_pht_entries);
  ~tage();
  const char* getTypeString() const override {
    return typeString;
  }
  bool predict(uint32_t, uint64_t &) override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};


class gtagged : public branch_predictor {
protected:
  constexpr static const char* typeString = "gtagged";  
  std::map<uint64_t, uint8_t> pht;
public:
  gtagged(uint64_t &);
  ~gtagged();
  const char* getTypeString() const override {
    return typeString;
  }  
  bool predict(uint32_t, uint64_t &) override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};


class bimodal : public branch_predictor {
protected:
  constexpr static const char* typeString = "bimodal";
  uint32_t lg_c_pht_entries = 0;
  uint32_t lg_pht_entries = 0 ;
  twobit_counter_array *c_pht = nullptr;
  twobit_counter_array *t_pht = nullptr;
  twobit_counter_array *nt_pht = nullptr;
public:
  bimodal(uint64_t &,uint32_t,uint32_t);
  ~bimodal();
  const char *getTypeString() const override {
    return typeString;    
  }
  bool predict(uint32_t, uint64_t &) override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};

class uberhistory : public branch_predictor {
protected:
  constexpr static const char* typeString = "uberhistory";
  uint32_t lg_history_entries;
  struct entry {
    uint32_t pc;
    uint8_t p;
    bool v;
  };
  entry *history_table;
public:
  uberhistory(uint64_t &, uint32_t);
  ~uberhistory();
  const char* getTypeString() const override {
    return typeString;
  }
  bool predict(uint32_t, uint64_t &) override;
  void update(uint32_t addr, uint64_t idx, bool prediction, bool taken) override;
};

std::ostream &operator<<(std::ostream &, const branch_predictor&);

#ifndef KEEP_BPRED_IMPL_IMPL
#undef BPRED_IMPL_IMPL
#endif

#endif
