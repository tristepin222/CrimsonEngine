# Custom Entity-Component System (ECS)

This document provides a technical deep-dive into the engine's custom, production-ready Entity-Component System (ECS). The ECS is designed from scratch to prioritize cache locality, ultra-fast sparse-set lookups, strict generational entity recycling, compile-time template queries, deferred command buffering, staged system execution, and multi-threaded parallel iteration.

![ECS Structure](diagrams/out/ecs_structure/ECSStructure.png)

---

## 1. Dynamic Entity Allocation & Generational Recycling

The [Entity](../engine/include/ecs/Entity.hpp) class is a 32-bit generational handle combining a 24-bit numeric identifier with an 8-bit generation counter (`uint32_t` bit-packed). This protects against dangling references when an entity ID is recycled.

Lifetime and allocation are managed by [EntityManager.hpp](../engine/include/ecs/EntityManager.hpp):

*   **Dynamic Scalability (No Static Limit)**: Unlike earlier prototypes constrained by a static `MAX_ENTITIES = 10000`, the manager dynamically scales its capacity on demand with a monotonically increasing `nextId` counter and exponential growth.
*   **Recycling Queue**: Recycled entity indices are stored in a `std::stack<Entity::IdType> freeIds`. When an entity is destroyed, its generation counter increments (`generations[id]++`), and its index is returned to the pool.
*   **Dense Alive Tracking & $O(1)$ Swap-Remove**: The manager maintains a packed `std::vector<uint32_t> alive` array of active entity IDs, paired with an inverse index vector `entityToAliveIndex`. Deleting an entity performs a true $O(1)$ swap-and-pop:
    ```cpp
    uint32_t removedIdx = entityToAliveIndex[id];
    uint32_t lastId = alive.back();
    alive[removedIdx] = lastId;
    entityToAliveIndex[lastId] = removedIdx;
    alive.pop_back();
    ```
    This guarantees that retrieving active entities (`getAlive()`) is completely contiguous with zero empty slot iterations.
*   **Generational Mask Accessors**: Accessing masks via `getMask(Entity e)` validates that `e` is currently alive and that its generation matches. If a stale or invalid handle is passed, it throws a `std::runtime_error`. Non-throwing lookup is provided via `tryGetMask(Entity e)`.

---

## 2. Dynamic Sparse Set Storage & Generational Safety (`ComponentStorage`)

Components are pure plain-old-data (POD) structures. To achieve optimal cache locality while avoiding wasteful memory overhead, [ComponentStorage.hpp](../engine/include/ecs/ComponentStorage.hpp) implements a **Dynamic Sparse Set** architecture:

```cpp
template<typename T>
class ComponentStorage : public IStorage {
private:
    std::vector<T> dense;                             // Contiguous, tightly packed component data
    std::vector<Entity> entities;                     // Parallel vector tracking owning entities
    std::vector<uint32_t> sparse;                     // Dynamic sparse map: Entity ID -> Dense Index
};
```

### Strict Generational Enforcement at Every Access Point

To prevent subtle bugs where a stale entity handle accesses or corrupts components belonging to a newly recycled entity with the same numeric ID:

1. **`has(Entity e)`**: Verifies `id < sparse.size() && sparse[id] != INVALID_INDEX && entities[sparse[id]] == e`.
2. **`add(Entity e, T comp)`**: If the ID slot is occupied (`sparse[id] != INVALID_INDEX`), it validates `entities[denseIdx] == e`. If generations mismatch, it throws `std::runtime_error`, preventing stale handles from overwriting components belonging to new entities.
3. **`get(Entity e)` / `tryGet(Entity e)`**: Checks `entities[sparse[id]] == e`. Stale handles throw on `get()` or return `nullptr` on `tryGet()`.
4. **`remove(Entity e)`**: Only removes the component if `entities[sparse[id]] == e`, ensuring a stale handle cannot delete components from a recycled entity.
5. **`Registry::emplace` / `Registry::emplace_or_replace`**: Validates `entities.isValid(e)` upfront before mutating storage or setting component mask bits.

---

## 3. Thread-Safe Entity Command Buffer (`EntityCommandBuffer`)

Mutating the ECS topology (creating/destroying entities, adding or removing components) while iterating over views or executing systems across worker threads causes race conditions and iterator invalidation.

To make ECS operations production-ready and thread-safe, the engine provides the [`EntityCommandBuffer`](../engine/include/ecs/Registry.hpp):

```cpp
EntityCommandBuffer ecb;

// Record commands safely inside concurrent worker threads or parallel loops
ecb.destroyEntity(enemy);
ecb.addComponent<ExplosionComponent>(posEntity, ExplosionComponent{...});
ecb.removeComponent<Velocity>(stoppedEntity);

// Play back all recorded mutations sequentially at the frame sync point
ecb.execute(registry);
```

### Concurrency Guarantees
* **Internal Synchronization**: All recording methods (`destroyEntity`, `addComponent`, `removeComponent`, `setActive`, `clear`) are guarded by an internal `std::mutex`.
* **Multi-Thread Recording**: Multiple worker threads and parallel systems can safely record commands into the same command buffer without data races.
* **Atomic Playback**: `execute(registry)` moves the pending command vector out under the lock before executing, allowing new commands to be buffered concurrently if needed.

---

## 4. Multi-Component Views & Iteration Strategies

Queries in the engine use `Registry::view<Components...>()` to filter entities possessing a specific set of components.

### Optimization: Smallest Pool Heuristic

To query entities having components `A`, `B`, and `C`, the `Registry` inspects the sizes of all participating storage pools and selects the **smallest pool** as the primary driver. Only entities in this driver pool have their component masks tested against the composite bitmask:

```cpp
template<typename First, typename Second, typename... Rest>
IStorage* getSmallestStorage() {
    IStorage* a = getStorage<First>();
    IStorage* b = getSmallestStorage<Second, Rest...>();
    if (!a) return b;
    if (!b) return a;
    return (a->size() < b->size()) ? a : b;
}
```

### Iteration Paradigms

#### 1. Range-Based For Loop (Structured Bindings)
Standard C++ forward iterator supporting concise structured bindings:
```cpp
for (auto [entity, transform, mesh] : registry.view<Transform, Mesh>()) {
    transform.position.y += 1.0f * dt;
}
```

#### 2. Direct Functional Iteration (`view.each`)
Bypasses iterator object construction and state machine checking for maximum single-threaded throughput:
```cpp
registry.view<Transform, RigidBody>().each([](Entity e, Transform& t, RigidBody& rb) {
    t.position += rb.velocity * dt;
});
```

#### 3. Multi-Threaded Parallel Iteration (`view.eachParallel`)
Distributes entity chunks across the engine's worker threads via the `JobSystem`:
```cpp
registry.view<Transform, ParticleComponent>().eachParallel(
    jobSystem,
    [](Entity e, Transform& t, ParticleComponent& p) {
        p.lifetime -= dt;
        t.position += p.velocity * dt;
    },
    chunkSize /* e.g. 64 entities per worker chunk */
);
```

---

## 5. System Execution Pipeline & System Stages

The engine defines a structured system lifecycle via the [`SystemStage`](../engine/include/ecs/System.hpp) enum:

```mermaid
flowchart LR
    PreUpdate["1. PreUpdate<br/>(Input, Hardware)"] --> Physics["2. PhysicsUpdate<br/>(RigidBody, Collisions)"]
    Physics --> Update["3. Update<br/>(Gameplay, Controllers, Animation)"]
    Update --> PostUpdate["4. PostUpdate<br/>(Camera, Cinemachine, Audio, UI)"]
    PostUpdate --> Flush["Command Buffer Flush<br/>(ECB Playback)"]
    Flush --> RenderStage["5. Render<br/>(Scene Draw, Meshes, Post-Process)"]
```

### Lifecycle Stage Definitions

| Stage | Enum | Purpose | Execution Mode | Standard Systems |
| :--- | :--- | :--- | :--- | :--- |
| **Pre-Update** | `SystemStage::PreUpdate` | Process input hardware events, networking packets, and initial frame setup. | Sequential | `InputSystem` |
| **Physics Update** | `SystemStage::PhysicsUpdate` | Discrete timestep integration, broadphase/narrowphase collision detection, constraint solvers. | Sequential | `PhysicsSystem` |
| **Update** | `SystemStage::Update` | Core gameplay logic, character controllers, procedural generation, AI pathfinding. | Parallel (`updateStageParallel`) | `PlayerControllerSystem`, `AnimationSystem`, `AStarSystem` |
| **Post-Update** | `SystemStage::PostUpdate` | Camera tracking, late animation constraints, procedural audio, UI layout reconciliation. | Parallel (`updateStageParallel`) | `CameraSystem`, `CinemachineSystem`, `AudioSystem`, `UISystem` |
| **Render** | `SystemStage::Render` | Scene rasterization, compute passes, shadow cascades, ImGui overlays. | Sequential | `RenderSystem` |

### Parallel-Safe System Contract (`isParallelSafe()`)

Systems marked with `isParallelSafe() == true` adhere to an explicit concurrency contract:
1. **Deferred Topology Mutations**: Systems must never directly call `registry.create()`, `registry.destroy()`, or add/remove components during parallel execution. All topology mutations must be recorded via `EntityCommandBuffer`.
2. **Disjoint Component Access**: Parallel-safe systems within the same stage must operate on mutually disjoint component write sets. Read-only / const component lookups are unrestricted.
3. **Thread-Safe Command Recording**: All mutations recorded to `systemManager.getCommandBuffer()` or `registry.getCommandBuffer()` are internally synchronized and executed deterministically during `flushCommandBuffer(registry)` before the Render stage.

---

## 6. Deep Entity Cloning (`EntityCloner`)

Duplicating entities with complex hierarchies, multiple components, and GPU resources is handled by [`EntityCloner.hpp`](../engine/include/ecs/EntityCloner.hpp):

1. **Entity Allocation**: Allocates a new entity identifier from `registry.create()`.
2. **Component Introspection**: Iterates through registered component reflection metadata in `ComponentReflectionRegistry`.
3. **Data Duplication**: Re-creates components on the clone and performs deep member copying.
4. **Name Formatting**: Increments duplicate labels (e.g. `"Orc"` becomes `"Orc (Clone)"`).
5. **GPU Asset Binding**: Safely references shared mesh/texture descriptor sets without triggering redundant VRAM allocations.
6. **Hierarchy Cloning**: Duplicates child hierarchies attached via `HierarchyComponent`, remapping parent/child references to the newly cloned entities.

---

## 7. Event Subscriptions

The [Registry](../engine/include/ecs/Registry.hpp) allows systems to subscribe to component insertion and deletion events:
```cpp
using ComponentAddedCallback = std::function<void(Entity)>;
using ComponentRemovedCallback = std::function<void(Entity)>;

registry.subscribeToAdded<Mesh>([this](Entity e) { checkAndAdd(e); });
registry.subscribeToRemoved<Mesh>([this](Entity e) { removeEntity(e); });
```
This is utilized by `RenderSystem` and spatial acceleration structures to maintain auxiliary rendering lists without per-frame entity scans.
