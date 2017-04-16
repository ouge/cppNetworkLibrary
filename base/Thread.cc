#include "base/Thread.h"
#include "base/CurrentThread.h"
#include "base/Exception.h"
#include "base/Logging.h"
#include "base/Timestamp.h"

#include <sys/prctl.h>
#include <sys/types.h>
#include <boost/static_assert.hpp>
#include <boost/type_traits/is_same.hpp>
#include <memory>

namespace ouge::CurrentThread {

__thread int t_cachedTid = 0;
__thread char t_tidString[32];
__thread int t_tidStringLength = 6;
__thread const char* t_threadName = "unknown";
const bool sameType = boost::is_same<int, pid_t>::value;
BOOST_STATIC_ASSERT(sameType);
}

namespace ouge::detail {

// void afterFork() {
//   ouge::CurrentThread::t_cachedTid = 0;
//   ouge::CurrentThread::t_threadName = "main";
//   CurrentThread::tid();
// }

// class ThreadNameInitializer {
//  public:
//   ThreadNameInitializer() {
//     ouge::CurrentThread::t_threadName = "main";
//     CurrentThread::tid();
//     pthread_atfork(NULL, NULL, &afterFork);
//   }
// };

// ThreadNameInitializer init;

struct ThreadData {
  using ThreadFunc = ouge::Thread::ThreadFunc;
  ThreadFunc func_;
  std::string name_;
  std::weak_ptr<pid_t> wkTid_;

  ThreadData(const ThreadFunc& func, const std::string& name,
             const std::shared_ptr<pid_t>& tid)
      : func_(func), name_(name), wkTid_(tid) {}

  void runInThread() {
    pid_t tid = ouge::CurrentThread::tid();

    std::shared_ptr<pid_t> ptid = wkTid_.lock();
    if (ptid) {
      *ptid = tid;
      ptid.reset();
    }

    ouge::CurrentThread::t_threadName =
        name_.empty() ? "ougeThread" : name_.c_str();
    ::prctl(PR_SET_NAME, ouge::CurrentThread::t_threadName);
    try {
      func_();
      ouge::CurrentThread::t_threadName = "finished";
    } catch (const Exception& ex) {
      ouge::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
      abort();
    } catch (const std::exception& ex) {
      ouge::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
      fprintf(stderr, "reason: %s\n", ex.what());
      abort();
    } catch (...) {
      ouge::CurrentThread::t_threadName = "crashed";
      fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
      throw;
    }
  }
};

void* startThread(void* obj) {
  ThreadData* data = static_cast<ThreadData*>(obj);
  data->runInThread();
  delete data;
  return NULL;
}
}

namespace ouge {

void CurrentThread::cacheTid() {
  if (t_cachedTid == 0) {
    t_cachedTid = ::gettid();
    t_tidStringLength =
        snprintf(t_tidString, sizeof t_tidString, "%5d ", t_cachedTid);
  }
}

bool CurrentThread::isMainThread() { return tid() == ::getpid(); }

void CurrentThread::sleepUsec(int64_t usec) {
  struct timespec ts = {0, 0};
  ts.tv_sec = static_cast<time_t>(usec / Timestamp::kMicroSecondsPerSecond);
  ts.tv_nsec =
      static_cast<long>(usec % Timestamp::kMicroSecondsPerSecond * 1000);
  ::nanosleep(&ts, NULL);
}

AtomicInt32 Thread::numCreated_;

Thread::Thread(const ThreadFunc& func, const std::string& n)
    : started_(false),
      joined_(false),
      pthreadId_(0),
      tid_(new pid_t(0)),
      func_(func),
      name_(n) {
  setDefaultName();
}

Thread::Thread(ThreadFunc&& func, const std::string& n)
    : started_(false),
      joined_(false),
      pthreadId_(0),
      tid_(new pid_t(0)),
      func_(std::move(func)),
      name_(n) {
  setDefaultName();
}

Thread::~Thread() {
  if (started_ && !joined_) {
    pthread_detach(pthreadId_);
  }
}

void Thread::setDefaultName() {
  int num = numCreated_.incrementAndGet();
  if (name_.empty()) {
    char buf[32];
    snprintf(buf, sizeof buf, "Thread%d", num);
    name_ = buf;
  }
}

void Thread::start() {
  assert(!started_);
  started_ = true;
  detail::ThreadData* data = new detail::ThreadData(func_, name_, tid_);
  if (pthread_create(&pthreadId_, NULL, &detail::startThread, data)) {
    started_ = false;
    delete data;
    LOG_SYSFATAL << "Failed in pthread_create";
  }
}

int Thread::join() {
  assert(started_);
  assert(!joined_);
  joined_ = true;
  return pthread_join(pthreadId_, NULL);
}
}