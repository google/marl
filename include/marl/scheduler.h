// Copyright 2019 The Marl Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef marl_scheduler_h
#define marl_scheduler_h

#include "debug.h"
#include "memory.h"
#include "mutex.h"
#include "task.h"
#include "thread.h"

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace marl {

class OSFiber;

// Scheduler asynchronously processes Tasks.
// A scheduler can be bound to one or more threads using the bind() method.
// Once bound to a thread, that thread can call marl::schedule() to enqueue
// work tasks to be executed asynchronously.
// Scheduler are initially constructed in single-threaded mode.
// Call setWorkerThreadCount() to spawn dedicated worker threads.
class Scheduler {
  class Worker;

 public:
  using TimePoint = std::chrono::system_clock::time_point;
  using Predicate = std::function<bool()>;

  Scheduler(Allocator* allocator = Allocator::Default);

  // Destructor.
  // Blocks until the scheduler is unbound from all threads before returning.
  ~Scheduler();

  // get() returns the scheduler bound to the current thread.
  static Scheduler* get();

  // bind() binds this scheduler to the current thread.
  // There must be no existing scheduler bound to the thread prior to calling.
  void bind();

  // unbind() unbinds the scheduler currently bound to the current thread.
  // There must be a existing scheduler bound to the thread prior to calling.
  // unbind() flushes any enqueued tasks on the single-threaded worker before
  // returning.
  static void unbind();

  // enqueue() queues the task for asynchronous execution.
  void enqueue(Task&& task);

  // setThreadInitializer() sets the worker thread initializer function which
  // will be called for each new worker thread spawned.
  // The initializer will only be called on newly created threads (call
  // setThreadInitializer() before setWorkerThreadCount()).
  void setThreadInitializer(const std::function<void()>& init);

  // getThreadInitializer() returns the thread initializer function set by
  // setThreadInitializer().
  const std::function<void()>& getThreadInitializer();

  // setWorkerThreadCount() adjusts the number of dedicated worker threads.
  // A count of 0 puts the scheduler into single-threaded mode.
  // Note: Currently the number of threads cannot be adjusted once tasks
  // have been enqueued. This restriction may be lifted at a later time.
  void setWorkerThreadCount(int count);

  // getWorkerThreadCount() returns the number of worker threads.
  int getWorkerThreadCount();

  // Fibers expose methods to perform cooperative multitasking and are
  // automatically created by the Scheduler.
  //
  // The currently executing Fiber can be obtained by calling Fiber::current().
  //
  // When execution becomes blocked, yield() can be called to suspend execution
  // of the fiber and start executing other pending work. Once the block has
  // been lifted, schedule() can be called to reschedule the Fiber on the same
  // thread that previously executed it.
  class Fiber {
   public:
    // current() returns the currently executing fiber, or nullptr if called
    // without a bound scheduler.
    static Fiber* current();

    // wait() suspends execution of this Fiber until the Fiber is woken up with
    // a call to notify() and the predicate pred returns true.
    // If the predicate pred does not return true when notify() is called, then
    // the Fiber is automatically re-suspended, and will need to be woken with
    // another call to notify().
    // While the Fiber is suspended, the scheduler thread may continue executing
    // other tasks.
    // lock must be locked before calling, and is unlocked by wait() just before
    // the Fiber is suspended, and re-locked before the fiber is resumed. lock
    // will be locked before wait() returns.
    // pred will be always be called with the lock held.
    // wait() must only be called on the currently executing fiber.
    void wait(marl::lock& lock, const Predicate& pred);

    // wait() suspends execution of this Fiber until the Fiber is woken up with
    // a call to notify() and the predicate pred returns true, or sometime after
    // the timeout is reached.
    // If the predicate pred does not return true when notify() is called, then
    // the Fiber is automatically re-suspended, and will need to be woken with
    // another call to notify() or will be woken sometime after the timeout is
    // reached.
    // While the Fiber is suspended, the scheduler thread may continue executing
    // other tasks.
    // lock must be locked before calling, and is unlocked by wait() just before
    // the Fiber is suspended, and re-locked before the fiber is resumed. lock
    // will be locked before wait() returns.
    // pred will be always be called with the lock held.
    // wait() must only be called on the currently executing fiber.
    template <typename Clock, typename Duration>
    inline bool wait(marl::lock& lock,
                     const std::chrono::time_point<Clock, Duration>& timeout,
                     const Predicate& pred);

    // wait() suspends execution of this Fiber until the Fiber is woken up with
    // a call to notify().
    // While the Fiber is suspended, the scheduler thread may continue executing
    // other tasks.
    // wait() must only be called on the currently executing fiber.
    //
    // Warning: Unlike wait() overloads that take a lock and predicate, this
    // form of wait() offers no safety for notify() signals that occur before
    // the fiber is suspended, when signalling between different threads. In
    // this scenario you may deadlock. For this reason, it is only ever
    // recommended to use this overload if you can guarantee that the calls to
    // wait() and notify() are made by the same thread.
    //
    // Use with extreme caution.
    inline void wait();

    // wait() suspends execution of this Fiber until the Fiber is woken up with
    // a call to notify(), or sometime after the timeout is reached.
    // While the Fiber is suspended, the scheduler thread may continue executing
    // other tasks.
    // wait() must only be called on the currently executing fiber.
    //
    // Warning: Unlike wait() overloads that take a lock and predicate, this
    // form of wait() offers no safety for notify() signals that occur before
    // the fiber is suspended, when signalling between different threads. For
    // this reason, it is only ever recommended to use this overload if you can
    // guarantee that the calls to wait() and notify() are made by the same
    // thread.
    //
    // Use with extreme caution.
    template <typename Clock, typename Duration>
    inline bool wait(const std::chrono::time_point<Clock, Duration>& timeout);

    // notify() reschedules the suspended Fiber for execution.
    // notify() is usually only called when the predicate for one or more wait()
    // calls will likely return true.
    void notify();

    // id is the thread-unique identifier of the Fiber.
    uint32_t const id;

   private:
    friend class Allocator;
    friend class Scheduler;

    enum class State {
      // Idle: the Fiber is currently unused, and sits in Worker::idleFibers,
      // ready to be recycled.
      Idle,

      // Yielded: the Fiber is currently blocked on a wait() call with no
      // timeout.
      Yielded,

      // Waiting: the Fiber is currently blocked on a wait() call with a
      // timeout. The fiber is stilling in the Worker::Work::waiting queue.
      Waiting,

      // Queued: the Fiber is currently queued for execution in the
      // Worker::Work::fibers queue.
      Queued,

      // Running: the Fiber is currently executing.
      Running,
    };

    Fiber(Allocator::unique_ptr<OSFiber>&&, uint32_t id);

    // switchTo() switches execution to the given fiber.
    // switchTo() must only be called on the currently executing fiber.
    void switchTo(Fiber*);

    // create() constructs and returns a new fiber with the given identifier,
    // stack size that will executed func when switched to.
    static Allocator::unique_ptr<Fiber> create(
        Allocator* allocator,
        uint32_t id,
        size_t stackSize,
        const std::function<void()>& func);

    // createFromCurrentThread() constructs and returns a new fiber with the
    // given identifier for the current thread.
    static Allocator::unique_ptr<Fiber> createFromCurrentThread(
        Allocator* allocator,
        uint32_t id);

    // toString() returns a string representation of the given State.
    // Used for debugging.
    static const char* toString(State state);

    Allocator::unique_ptr<OSFiber> const impl;
    Worker* const worker;
    State state = State::Running;  // Guarded by Worker's work.mutex.
  };

 private:
  Scheduler(const Scheduler&) = delete;
  Scheduler(Scheduler&&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  // Stack size in bytes of a new fiber.
  // TODO: Make configurable so the default size can be reduced.
  static constexpr size_t FiberStackSize = 1024 * 1024;

  // Maximum number of worker threads.
  static constexpr size_t MaxWorkerThreads = 256;

  // WaitingFibers holds all the fibers waiting on a timeout.
  struct WaitingFibers {
    // operator bool() returns true iff there are any wait fibers.
    inline operator bool() const;

    // take() returns the next fiber that has exceeded its timeout, or nullptr
    // if there are no fibers that have yet exceeded their timeouts.
    inline Fiber* take(const TimePoint& timeout);

    // next() returns the timepoint of the next fiber to timeout.
    // next() can only be called if operator bool() returns true.
    inline TimePoint next() const;

    // add() adds another fiber and timeout to the list of waiting fibers.
    inline void add(const TimePoint& timeout, Fiber* fiber);

    // erase() removes the fiber from the waiting list.
    inline void erase(Fiber* fiber);

    // contains() returns true if fiber is waiting.
    inline bool contains(Fiber* fiber) const;

   private:
    struct Timeout {
      TimePoint timepoint;
      Fiber* fiber;
      inline bool operator<(const Timeout&) const;
    };
    std::set<Timeout> timeouts;
    std::unordered_map<Fiber*, TimePoint> fibers;
  };

  // TODO: Implement a queue that recycles elements to reduce number of
  // heap allocations.
  using TaskQueue = std::deque<Task>;
  using FiberQueue = std::deque<Fiber*>;
  using FiberSet = std::unordered_set<Fiber*>;

  // Workers executes Tasks on a single thread.
  // Once a task is started, it may yield to other tasks on the same Worker.
  // Tasks are always resumed by the same Worker.
  class Worker {
   public:
    enum class Mode {
      // Worker will spawn a background thread to process tasks.
      MultiThreaded,

      // Worker will execute tasks whenever it yields.
      SingleThreaded,
    };

    Worker(Scheduler* scheduler, Mode mode, uint32_t id);

    // start() begins execution of the worker.
    void start() EXCLUDES(work.mutex);

    // stop() ceases execution of the worker, blocking until all pending
    // tasks have fully finished.
    void stop() EXCLUDES(work.mutex);

    // wait() suspends execution of the current task until the predicate pred
    // returns true or the optional timeout is reached.
    // See Fiber::wait() for more information.
    bool wait(marl::lock& lock, const TimePoint* timeout, const Predicate& pred)
        EXCLUDES(work.mutex);

    // wait() suspends execution of the current task until the fiber is
    // notified, or the optional timeout is reached.
    // See Fiber::wait() for more information.
    bool wait(const TimePoint* timeout) EXCLUDES(work.mutex);

    // suspend() suspends the currenetly executing Fiber until the fiber is
    // woken with a call to enqueue(Fiber*), or automatically sometime after the
    // optional timeout.
    void suspend(const TimePoint* timeout) REQUIRES(work.mutex);

    // enqueue(Fiber*) enqueues resuming of a suspended fiber.
    void enqueue(Fiber* fiber) EXCLUDES(work.mutex);

    // enqueue(Task&&) enqueues a new, unstarted task.
    void enqueue(Task&& task) EXCLUDES(work.mutex);

    // tryLock() attempts to lock the worker for task enqueing.
    // If the lock was successful then true is returned, and the caller must
    // call enqueueAndUnlock().
    bool tryLock() EXCLUDES(work.mutex) TRY_ACQUIRE(true, work.mutex);

    // enqueueAndUnlock() enqueues the task and unlocks the worker.
    // Must only be called after a call to tryLock() which returned true.
    // _Releases_lock_(work.mutex)
    void enqueueAndUnlock(Task&& task) REQUIRES(work.mutex) RELEASE(work.mutex);

    // runUntilShutdown() processes all tasks and fibers until there are no more
    // and shutdown is true, upon runUntilShutdown() returns.
    void runUntilShutdown() REQUIRES(work.mutex);

    // steal() attempts to steal a Task from the worker for another worker.
    // Returns true if a task was taken and assigned to out, otherwise false.
    bool steal(Task& out) EXCLUDES(work.mutex);

    // getCurrent() returns the Worker currently bound to the current
    // thread.
    static inline Worker* getCurrent();

    // getCurrentFiber() returns the Fiber currently being executed.
    inline Fiber* getCurrentFiber() const;

    // Unique identifier of the Worker.
    const uint32_t id;

   private:
    // run() is the task processing function for the worker.
    // run() processes tasks until stop() is called.
    void run() REQUIRES(work.mutex);

    // createWorkerFiber() creates a new fiber that when executed calls
    // run().
    Fiber* createWorkerFiber() REQUIRES(work.mutex);

    // switchToFiber() switches execution to the given fiber. The fiber
    // must belong to this worker.
    void switchToFiber(Fiber*) REQUIRES(work.mutex);

    // runUntilIdle() executes all pending tasks and then returns.
    void runUntilIdle() REQUIRES(work.mutex);

    // waitForWork() blocks until new work is available, potentially calling
    // spinForWork().
    void waitForWork() REQUIRES(work.mutex);

    // spinForWork() attempts to steal work from another Worker, and keeps
    // the thread awake for a short duration. This reduces overheads of
    // frequently putting the thread to sleep and re-waking.
    void spinForWork();

    // enqueueFiberTimeouts() enqueues all the fibers that have finished
    // waiting.
    void enqueueFiberTimeouts() REQUIRES(work.mutex);

    inline void changeFiberState(Fiber* fiber,
                                 Fiber::State from,
                                 Fiber::State to) const REQUIRES(work.mutex);

    inline void setFiberState(Fiber* fiber, Fiber::State to) const
        REQUIRES(work.mutex);

    // Work holds tasks and fibers that are enqueued on the Worker.
    struct Work {
      std::atomic<uint64_t> num = {0};  // tasks.size() + fibers.size()
      GUARDED_BY(mutex) uint64_t numBlockedFibers = 0;
      GUARDED_BY(mutex) TaskQueue tasks;
      GUARDED_BY(mutex) FiberQueue fibers;
      GUARDED_BY(mutex) WaitingFibers waiting;
      GUARDED_BY(mutex) bool notifyAdded = true;
      std::condition_variable added;
      marl::mutex mutex;

      template <typename F>
      inline void wait(F&&) REQUIRES(mutex);
    };

    // https://en.wikipedia.org/wiki/Xorshift
    class FastRnd {
     public:
      inline uint64_t operator()() {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        return x;
      }

     private:
      uint64_t x = std::chrono::system_clock::now().time_since_epoch().count();
    };

    // The current worker bound to the current thread.
    static thread_local Worker* current;

    Mode const mode;
    Scheduler* const scheduler;
    Allocator::unique_ptr<Fiber> mainFiber;
    Fiber* currentFiber = nullptr;
    Thread thread;
    Work work;
    FiberSet idleFibers;  // Fibers that have completed which can be reused.
    std::vector<Allocator::unique_ptr<Fiber>>
        workerFibers;  // All fibers created by this worker.
    FastRnd rng;
    bool shutdown = false;
  };

  // stealWork() attempts to steal a task from the worker with the given id.
  // Returns true if a task was stolen and assigned to out, otherwise false.
  bool stealWork(Worker* thief, uint64_t from, Task& out);

  // onBeginSpinning() is called when a Worker calls spinForWork().
  // The scheduler will prioritize this worker for new tasks to try to prevent
  // it going to sleep.
  void onBeginSpinning(int workerId);

  // The scheduler currently bound to the current thread.
  static thread_local Scheduler* bound;

  Allocator* const allocator;

  std::function<void()> threadInitFunc;
  mutex threadInitFuncMutex;

  std::array<std::atomic<int>, 8> spinningWorkers;
  std::atomic<unsigned int> nextSpinningWorkerIdx = {0x8000000};

  // TODO: Make this lot thread-safe so setWorkerThreadCount() can be called
  // during execution of tasks.
  std::atomic<unsigned int> nextEnqueueIndex = {0};
  unsigned int numWorkerThreads = 0;
  std::array<Worker*, MaxWorkerThreads> workerThreads;

  struct SingleThreadedWorkers {
    using WorkerByTid =
        std::unordered_map<std::thread::id, Allocator::unique_ptr<Worker>>;
    marl::mutex mutex;
    GUARDED_BY(mutex) std::condition_variable unbind;
    GUARDED_BY(mutex) WorkerByTid byTid;
  };
  SingleThreadedWorkers singleThreadedWorkers;
};

template <typename Clock, typename Duration>
bool Scheduler::Fiber::wait(
    marl::lock& lock,
    const std::chrono::time_point<Clock, Duration>& timeout,
    const Predicate& pred) {
  using ToDuration = typename TimePoint::duration;
  using ToClock = typename TimePoint::clock;
  auto tp = std::chrono::time_point_cast<ToDuration, ToClock>(timeout);
  return worker->wait(lock, &tp, pred);
}

void Scheduler::Fiber::wait() {
  worker->wait(nullptr);
}

template <typename Clock, typename Duration>
bool Scheduler::Fiber::wait(
    const std::chrono::time_point<Clock, Duration>& timeout) {
  using ToDuration = typename TimePoint::duration;
  using ToClock = typename TimePoint::clock;
  auto tp = std::chrono::time_point_cast<ToDuration, ToClock>(timeout);
  return worker->wait(&tp);
}

Scheduler::Worker* Scheduler::Worker::getCurrent() {
  return Worker::current;
}

Scheduler::Fiber* Scheduler::Worker::getCurrentFiber() const {
  return currentFiber;
}

// schedule() schedules the task T to be asynchronously called using the
// currently bound scheduler.
inline void schedule(Task&& t) {
  MARL_ASSERT_HAS_BOUND_SCHEDULER("marl::schedule");
  auto scheduler = Scheduler::get();
  scheduler->enqueue(std::move(t));
}

// schedule() schedules the function f to be asynchronously called with the
// given arguments using the currently bound scheduler.
template <typename Function, typename... Args>
inline void schedule(Function&& f, Args&&... args) {
  MARL_ASSERT_HAS_BOUND_SCHEDULER("marl::schedule");
  auto scheduler = Scheduler::get();
  scheduler->enqueue(
      std::bind(std::forward<Function>(f), std::forward<Args>(args)...));
}

// schedule() schedules the function f to be asynchronously called using the
// currently bound scheduler.
template <typename Function>
inline void schedule(Function&& f) {
  MARL_ASSERT_HAS_BOUND_SCHEDULER("marl::schedule");
  auto scheduler = Scheduler::get();
  scheduler->enqueue(Task(std::forward<Function>(f)));
}

}  // namespace marl

#endif  // marl_scheduler_h
