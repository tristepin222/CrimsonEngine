#include <iostream>
#include <cassert>
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
#include "core/JobSystem.hpp"

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

    assert(registry.has<TestComponent>(entityA));
    assert(registry.get<TestComponent>(entityA)->value == 42);
    assert(registry.getRef<TestComponent>(entityA).value == 42);

    // 2. Destroy Entity A
    registry.destroy(entityA);
    assert(!registry.isValid(entityA));

    // 3. Create Entity B (recycles ID 0 with Gen 1)
    Entity entityB = registry.create();
    assert(entityB.getId() == originalId);
    assert(entityB.getGeneration() != entityA.getGeneration());
    assert(entityB != entityA);

    // Emplace component on entity B
    registry.emplace<TestComponent>(entityB, TestComponent{99, "EntityB"});
    assert(registry.has<TestComponent>(entityB));
    assert(registry.get<TestComponent>(entityB)->value == 99);

    // 4. Stale handle A regression checks:
    // a) has() on stale handle must be false
    assert(!registry.has<TestComponent>(entityA));

    // b) get() and tryGet() on stale handle must return nullptr
    assert(registry.get<TestComponent>(entityA) == nullptr);
    assert(registry.tryGet<TestComponent>(entityA) == nullptr);

    // c) getRef() on stale handle must throw
    bool threwOnGetRef = false;
    try {
        [[maybe_unused]] auto& ref = registry.getRef<TestComponent>(entityA);
    } catch (const std::runtime_error&) {
        threwOnGetRef = true;
    }
    assert(threwOnGetRef);

    // d) emplace() on stale handle must throw and MUST NOT overwrite B's data
    bool threwOnEmplace = false;
    try {
        registry.emplace<TestComponent>(entityA, TestComponent{999, "StaleOverwrite"});
    } catch (const std::runtime_error&) {
        threwOnEmplace = true;
    }
    assert(threwOnEmplace);
    assert(registry.get<TestComponent>(entityB)->value == 99);
    assert(registry.get<TestComponent>(entityB)->name == "EntityB");

    // e) emplace_or_replace() on stale handle must throw
    bool threwOnEmplaceOrReplace = false;
    try {
        registry.emplace_or_replace<TestComponent>(entityA, TestComponent{888, "StaleReplace"});
    } catch (const std::runtime_error&) {
        threwOnEmplaceOrReplace = true;
    }
    assert(threwOnEmplaceOrReplace);
    assert(registry.get<TestComponent>(entityB)->value == 99);

    // f) remove() on stale handle must NOT remove B's component
    registry.remove<TestComponent>(entityA);
    assert(registry.has<TestComponent>(entityB));
    assert(registry.get<TestComponent>(entityB)->value == 99);

    // g) getMask() on stale handle must throw; tryGetMask() must return nullptr
    bool threwOnGetMask = false;
    try {
        [[maybe_unused]] auto& mask = registry.getMask(entityA);
    } catch (const std::runtime_error&) {
        threwOnGetMask = true;
    }
    assert(threwOnGetMask);
    assert(registry.tryGetMask(entityA) == nullptr);

    // h) Direct ComponentStorage tests with stale handle
    ComponentStorage<TestComponent> storage;
    storage.add(entityB, TestComponent{123, "StorageB"});
    assert(storage.has(entityB));
    assert(!storage.has(entityA));
    assert(storage.tryGet(entityA) == nullptr);

    bool threwStorageGet = false;
    try {
        [[maybe_unused]] auto& ref = storage.get(entityA);
    } catch (const std::runtime_error&) {
        threwStorageGet = true;
    }
    assert(threwStorageGet);

    bool threwStorageAdd = false;
    try {
        storage.add(entityA, TestComponent{777, "StaleStorageAdd"});
    } catch (const std::runtime_error&) {
        threwStorageAdd = true;
    }
    assert(threwStorageAdd);
    assert(storage.get(entityB).value == 123);

    // remove on stale handle must not remove B
    storage.remove(entityA);
    assert(storage.has(entityB));
    assert(storage.get(entityB).value == 123);

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

    assert(caughtException);

    // Verify subsequent jobs run normally and the job system is not hung or broken
    std::atomic<int> counter{0};
    js.parallelFor(50, [&counter](int /*i*/) {
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    assert(counter.load() == 50);

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
    assert(nestedCounter.load() == 10);

    std::cout << "[Test 3] PASSED: Cooperative nested wait succeeded without deadlock." << std::endl;
}

// --------------------------------------------------------------------------
// 4. Queue Saturation Stress Test
// --------------------------------------------------------------------------
void testQueueSaturation() {
    std::cout << "[Test 4] Running Queue Saturation Stress Test (8,000 tasks)..." << std::endl;
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

    assert(completed.load() == TOTAL_JOBS);
    std::cout << "[Test 4] PASSED: 8,000 jobs completed with ring-buffer saturation handling." << std::endl;
}

// --------------------------------------------------------------------------
// 5. Job System Shutdown and Restart
// --------------------------------------------------------------------------
void testShutdownAndRestart() {
    std::cout << "[Test 5] Running Job System Shutdown and Restart Test..." << std::endl;
    auto& js = Engine::JobSystem::getInstance();

    js.shutdown();

    // Re-initialize and verify execution
    js.initialize(4);
    std::atomic<int> sum{0};
    js.parallelFor(200, [&sum](int i) {
        sum.fetch_add(i, std::memory_order_relaxed);
    });

    int expected = (199 * 200) / 2;
    assert(sum.load() == expected);

    std::cout << "[Test 5] PASSED: Shutdown and re-initialization functioned cleanly." << std::endl;
}

// --------------------------------------------------------------------------
// 6. Thread-Safe EntityCommandBuffer Concurrency Test
// --------------------------------------------------------------------------
void testEntityCommandBufferConcurrency() {
    std::cout << "[Test 6] Running Concurrent EntityCommandBuffer Test..." << std::endl;
    std::cout << "  -> Step A: instantiating Registry..." << std::endl;
    Registry registry;
    std::cout << "  -> Step B: instantiating EntityCommandBuffer..." << std::endl;
    EntityCommandBuffer ecb;
    std::cout << "  -> Step C: creating test entities..." << std::endl;

    std::vector<Entity> testEntities;
    for (int i = 0; i < 200; ++i) {
        testEntities.push_back(registry.create());
    }

    // Spawn 8 threads concurrently recording commands into the same buffer
    std::vector<std::thread> threads;
    constexpr int THREADS = 8;
    std::cout << "  -> Entities created, spawning threads..." << std::endl;
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
    std::cout << "  -> Threads joined. ecb.size() = " << ecb.size() << std::endl;

    assert(ecb.size() == 400); // 200 addComponent + 200 setActive
    std::cout << "  -> Executing ecb..." << std::endl;
    ecb.execute(registry);
    std::cout << "  -> ecb executed. Verifying..." << std::endl;
    assert(ecb.empty());

    // Verify all components were properly attached
    for (int i = 0; i < 200; ++i) {
        Entity e = testEntities[i];
        assert(registry.has<TestComponent>(e));
    }

    std::cout << "[Test 6] PASSED: EntityCommandBuffer concurrent recording and playback succeeded." << std::endl;
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
        testQueueSaturation();
        testShutdownAndRestart();
        testEntityCommandBufferConcurrency();
    } catch (const std::exception& e) {
        std::cerr << "[TEST SUITE FAILED] Unhandled exception: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[TEST SUITE FAILED] Unknown unhandled exception!" << std::endl;
        return 1;
    }

    std::cout << "======================================================" << std::endl;
    std::cout << "           ALL 6 TEST SUITES PASSED!                  " << std::endl;
    std::cout << "======================================================" << std::endl;
    return 0;
}
