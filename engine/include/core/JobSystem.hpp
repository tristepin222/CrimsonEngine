#pragma once
#include <vector>
#include <thread>
#include <functional>
#include <atomic>
#include <memory>
#include <semaphore>
#include <mutex>
#include <exception>
#include <cstdint>

namespace Engine {

    /**
     * @struct JobCounter
     * @brief Synchronization counter for tracking job completion and propagating exceptions.
     */
    struct JobCounter {
        std::atomic<int> remaining{0};
        std::atomic<bool> hasException{false};
        std::exception_ptr exception{nullptr};
        std::mutex exceptionMutex;

        bool isComplete() const {
            return remaining.load(std::memory_order_acquire) <= 0;
        }

        void recordException(std::exception_ptr eptr) {
            if (!eptr) return;
            std::lock_guard<std::mutex> lock(exceptionMutex);
            if (!hasException.load(std::memory_order_relaxed)) {
                exception = eptr;
                hasException.store(true, std::memory_order_release);
            }
        }
    };

    /** @brief Lightweight shared handle representing an asynchronous job or batch of jobs. */
    using JobHandle = std::shared_ptr<JobCounter>;

    /**
     * @class JobSystem
     * @brief High-performance lock-free thread pool scheduler for concurrent work distribution.
     */
    class JobSystem {
    public:
        /**
         * @brief Get the singleton instance.
         */
        static JobSystem& getInstance() {
            static JobSystem instance;
            return instance;
        }

        /**
         * @brief Spawns worker threads and allocates lock-free ring-buffer queue.
         * @param threadCount Number of worker threads. Defaults to std::thread::hardware_concurrency() - 1.
         * @param queueCapacity Capacity of the bounded MPMC queue. Will be rounded up to the nearest power of 2 (min 64).
         */
        void initialize(unsigned int threadCount = 0, size_t queueCapacity = 65536);

        /**
         * @brief Shuts down all worker threads and waits for them to join.
         */
        void shutdown();

        /**
         * @brief Enqueues a single void-returning task and returns a completion handle.
         * @param job Callable task closure.
         * @return JobHandle to track task completion or wait on it.
         */
        JobHandle pushJob(std::function<void()> job);

        /**
         * @brief Enqueues a batch of void-returning tasks and returns a single combined completion handle.
         * @param jobs Vector of callable tasks.
         * @return JobHandle tracking the batch.
         */
        JobHandle pushJobs(const std::vector<std::function<void()>>& jobs);

        /**
         * @brief Waits for a job handle to complete, cooperatively executing queued jobs on the calling thread.
         * If an exception was thrown in any task belonging to this handle, it is rethrown to the caller.
         * @param handle The JobHandle to wait on.
         */
        void wait(const JobHandle& handle);

        /**
         * @brief Checks if a job handle has finished executing.
         * @param handle The JobHandle to check.
         * @return True if completed or null.
         */
        bool isComplete(const JobHandle& handle) const;

        /**
         * @brief Splits a total loop count into chunks and evaluates them in parallel across worker threads.
         * 
         * @note Exception Semantics & Partial Execution:
         * parallelFor executes chunks concurrently across worker threads. If one or more iterations throw
         * an exception, the first captured exception will be rethrown to the caller upon completion in wait().
         * Execution is NON-TRANSACTIONAL and PARTIAL: iterations executed before or concurrently with the failure
         * will have completed their side-effects, while subsequent iterations may not run.
         * Callers must not assume parallelFor can be cleanly retried without accounting for partial execution.
         * 
         * @param count Number of iterations [0, count).
         * @param func Callback taking the current index.
         * @param chunkSize Optional custom chunk size. If <= 0, automatically computed for optimal cache locality.
         */
        void parallelFor(int count, std::function<void(int)> func, int chunkSize = 0);

        /**
         * @brief Returns active thread count.
         */
        unsigned int getThreadCount() const { return static_cast<unsigned int>(workers.size()); }

        /**
         * @brief Bounded ring-buffer queue capacity.
         */
        size_t getQueueCapacity() const { return buffer ? (bufferMask + 1) : 0; }

        /**
         * @brief Total number of jobs executed by the scheduler across all threads.
         */
        uint64_t getTotalJobsExecuted() const { return totalJobsExecuted.load(std::memory_order_relaxed); }

        // Tries to execute a single job from the queue (returns true if a job was executed)
        bool tryExecuteOneJob();

    private:
        JobSystem() = default;
        ~JobSystem();

        // Worker thread function loop
        void workerThreadLoop(unsigned int threadIndex);

        std::vector<std::thread> workers;

        // Bounded MPMC queue cell structure
        struct Cell {
            std::atomic<size_t> sequence;
            std::function<void()> job;
        };

        std::unique_ptr<Cell[]> buffer;
        size_t bufferMask{0};

        // Align queue cursors on separate cache lines to avoid false sharing
        alignas(64) std::atomic<size_t> enqueuePos{0};
        alignas(64) std::atomic<size_t> dequeuePos{0};

        // Semaphore to wake worker threads without a mutex
        std::counting_semaphore<65536> jobSemaphore{0};

        std::atomic<bool> stop{false};
        std::atomic<bool> initialized{false};
        std::atomic<uint64_t> totalJobsExecuted{0};
    };

} // namespace Engine

