#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "base/Thread.h"

#include "base/Copyable.h"

#include <boost/ptr_container/ptr_vector.hpp>
#include <deque>
#include <functional>
#include <mutex>
#include <condition_variable>

namespace ouge {

class ThreadPool : NonCopyable {
  public:
    using Task = std::function<void()>;

    explicit ThreadPool(const std::string& nameArg = std::string("ThreadPool"));
    ~ThreadPool();

    void setMaxQueueSize(int maxSize);
    void setThreadInitCallback(const Task& cb);
    void start(int numThreads);
    void               stop();
    const std::string& name() const { return name_; }
    size_t             queueSize() const;
    void run(const Task& f);
    void run(Task&& f);

  private:
    bool isFull() const;
    void runInThread();
    Task take();

    mutable std::mutex              mutex_;
    std::condition_variable         notEmpty_;
    std::condition_variable         notFull_;
    std::string                     name_;
    Task                            threadInitCallback_;
    boost::ptr_vector<ouge::Thread> threads_;
    std::deque<Task>                queue_;
    size_t                          maxQueueSize_;
    bool                            running_;
};
}

#endif /* THREADPOOL_H */
