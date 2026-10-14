#include <iostream>
#include <string>
#include <vector>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <chrono>

#include "ecs/Registry.hpp"
#include "ecs/Entity.hpp"
#include "ecs/EntityManager.hpp"
#include "ecs/ComponentStorage.hpp"
#include "ecs/SystemManager.hpp"
#include "core/JobSystem.hpp"

// Non-stripping test assertion macro that works reliably in Release (NDEBUG) builds
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[ASSERTION FAILED] " << __FILE__ << ":" << __LINE__ \
                      << " in " << __func__ << "(): " << #cond << " -> " << (msg) << std::endl; \
            throw std::runtime_error(std::string("Assertion failed: ") + #cond + " - " + (msg)); \
        } \
    } while (false)

// Test components
struct TestComponent {
    int value = 0;
    std::string name;
};

struct HealthComponent {
    float hp = 100.0f;
};

// --------------------------------------------------------------------------
// 1. ECS Generational Handle Regression Test
// --------------------------------------------------------------------------
void testECSGenerationalSafety() {
    std::cout << "[Test 1] Running ECS Generational Handle Regression Test..." << std::endl;
    Registry registry;

    // 1. Create Entity A (e.g. ID 0, Gen 0)
    Entity entityA = registry.create();
    uint32_t originalId = entityA.getId();
    registry.emplace<TestComponent>(entityA, TestComponent{42, "EntityA"});

    TEST_ASSERT(registry.has<TestComponent>(entityA), "EntityA should have TestComponent");
    TEST_ASSERT(registry.get<TestComponent>(entityA) != nullptr && registry.get<TestComponent>(entityA)->value == 42, "EntityA component value should be 42");
    TEST_ASSERT(registry.getRef<TestComponent>(entityA).value == 42, "EntityA getRef value should be 42");

    // 2. Destroy Entity A
    registry.destroy(entityA);
    TEST_ASSERT(!registry.isValid(entityA), "EntityA should be invalid after destroy");

    // 3. Create Entity B (recycles ID 0 with Gen 1)
    Entity entityB = registry.create();
    TEST_ASSERT(entityB.getId() == originalId, "EntityB should recycle EntityA's numeric ID");
    TEST_ASSERT(entityB.getGeneration() != entityA.getGeneration(), "EntityB generation must differ from EntityA");
    TEST_ASSERT(entityB != entityA, "EntityB handle must not equal EntityA handle");

    // Emplace component on entity B
    registry.emplace<TestComponent>(entityB, TestComponent{99, "EntityB"});
    TEST_ASSERT(registry.has<TestComponent>(entityB), "EntityB should have TestComponent");
    TEST_ASSERT(registry.get<TestComponent>(entityB) != nullptr && registry.get<TestComponent>(entityB)->value == 99, "EntityB component value should be 99");

    // 4. Stale handle A regression checks:
    // a) has() on stale handle must be false
    TEST_ASSERT(!registry.has<TestComponent>(entityA), "has() on stale handle must return false");

    // b) get() and tryGet() on stale handle must return nullptr
    TEST_ASSERT(registry.get<TestComponent>(entityA) == nullptr, "get() on stale handle must return nullptr");
    TEST_ASSERT(registry.tryGet<TestComponent>(entityA) == nullptr, "tryGet() on stale handle must return nullptr");

    // c) getRef() on stale handle must throw
    bool threwOnGetRef = false;
    try {
        [[maybe_unused]] auto& ref = registry.getRef<TestComponent>(entityA);
    } catch (const std::runtime_error&) {
        threwOnGetRef = true;
    }
    TEST_ASSERT(threwOnGetRef, "getRef() on stale handle must throw std::runtime_error");

    // d) emplace() on stale handle must throw and MUST NOT overwrite B's data
    bool threwOnEmplace = false;
    try {
        registry.emplace<TestComponent>(entityA, TestComponent{999, "StaleOverwrite"});
    } catch (const std::runtime_error&) {
        threwOnEmplace = true;
    }
    TEST_ASSERT(threwOnEmplace, "emplace() on stale handle must throw std::runtime_error");
    TEST_ASSERT(registry.get<TestComponent>(entityB)->value == 99, "Stale emplace must not overwrite EntityB's value");
    TEST_ASSERT(registry.get<TestComponent>(entityB)->name == "EntityB", "Stale emplace must not overwrite EntityB's name");

    // e) emplace_or_replace() on stale handle must throw
    bool threwOnEmplaceOrReplace = false;
    try {
        registry.emplace_or_replace<TestComponent>(entityA, TestComponent{888, "StaleReplace"});
    } catch (const std::runtime_error&) {
        threwOnEmplaceOrReplace = true;
    }
    TEST_ASSERT(threwOnEmplaceOrReplace, "emplace_or_replace() on stale handle must throw std::runtime_error");
    TEST_ASSERT(registry.get<TestComponent>(entityB)->value == 99, "Stale emplace_or_replace must not mutate EntityB");

    // f) remove() on stale handle must NOT remove B's component
    registry.remove<TestComponent>(entityA);
    TEST_ASSERT(registry.has<TestComponent>(entityB), "remove() on stale handle must not remove EntityB's component");
    TEST_ASSERT(registry.get<TestComponent>(entityB)->value == 99, "EntityB value must remain unchanged after stale remove");

    // g) getMask() on stale handle must throw; tryGetMask() must return nullptr
    bool threwOnGetMask = false;
    try {
        [[maybe_unused]] auto& mask = registry.getMask(entityA);
    } catch (const std::runtime_error&) {
        threwOnGetMask = true;
    }
    TEST_ASSERT(threwOnGetMask, "getMask() on stale handle must throw std::runtime_error");
    TEST_ASSERT(registry.tryGetMask(entityA) == nullptr, "tryGetMask() on stale handle must return nullptr");

    // h) Direct ComponentStorage tests with stale handle
    ComponentStorage<TestComponent> storage;
    storage.add(entityB, TestComponent{123, "StorageB"});
    TEST_ASSERT(storage.has(entityB), "Storage must have EntityB");
    TEST_ASSERT(!storage.has(entityA), "Storage must not have stale EntityA");
    TEST_ASSERT(storage.tryGet(entityA) == nullptr, "storage.tryGet(entityA) must be nullptr");

    bool threwStorageGet = false;
    try {
        [[maybe_unused]] auto& ref = storage.get(entityA);
    } catch (const std::runtime_error&) {
        threwStorageGet = true;
    }
    TEST_ASSERT(threwStorageGet, "storage.get(stale handle) must throw");

    bool threwStorageAdd = false;
    try {
        storage.add(entityA, TestComponent{777, "StaleStorageAdd"});
    } catch (const std::runtime_error&) {
        threwStorageAdd = true;
    }
    TEST_ASSERT(threwStorageAdd, "storage.add(stale handle) must throw");
    TEST_ASSERT(storage.get(entityB).value == 123, "EntityB storage value preserved");

    // remove on stale handle must not remove B
    storage.remove(entityA);
    TEST_ASSERT(storage.has(entityB), "storage.remove(stale) must not remove EntityB");
    TEST_ASSERT(storage.get(entityB).value == 123, "storage value intact after stale remove");

    std::cout << "[Test 1] PASSED: Generational handles are strictly enforced at all access points." << std::endl;
}

// --------------------------------------------------------------------------
// 2. Job System Exception Safety in parallelFor()
// --------------------------------------------------------------------------
void testJobSystemExceptionSafety() {
    std::cout << "[Test 2] Running Job System Exception Safety Test..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();
    js.initialize();

    bool caughtException = false;
    try {
        js.parallelFor(100, [](int i) {
            if (i == 42) {
                throw std::runtime_error("Simulated failure at index 42");
            }
        });
    } catch (const std::runtime_error& e) {
        caughtException = true;
        std::cout << "  -> Successfully caught expected exception from parallelFor: " << e.what() << std::endl;
    } catch (...) {
        caughtException = true;
        std::cout << "  -> Caught unknown exception from parallelFor" << std::endl;
    }

    TEST_ASSERT(caughtException, "parallelFor must propagate exception when a worker task throws");

    // Verify subsequent jobs run normally and the job system is not hung or broken
    std::atomic<int> counter{0};
    js.parallelFor(50, [&counter](int /*i*/) {
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    TEST_ASSERT(counter.load() == 50, "Subsequent parallelFor must complete all 50 iterations successfully");

    std::cout << "[Test 2] PASSED: parallelFor completes and rethrows on exception without hanging." << std::endl;
}

// --------------------------------------------------------------------------
// 3. Nested Waits in Job System
// --------------------------------------------------------------------------
void testNestedWaits() {
    std::cout << "[Test 3] Running Nested Waits Test..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();

    std::atomic<int> nestedCounter{0};
    auto parentJob = js.pushJob([&js, &nestedCounter]() {
        // Child batch inside worker job
        std::vector<std::function<void()>> children;
        for (int i = 0; i < 10; ++i) {
            children.push_back([&nestedCounter]() {
                nestedCounter.fetch_add(1, std::memory_order_relaxed);
            });
        }
        auto childHandle = js.pushJobs(children);
        // Cooperative wait inside worker thread
        js.wait(childHandle);
    });

    js.wait(parentJob);
    TEST_ASSERT(nestedCounter.load() == 10, "All 10 nested child jobs must complete");

    std::cout << "[Test 3] PASSED: Cooperative nested wait succeeded without deadlock." << std::endl;
}

// --------------------------------------------------------------------------
// 4. High-Volume Batch Throughput Test (8,000 tasks)
// --------------------------------------------------------------------------
void testHighVolumeBatchThroughput() {
    std::cout << "[Test 4] Running High-Volume Batch Throughput Test (8,000 tasks)..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();

    constexpr int TOTAL_JOBS = 8000;
    std::atomic<int> completed{0};

    std::vector<std::function<void()>> batch;
    batch.reserve(TOTAL_JOBS);
    for (int i = 0; i < TOTAL_JOBS; ++i) {
        batch.push_back([&completed]() {
            completed.fetch_add(1, std::memory_order_relaxed);
        });
    }

    auto handle = js.pushJobs(batch);
    js.wait(handle);

    TEST_ASSERT(completed.load() == TOTAL_JOBS, "All 8,000 batch jobs must complete");
    std::cout << "[Test 4] PASSED: 8,000 jobs completed successfully through batch dispatcher." << std::endl;
}

// --------------------------------------------------------------------------
// 5. Genuine Queue Saturation and Backpressure Test
// --------------------------------------------------------------------------
void testQueueSaturationAndBackpressure() {
    std::cout << "[Test 5] Running Genuine Queue Saturation & Backpressure Test..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();

    // Re-initialize JobSystem with a deliberately small queue capacity (128 cells) and 2 workers
    js.shutdown();
    constexpr size_t SMALL_CAPACITY = 128;
    js.initialize(2, SMALL_CAPACITY);
    TEST_ASSERT(js.getQueueCapacity() == SMALL_CAPACITY, "Queue capacity should be exactly 128");

    // Spawn 4 concurrent producer threads each pushing 512 jobs (total 2048 jobs, 16x queue capacity)
    // This exercises the `diff < 0` backpressure spin-yield loop on full ring buffer
    constexpr int PRODUCERS = 4;
    constexpr int JOBS_PER_PRODUCER = 512;
    constexpr int TOTAL_JOBS = PRODUCERS * JOBS_PER_PRODUCER;

    std::atomic<int> completedCount{0};
    std::atomic<bool> startSignal{false};
    std::vector<std::thread> producers;
    producers.reserve(PRODUCERS);

    for (int p = 0; p < PRODUCERS; ++p) {
        producers.emplace_back([&js, &completedCount, &startSignal]() {
            while (!startSignal.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < JOBS_PER_PRODUCER; ++i) {
                js.pushJob([&completedCount]() {
                    completedCount.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
    }

    // Release producers concurrently
    startSignal.store(true, std::memory_order_release);

    for (auto& t : producers) {
        t.join();
    }

    // Wait until all submitted jobs have finished
    auto startTime = std::chrono::steady_clock::now();
    while (completedCount.load(std::memory_order_acquire) < TOTAL_JOBS) {
        js.tryExecuteOneJob();
        auto elapsed = std::chrono::steady_clock::now() - startTime;
        if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() > 10) {
            TEST_ASSERT(false, "Timeout waiting for saturated queue jobs to finish!");
        }
    }

    TEST_ASSERT(completedCount.load() == TOTAL_JOBS, "All 2,048 jobs submitted under heavy queue backpressure must complete");
    std::cout << "[Test 5] PASSED: Bounded MPMC queue successfully sustained full saturation and backpressure across concurrent producers." << std::endl;

    // Restore standard scheduler configuration for subsequent tests
    js.shutdown();
    js.initialize();
}

// --------------------------------------------------------------------------
// 6. Job System Shutdown and Restart
// --------------------------------------------------------------------------
void testShutdownAndRestart() {
    std::cout << "[Test 6] Running Job System Shutdown and Restart Test..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();

    js.shutdown();

    // Re-initialize with 4 workers and verify execution
    js.initialize(4);
    std::atomic<int> sum{0};
    js.parallelFor(200, [&sum](int i) {
        sum.fetch_add(i, std::memory_order_relaxed);
    });

    int expected = (199 * 200) / 2;
    TEST_ASSERT(sum.load() == expected, "Sum calculated via parallelFor after restart must match arithmetic series sum");

    std::cout << "[Test 6] PASSED: Shutdown and re-initialization functioned cleanly." << std::endl;
}

// --------------------------------------------------------------------------
// 7. Thread-Safe EntityCommandBuffer Concurrency Test
// --------------------------------------------------------------------------
void testEntityCommandBufferConcurrency() {
    std::cout << "[Test 7] Running Concurrent EntityCommandBuffer Recording Test..." << std::endl;
    Registry registry;
    EntityCommandBuffer ecb;

    std::vector<Entity> testEntities;
    for (int i = 0; i < 200; ++i) {
        testEntities.push_back(registry.create());
    }

    // Spawn 8 threads concurrently recording commands into the same buffer
    std::vector<std::thread> threads;
    constexpr int THREADS = 8;
    for (int t = 0; t < THREADS; ++t) {
        threads.emplace_back([t, &ecb, &testEntities]() {
            for (int i = 0; i < 25; ++i) {
                Entity e = testEntities[t * 25 + i];
                ecb.addComponent<TestComponent>(e, TestComponent{t * 100 + i, "Parallel"});
                ecb.setActive(e, true);
            }
        });
    }

    for (auto& th : threads) {
        th.join();
    }

    TEST_ASSERT(ecb.size() == 400, "ECB should have exactly 400 recorded commands (200 adds + 200 setActives)");
    ecb.execute(registry);
    TEST_ASSERT(ecb.empty(), "ECB should be empty after execute");

    // Verify all components were properly attached
    for (int i = 0; i < 200; ++i) {
        Entity e = testEntities[i];
        TEST_ASSERT(registry.has<TestComponent>(e), "Entity must possess TestComponent after ECB playback");
    }

    std::cout << "[Test 7] PASSED: Thread-safe ECB recording from multiple threads played back deterministically." << std::endl;
}

// --------------------------------------------------------------------------
// 8. EntityCommandBuffer Exception Safety & Non-Discarding Playback Test
// --------------------------------------------------------------------------
void testEntityCommandBufferExceptionSafety() {
    std::cout << "[Test 8] Running EntityCommandBuffer Playback Exception Safety Test..." << std::endl;
    Registry registry;
    EntityCommandBuffer ecb;

    // Create 3 entities: A, B, C
    Entity entityA = registry.create();
    Entity entityB = registry.create();
    Entity entityC = registry.create();

    // Destroy Entity B so its handle becomes stale
    registry.destroy(entityB);
    TEST_ASSERT(!registry.isValid(entityB), "EntityB should be invalid/stale");

    // Record 3 deferred commands:
    // 1. Add TestComponent to valid Entity A
    // 2. Add TestComponent to STALE Entity B (which will throw due to generational safety)
    // 3. Add TestComponent to valid Entity C
    ecb.addComponent<TestComponent>(entityA, TestComponent{111, "ValidA"});
    ecb.addComponent<TestComponent>(entityB, TestComponent{222, "StaleB"});
    ecb.addComponent<TestComponent>(entityC, TestComponent{333, "ValidC"});

    TEST_ASSERT(ecb.size() == 3, "ECB should have 3 pending commands");

    // Execute with throwOnError = false and collect errors into vector
    std::vector<std::exception_ptr> collectedErrors;
    size_t successCount = ecb.execute(registry, false, &collectedErrors);

    // Verify non-discarding policy:
    // - Command 1 (Entity A) executed successfully
    // - Command 2 (Entity B) threw and was captured in collectedErrors
    // - Command 3 (Entity C) executed successfully (NOT discarded despite command 2's failure!)
    TEST_ASSERT(successCount == 2, "Exactly 2 valid commands should have succeeded");
    TEST_ASSERT(collectedErrors.size() == 1, "Exactly 1 exception should have been collected for stale handle");
    TEST_ASSERT(ecb.empty(), "ECB pending commands buffer should be cleared after playback");

    TEST_ASSERT(registry.has<TestComponent>(entityA), "Entity A must have received its component");
    TEST_ASSERT(registry.get<TestComponent>(entityA)->value == 111, "Entity A value should be 111");

    TEST_ASSERT(!registry.has<TestComponent>(entityB), "Stale Entity B must not have any components");

    TEST_ASSERT(registry.has<TestComponent>(entityC), "Entity C must have received its component despite prior failure");
    TEST_ASSERT(registry.get<TestComponent>(entityC)->value == 333, "Entity C value should be 333");

    // Now test throwOnError = true behavior
    ecb.addComponent<TestComponent>(entityB, TestComponent{999, "ThrowOnStale"});
    bool threwOnPlayback = false;
    try {
        ecb.execute(registry, true);
    } catch (const std::runtime_error& e) {
        threwOnPlayback = true;
    }
    TEST_ASSERT(threwOnPlayback, "ecb.execute(reg, true) must rethrow when a command encounters an error");

    std::cout << "[Test 8] PASSED: EntityCommandBuffer playback does not discard subsequent commands on error." << std::endl;
}

// --------------------------------------------------------------------------
// 9. pushJobs() Batch Exceeding Queue Capacity (Deadlock-Proof with Timeout)
// --------------------------------------------------------------------------
void testPushJobsExceedingQueueCapacity() {
    std::cout << "[Test 9] Running pushJobs() Batch Exceeding Queue Capacity Test..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();
    js.shutdown();

    // Configure small queue capacity (128 cells) and 2 worker threads
    constexpr size_t SMALL_CAPACITY = 128;
    js.initialize(2, SMALL_CAPACITY);
    TEST_ASSERT(js.getQueueCapacity() == SMALL_CAPACITY, "Queue capacity must be exactly 128");

    // Batch size of 1,024 jobs (8x the queue capacity)
    constexpr int BATCH_SIZE = 1024;
    std::atomic<int> executedCount{0};

    std::vector<std::function<void()>> batch;
    batch.reserve(BATCH_SIZE);
    for (int i = 0; i < BATCH_SIZE; ++i) {
        batch.push_back([&executedCount]() {
            executedCount.fetch_add(1, std::memory_order_relaxed);
        });
    }

    std::atomic<bool> completed{false};
    std::atomic<bool> timedOut{false};

    // Run pushJobs + wait in a separate thread so a deadlock can be caught via timeout instead of hanging CI
    std::thread testThread([&js, &batch, &completed]() {
        auto handle = js.pushJobs(batch);
        js.wait(handle);
        completed.store(true, std::memory_order_release);
    });

    auto start = std::chrono::steady_clock::now();
    while (!completed.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - start).count();
        if (elapsed >= 5) {
            timedOut.store(true, std::memory_order_release);
            break;
        }
    }

    if (timedOut.load()) {
        std::cerr << "[DEADLOCK DETECTED] pushJobs() deadlocked when batch size (1024) exceeded queue capacity (128)!" << std::endl;
        testThread.detach();
        TEST_ASSERT(false, "pushJobs() deadlocked on batch larger than queue capacity!");
    } else {
        testThread.join();
    }

    TEST_ASSERT(executedCount.load() == BATCH_SIZE, "All 1,024 batch jobs must have completed");
    std::cout << "[Test 9] PASSED: pushJobs() successfully processed batch of 1,024 jobs through a 128-entry queue without deadlocking." << std::endl;

    // Reset JobSystem to standard configuration
    js.shutdown();
    js.initialize();
}

// --------------------------------------------------------------------------
// 10. SystemManager::flushCommandBuffer() Both Buffers Flushed On Error Test
// --------------------------------------------------------------------------
void testSystemManagerFlushCommandBufferBothBuffers() {
    std::cout << "[Test 10] Running SystemManager::flushCommandBuffer() Both Buffers Evaluation Test..." << std::endl;
    Registry registry;
    SystemManager sm;

    Entity entityA = registry.create();
    Entity entityB = registry.create();
    Entity staleEntity = registry.create();
    registry.destroy(staleEntity);
    TEST_ASSERT(!registry.isValid(staleEntity), "staleEntity must be invalid");

    // In SystemManager's command buffer:
    // 1. Add invalid command that throws (stale entity)
    // 2. Add valid command for entityA
    sm.getCommandBuffer().addComponent<TestComponent>(staleEntity, TestComponent{1, "StaleSM"});
    sm.getCommandBuffer().addComponent<TestComponent>(entityA, TestComponent{10, "ValidSM"});

    // In Registry's command buffer:
    // Add valid command for entityB
    registry.getCommandBuffer().addComponent<TestComponent>(entityB, TestComponent{20, "ValidReg"});

    TEST_ASSERT(!sm.getCommandBuffer().empty(), "SM command buffer should not be empty");
    TEST_ASSERT(!registry.getCommandBuffer().empty(), "Registry command buffer should not be empty");

    // Execute flush with throwOnError = true
    bool caughtExpectedError = false;
    try {
        sm.flushCommandBuffer(registry, true);
    } catch (const std::runtime_error& e) {
        caughtExpectedError = true;
    }

    TEST_ASSERT(caughtExpectedError, "flushCommandBuffer must rethrow when throwOnError is true");

    // Verify BOTH buffers were fully drained and executed despite the exception in the first buffer
    TEST_ASSERT(sm.getCommandBuffer().empty(), "SM command buffer must be empty after flush");
    TEST_ASSERT(registry.getCommandBuffer().empty(), "Registry command buffer must be empty after flush (NOT skipped!)");

    TEST_ASSERT(registry.has<TestComponent>(entityA), "entityA must have received component from SM buffer");
    TEST_ASSERT(registry.get<TestComponent>(entityA)->value == 10, "entityA value must be 10");

    TEST_ASSERT(registry.has<TestComponent>(entityB), "entityB must have received component from Registry buffer");
    TEST_ASSERT(registry.get<TestComponent>(entityB)->value == 20, "entityB value must be 20");

    std::cout << "[Test 10] PASSED: SystemManager::flushCommandBuffer() evaluated both command buffers without stopping on first error." << std::endl;
}

// --------------------------------------------------------------------------
// Main Entry Point
// --------------------------------------------------------------------------
int main() {
    std::cout << "======================================================" << std::endl;
    std::cout << "           RUNNING ENGINE TEST SUITE                  " << std::endl;
    std::cout << "======================================================" << std::endl;

    try {
        testECSGenerationalSafety();
        testJobSystemExceptionSafety();
        testNestedWaits();
        testHighVolumeBatchThroughput();
        testQueueSaturationAndBackpressure();
        testShutdownAndRestart();
        testEntityCommandBufferConcurrency();
        testEntityCommandBufferExceptionSafety();
        testPushJobsExceedingQueueCapacity();
        testSystemManagerFlushCommandBufferBothBuffers();
    } catch (const std::exception& e) {
        std::cerr << "[TEST SUITE FAILED] Unhandled exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[TEST SUITE FAILED] Unknown unhandled exception!" << std::endl;
        return 1;
    }

    std::cout << "======================================================" << std::endl;
    std::cout << "           ALL 10 TEST SUITES PASSED!                 " << std::endl;
    std::cout << "======================================================" << std::endl;
    return 0;
}
