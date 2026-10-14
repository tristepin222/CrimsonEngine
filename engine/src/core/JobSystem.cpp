#include "core/JobSystem.hpp"
#include <algorithm>
#include <iostream>
#include <atomic>
#include <semaphore>
#include <memory>
#include <limits>
#include <cstdint>
#include <string>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#endif

#if defined(_MSC_VER)
#include <intrin.h>
#if defined(_M_ARM) || defined(_M_ARM64)
#define SPIN_PAUSE() __yield()
#else
#define SPIN_PAUSE() _mm_pause()
#endif
#elif defined(__i386__) || defined(__x86_64__)
#if defined(__clang__) || defined(__GNUC__)
#define SPIN_PAUSE() __builtin_ia32_pause()
#else
#define SPIN_PAUSE() do {} while(0)
#endif
#elif defined(__arm__) || defined(__aarch64__)
#define SPIN_PAUSE() __asm__ __volatile__("yield")
#else
#define SPIN_PAUSE() do {} while(0)
#endif

namespace Engine {

    void JobSystem::initialize(unsigned int threadCount) {
        if (initialized.load(std::memory_order_acquire)) return;

        if (threadCount == 0) {
            unsigned int hw = std::thread::hardware_concurrency();
            // Use hardware threads minus 1 (to leave main thread free), or at least 1
            threadCount = hw > 1 ? hw - 1 : 1;
        }

        // Initialize Dmitriy Vyukov's MPMC bounded queue
        const size_t bufferSize = 65536; // Must be a power of two
        buffer = std::make_unique<Cell[]>(bufferSize);
        bufferMask = bufferSize - 1;
        for (size_t i = 0; i < bufferSize; ++i) {
            buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
        enqueuePos.store(0, std::memory_order_relaxed);
        dequeuePos.store(0, std::memory_order_relaxed);

        stop.store(false, std::memory_order_release);
        workers.reserve(threadCount);
        for (unsigned int i = 0; i < threadCount; ++i) {
            workers.emplace_back(&JobSystem::workerThreadLoop, this, i);
        }

        initialized.store(true, std::memory_order_release);
        std::cout << "[JobSystem] Lock-free pre-allocated JobSystem initialized with " << threadCount << " worker threads." << std::endl;
    }

    void JobSystem::shutdown() {
        if (!initialized.load(std::memory_order_acquire)) return;

        // Signal shutdown and wake all workers
        stop.store(true, std::memory_order_release);
        jobSemaphore.release(static_cast<int>(workers.size()));

        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
        workers.clear();

        // Clear pre-allocated queue buffer jobs
        buffer.reset();
        bufferMask = 0;
        enqueuePos.store(0, std::memory_order_relaxed);
        dequeuePos.store(0, std::memory_order_relaxed);

        initialized.store(false, std::memory_order_release);
        std::cout << "[JobSystem] Lock-free JobSystem shut down successfully." << std::endl;
    }

    JobSystem::~JobSystem() {
        shutdown();
    }

    JobHandle JobSystem::pushJob(std::function<void()> job) {
        auto handle = std::make_shared<JobCounter>();
        handle->remaining.store(1, std::memory_order_release);

        if (stop.load(std::memory_order_acquire)) {
            handle->remaining.store(0, std::memory_order_release);
            return handle;
        }

        auto wrappedJob = [this, job = std::move(job), handle]() {
            if (job) {
                try {
                    job();
                } catch (const std::exception& e) {
                    std::cerr << "[JobSystem] Job execution caught exception: " << e.what() << std::endl;
                } catch (...) {
                    std::cerr << "[JobSystem] Job execution caught unknown exception." << std::endl;
                }
            }
            handle->remaining.fetch_sub(1, std::memory_order_release);
            totalJobsExecuted.fetch_add(1, std::memory_order_relaxed);
        };

        size_t pos = enqueuePos.load(std::memory_order_relaxed);
        while (true) {
            Cell* cell = &buffer[pos & bufferMask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            if (diff == 0) {
                if (enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    cell->job = std::move(wrappedJob);
                    cell->sequence.store(pos + 1, std::memory_order_release);
                    break;
                }
            } else if (diff < 0) {
                for (int spin = 0; spin < 64; ++spin) {
                    SPIN_PAUSE();
                }
                std::this_thread::yield();
                pos = enqueuePos.load(std::memory_order_relaxed);
            } else {
                pos = enqueuePos.load(std::memory_order_relaxed);
            }
        }

        // Wake a worker without locking
        jobSemaphore.release();
        return handle;
    }

    JobHandle JobSystem::pushJobs(const std::vector<std::function<void()>>& jobs) {
        auto handle = std::make_shared<JobCounter>();
        handle->remaining.store(static_cast<int>(jobs.size()), std::memory_order_release);

        if (stop.load(std::memory_order_acquire) || jobs.empty()) {
            handle->remaining.store(0, std::memory_order_release);
            return handle;
        }

        for (const auto& job : jobs) {
            auto wrappedJob = [this, job, handle]() {
                if (job) {
                    try {
                        job();
                    } catch (const std::exception& e) {
                        std::cerr << "[JobSystem] Job execution caught exception: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[JobSystem] Job execution caught unknown exception." << std::endl;
                    }
                }
                handle->remaining.fetch_sub(1, std::memory_order_release);
                totalJobsExecuted.fetch_add(1, std::memory_order_relaxed);
            };

            size_t pos = enqueuePos.load(std::memory_order_relaxed);
            while (true) {
                Cell* cell = &buffer[pos & bufferMask];
                size_t seq = cell->sequence.load(std::memory_order_acquire);
                intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
                if (diff == 0) {
                    if (enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                        cell->job = std::move(wrappedJob);
                        cell->sequence.store(pos + 1, std::memory_order_release);
                        break;
                    }
                } else if (diff < 0) {
                    for (int spin = 0; spin < 64; ++spin) {
                        SPIN_PAUSE();
                    }
                    std::this_thread::yield();
                    pos = enqueuePos.load(std::memory_order_relaxed);
                } else {
                    pos = enqueuePos.load(std::memory_order_relaxed);
                }
            }
        }

        jobSemaphore.release(static_cast<int>(jobs.size()));
        return handle;
    }

    void JobSystem::wait(const JobHandle& handle) {
        if (!handle) return;
        while (!handle->isComplete()) {
            if (tryExecuteOneJob()) {
                continue;
            }
            SPIN_PAUSE();
            std::this_thread::yield();
        }
    }

    bool JobSystem::isComplete(const JobHandle& handle) const {
        return !handle || handle->isComplete();
    }

    bool JobSystem::tryExecuteOneJob() {
        size_t pos = dequeuePos.load(std::memory_order_relaxed);
        Cell* cell = nullptr;
        while (true) {
            cell = &buffer[pos & bufferMask];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            if (diff == 0) {
                if (dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                // Queue is empty!
                return false;
            } else {
                pos = dequeuePos.load(std::memory_order_relaxed);
            }
        }

        std::function<void()> job = std::move(cell->job);
        cell->sequence.store(pos + bufferMask + 1, std::memory_order_release);

        if (job) {
            try {
                job();
            } catch (const std::exception& e) {
                std::cerr << "[JobSystem] Job execution caught exception: " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "[JobSystem] Job execution caught unknown exception." << std::endl;
            }
        }
        return true;
    }

    void JobSystem::parallelFor(int count, std::function<void(int)> func, int chunkSize) {
        if (count <= 0) return;
        unsigned int numThreads = static_cast<unsigned int>(workers.size());

        if (numThreads == 0 || count < 2) {
            for (int i = 0; i < count; ++i) func(i);
            return;
        }

        if (chunkSize <= 0) {
            int autoChunk = std::max(1, count / static_cast<int>(numThreads * 8));
            chunkSize = std::clamp(autoChunk, 1, 128);
        }

        std::atomic<int> nextIndex{0};
        auto handle = std::make_shared<JobCounter>();
        handle->remaining.store(static_cast<int>(numThreads), std::memory_order_release);

        for (unsigned int w = 0; w < numThreads; ++w) {
            auto workerTask = [&func, count, chunkSize, &nextIndex, handle]() {
                while (true) {
                    int start = nextIndex.fetch_add(chunkSize, std::memory_order_relaxed);
                    if (start >= count) break;
                    int end = std::min(start + chunkSize, count);
                    for (int i = start; i < end; ++i) {
                        func(i);
                    }
                }
                handle->remaining.fetch_sub(1, std::memory_order_release);
            };

            size_t pos = enqueuePos.load(std::memory_order_relaxed);
            while (true) {
                Cell* cell = &buffer[pos & bufferMask];
                size_t seq = cell->sequence.load(std::memory_order_acquire);
                intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
                if (diff == 0) {
                    if (enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                        cell->job = std::move(workerTask);
                        cell->sequence.store(pos + 1, std::memory_order_release);
                        break;
                    }
                } else if (diff < 0) {
                    for (int spin = 0; spin < 64; ++spin) {
                        SPIN_PAUSE();
                    }
                    std::this_thread::yield();
                    pos = enqueuePos.load(std::memory_order_relaxed);
                } else {
                    pos = enqueuePos.load(std::memory_order_relaxed);
                }
            }
            jobSemaphore.release();
        }

        wait(handle);
    }

    void JobSystem::workerThreadLoop(unsigned int threadIndex) {
#if defined(_WIN32)
        std::wstring wname = L"JobWorker #" + std::to_wstring(threadIndex);
        SetThreadDescription(GetCurrentThread(), wname.c_str());
#endif
#if defined(TRACY_ENABLE)
        std::string tracyName = "JobWorker #" + std::to_string(threadIndex);
        tracy::SetThreadName(tracyName.c_str());
#endif

        while (true) {
            // Wait until a job becomes available or shutdown is signaled
            jobSemaphore.acquire();

            if (stop.load(std::memory_order_acquire)) {
                // Pop and execute remaining jobs (if any) before exiting
                while (tryExecuteOneJob()) {}
                return;
            }

            tryExecuteOneJob();
        }
    }

} // namespace Engine

