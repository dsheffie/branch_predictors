#ifndef __MYLIST_HH__
#define __MYLIST_HH__

#include <iterator>
#include <boost/pool/object_pool.hpp>

template <typename T>
class mylist {
private:
  class entry {
  private:
    friend class mylist;
    entry *next;
    entry *prev;
    T data;
  public:
    entry(T data) : next(nullptr), prev(nullptr), data(data) {}
    const T &getData() const {
      return data;
    }
    T &getData() {
      return data;
    }
    void set_from_tail(entry *p) {
      p->next = this;
      prev = p;
    }
    void set_from_head(entry *n) {
      n->prev = this;
      next = n;
    }
    void unlink() {
      if(next) {
	next->prev = prev;
      }
      if(prev) {
	prev->next = next;
      }
    }
    void clear() {
      prev = next = nullptr;
    }
  };
  boost::object_pool<entry> pool;
  size_t cnt;
  entry *head, *tail;
  
  entry* alloc(T v) {
    return pool.construct(v);
  }
  void free(entry* e) {
    pool.free(e);
  }
  
public:
  mylist() : cnt(0), head(nullptr), tail(nullptr) {}
  
  void push_back(T v) {
    entry* e = alloc(v);
    cnt++;
    if(head == nullptr && tail == nullptr) {
      head = tail = e;
    }
    else {
      e->set_from_tail(tail);
      tail = e;
    }
  }
  void push_front(T v) {
    entry* e = alloc(v);
    cnt++;
    if(head == nullptr && tail == nullptr) {
      head = tail = e;
    }
    else {
      e->set_from_head(head);
      head = e;
    }
  }
  void pop_back() {
    if(tail==nullptr)
      return;
    entry *ptr = tail;
    ptr->unlink();
    tail = ptr->prev;
    free(ptr);
    cnt--;
  }
  class const_iterator : public std::iterator<std::forward_iterator_tag, mylist> {
  protected:
    friend class mylist;
    entry *ptr;
    ssize_t dist;
    const_iterator(entry *ptr, ssize_t dist) : ptr(ptr), dist(dist) {};
  public:
    const_iterator() : ptr(nullptr), dist(-1) {}
    const bool operator==(const const_iterator &rhs) const {
      return ptr == rhs.ptr;
    }
    const bool operator!=(const const_iterator &rhs) const {
      return ptr != rhs.ptr;
    }
    const T &operator*() const {
      return ptr->getData();
    }
    const_iterator operator++(int postfix) {
      const_iterator it = *this;
      ptr=ptr->next;
      return it;
    }
    const_iterator operator++() {
      ptr=ptr->next;
      return *this;
    }
  };
  
  class iterator : public const_iterator {
  private:
    friend class mylist;
    iterator(entry *ptr, ssize_t dist) : const_iterator(ptr,dist) {}
  public:
    iterator() : const_iterator() {}
    T &operator*() {
      return iterator::ptr->getData();
    }
  };
  const_iterator begin() const {
    return const_iterator(head,0);
  }
  const_iterator end() const {
    return const_iterator(nullptr,-1);
  }
  iterator begin() {
    return iterator(head,0);
  }
  iterator end() {
    return iterator(nullptr,-1);
  }
  iterator find(T v) {
    entry *p = head;
    ssize_t cnt = 0;
    while(p) {
      if(p->getData() == v)
	return iterator(p,cnt);
      p = p->next;
      cnt++;
    }
    return end();
  }
  void move_to_tail(iterator it) {
    entry *ptr = it.ptr;
    if(cnt<=1)
      return;
    ptr->unlink();
    if(ptr == head) {
      head = ptr->next;
    }
    if(ptr == tail) {
      tail = ptr->prev;
    }
    ptr->clear();
    ptr->set_from_tail(tail);
    tail = ptr;
  }
  void move_to_head(iterator it) {
    entry *ptr = it.ptr;
    if(cnt<=1)
      return;
    ptr->unlink();
    if(ptr == head) {
      head = ptr->next;
    }
    if(ptr == tail) {
      tail = ptr->prev;
    }
    ptr->clear();
    ptr->set_from_head(head);
    head = ptr;
  }
  void erase(iterator it) {
    if(it == end())
      return;
    it.ptr->unlink();
    if(it.ptr == head) {
      head = it.ptr->next;
    }
    if(it.ptr == tail) {
      tail = it.ptr->prev;
    }
    free(it.ptr);
    cnt--;
  }
  ssize_t distance(iterator it) {
    return it.dist;
  }
  size_t size() const {
    return cnt;
  }
  bool empty() const {
    return cnt==0;
  }

};


#endif
