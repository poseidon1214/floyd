#ifndef FLOYD_MUTEXLOCK_H_
#define FLOYD_MUTEXLOCK_H_

#include <pthread.h>

class CondVar;

class Mutex {
 public:
  Mutex();
  ~Mutex();

  void Lock();
  void Unlock();
  void AssertHeld() {}

 private:
  friend class CondVar;
  pthread_mutex_t mu_;

  // No copying
  Mutex(const Mutex &);
  void operator=(const Mutex &);
};

class CondVar {
 public:
  explicit CondVar(Mutex *mu);
  ~CondVar();
  void Wait();
  void WaitUntil(struct timespec ts);
  void Signal();
  void SignalAll();

 private:
  pthread_cond_t cv_;
  Mutex *mu_;
};

class MutexLock {
 public:
  explicit MutexLock(Mutex *mu) : mu_(mu) { this->mu_->Lock(); }
  ~MutexLock() { this->mu_->Unlock(); }

 private:
  Mutex *const mu_;
  // No copying allowed
  MutexLock(const MutexLock &);
  void operator=(const MutexLock &);
};

class MutexUnLock {
 public:
  explicit MutexUnLock(Mutex *mu) : mu_(mu) { this->mu_->Unlock(); }
  ~MutexUnLock() { this->mu_->Lock(); }

 private:
  Mutex *const mu_;
  // No copying allowed
  MutexUnLock(const MutexUnLock &);
  void operator=(const MutexUnLock &);
};

typedef pthread_once_t OnceType;
extern void InitOnce(OnceType *once, void (*initializer)());

#endif
