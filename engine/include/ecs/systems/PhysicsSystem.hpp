#pragma once
#include "ecs/System.hpp"
#include "ecs/Registry.hpp"
#include "ecs/components/Transform.hpp"
#include "ecs/components/RigidBody.hpp"
#include "ecs/components/Collider.hpp"
#include "ecs/components/Hierarchy.hpp"
#include "ecs/components/Tilemap.hpp"
#include "editor/EditorModeState.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <vector>
#include <cmath>
#include <limits>
#include <iostream>

namespace Engine {

    struct CollisionInfo {
        bool collided = false;
        glm::vec3 normal{0.0f}; // Points from A to B
        float penetration = 0.0f;
        glm::vec3 contactPoints[8]{};
        float contactPenetrations[8]{};
        int contactCount = 0;
    };

    struct Ray {
        glm::vec3 origin;
        glm::vec3 direction; // Normalized
    };

    struct RaycastHit {
        bool hit = false;
        Entity entity;
        float distance = std::numeric_limits<float>::max();
        glm::vec3 position{0.0f};
        glm::vec3 normal{0.0f};
    };

    /**
     * @class PhysicsSystem
     * @brief Evaluates rigid body movement and resolves collider intersections.
     */
    class PhysicsSystem : public System {
    public:
        const char* getName() const override { return "PhysicsSystem"; }
        PhysicsSystem(Registry& reg, EditorModeState& editorMode)
            : registry(reg), editorMode(editorMode) {}

        void applyAngularDisplacement(Transform& transform, const glm::vec3& deltaTheta) {
            if (glm::length(deltaTheta) < 1e-6f) return;

            // 1. Get current native quaternion directly from transform
            glm::quat qCurrent = transform.rotation.getQuat();

            // 2. Compute incremental rotation quaternion in world space (deltaTheta is in world coordinates)
            float angle = glm::length(deltaTheta);
            glm::quat qDelta = glm::angleAxis(angle, glm::normalize(deltaTheta));

            // 3. Pre-multiply since deltaTheta is in world coordinates and assign directly
            transform.rotation = glm::normalize(qDelta * qCurrent);
        }

        void update(float dt) override {
            if (!editorMode.isPlaying) return;
            if (dt <= 0.0f) return;
            dt = std::min(dt, 1.0f / 30.0f);

            glm::vec3 gravityVector{0.0f, -9.81f, 0.0f};

            // 1. Integration Pass (Gravity and Force accumulation)
            for (auto [entity, transform, rb] : registry.view<Transform, RigidBodyComponent>()) {
                if (rb.type == RigidBodyType::Static) continue;

                rb.hadContactThisFrame = false;
                rb.unstableContactThisFrame = false;

                // --- Sleeping body handling ---
                if (rb.sleeping) {
                    bool externalVel   = glm::length(rb.velocity)        > 0.08f;
                    bool externalAngVel= glm::length(rb.angularVelocity) > 0.15f;
                    bool externalForce = glm::length(rb.force)           > 0.01f ||
                                         glm::length(rb.torque)          > 0.01f;
                    if (externalVel || externalAngVel || externalForce) {
                        rb.sleeping   = false;
                        rb.sleepTimer = 0.0f;
                    } else {
                        rb.force  = glm::vec3(0.0f);
                        rb.torque = glm::vec3(0.0f);
                        continue;
                    }
                }

                float massVal = rb.mass;
                if (massVal < 1e-4f || std::isnan(massVal) || std::isinf(massVal)) {
                    rb.mass = 1.0f;
                    massVal = 1.0f;
                }

                glm::vec3 gravityForce = gravityVector * rb.gravityScale * massVal;
                glm::vec3 totalForce = rb.force + gravityForce;

                if (rb.freezePositionX) { totalForce.x = 0.0f; rb.velocity.x = 0.0f; }
                if (rb.freezePositionY) { totalForce.y = 0.0f; rb.velocity.y = 0.0f; }
                if (rb.freezePositionZ) { totalForce.z = 0.0f; rb.velocity.z = 0.0f; }

                glm::vec3 accel = totalForce / massVal;
                if (std::isnan(accel.x) || std::isinf(accel.x)) accel = glm::vec3(0.0f);

                rb.velocity += accel * dt;
                rb.velocity *= std::exp(-rb.linearDrag * dt);

                if (rb.freezePositionX) rb.velocity.x = 0.0f;
                if (rb.freezePositionY) rb.velocity.y = 0.0f;
                if (rb.freezePositionZ) rb.velocity.z = 0.0f;

                if (std::isnan(rb.velocity.x) || std::isinf(rb.velocity.x)) rb.velocity = glm::vec3(0.0f);

                transform.position += rb.velocity * dt;

                if (std::isnan(transform.position.x) || std::isinf(transform.position.x) ||
                    std::isnan(transform.position.y) || std::isinf(transform.position.y) ||
                    std::isnan(transform.position.z) || std::isinf(transform.position.z)) {
                    transform.position = glm::vec3(0.0f);
                    rb.velocity = glm::vec3(0.0f);
                }

                if (std::isnan(rb.angularVelocity.x) || std::isinf(rb.angularVelocity.x)) {
                    rb.angularVelocity = glm::vec3(0.0f);
                }

                if (rb.freezeRotationX) { rb.torque.x = 0.0f; rb.angularVelocity.x = 0.0f; }
                if (rb.freezeRotationY) { rb.torque.y = 0.0f; rb.angularVelocity.y = 0.0f; }
                if (rb.freezeRotationZ) { rb.torque.z = 0.0f; rb.angularVelocity.z = 0.0f; }

                float inertiaScale = 0.5f * massVal;
                glm::vec3 angularAccel = rb.torque / (inertiaScale > 1e-4f ? inertiaScale : 1.0f);
                rb.angularVelocity += angularAccel * dt;

                rb.angularVelocity *= std::exp(-rb.angularDrag * dt);

                if (rb.freezeRotationX) rb.angularVelocity.x = 0.0f;
                if (rb.freezeRotationY) rb.angularVelocity.y = 0.0f;
                if (rb.freezeRotationZ) rb.angularVelocity.z = 0.0f;

                applyAngularDisplacement(transform, rb.angularVelocity * dt);

                rb.force  = glm::vec3(0.0f);
                rb.torque = glm::vec3(0.0f);
            }

            // 2. Collision Detection & Resolution Pass
            struct ColliderEntry {
                Entity entity;
                Transform* transform;
                ColliderComponent* collider;
            };
            
            std::vector<ColliderEntry> colliders;
            for (auto [entity, transform, collider] : registry.view<Transform, ColliderComponent>()) {
                colliders.push_back({entity, &transform, &collider});
            }

            const int passes = 6;
            const float solverDt = dt / static_cast<float>(passes);
            for (int pass = 0; pass < passes; ++pass) {
                for (size_t i = 0; i < colliders.size(); ++i) {
                    for (size_t j = i + 1; j < colliders.size(); ++j) {
                        auto& entryA = colliders[i];
                        auto& entryB = colliders[j];

                        CollisionInfo colInfo = checkCollision(
                            entryA.entity, *entryA.collider,
                            entryB.entity, *entryB.collider
                        );

                        if (colInfo.collided) {
                            resolveCollision(
                                entryA.entity, *entryA.transform, *entryA.collider,
                                entryB.entity, *entryB.transform, *entryB.collider,
                                colInfo,
                                solverDt
                            );
                        }
                    }
                }
            }

            // 2b. Direct O(1) Tilemap Grid Collision Pass (Zero Physics Entities)
            for (auto [actorEnt, trans, col] : registry.view<Transform, ColliderComponent>()) {
                auto* rb = registry.get<RigidBodyComponent>(actorEnt);
                if (rb && rb->type == RigidBodyType::Static) continue;

                glm::vec3 actorPos = trans.position + col.offset;
                glm::vec3 actorExtents = col.extents;
                if (col.shape == ColliderShape::Sphere) actorExtents = glm::vec3(col.radius);

                for (auto [tmEnt, tm] : registry.view<TilemapComponent>()) {
                    auto* tmTrans = registry.get<Transform>(tmEnt);
                    glm::mat4 tmModel = tmTrans ? tmTrans->matrix() : glm::mat4(1.0f);
                    glm::mat4 tmInv = glm::inverse(tmModel);

                    glm::vec3 localActorPos = glm::vec3(tmInv * glm::vec4(actorPos, 1.0f));
                    float localActorMinX = localActorPos.x - actorExtents.x;
                    float localActorMaxX = localActorPos.x + actorExtents.x;
                    float localActorMinY = localActorPos.y - actorExtents.y;
                    float localActorMaxY = localActorPos.y + actorExtents.y;

                    int minTX = static_cast<int>(std::floor(localActorMinX / tm.tileSize));
                    int maxTX = static_cast<int>(std::floor(localActorMaxX / tm.tileSize));
                    int minTY = static_cast<int>(std::floor(localActorMinY / tm.tileSize));
                    int maxTY = static_cast<int>(std::floor(localActorMaxY / tm.tileSize));

                    for (int ty = minTY; ty <= maxTY; ++ty) {
                        for (int tx = minTX; tx <= maxTX; ++tx) {
                            if (!tm.isTileSolid(tx, ty)) continue;

                            float tileMinX = tx * tm.tileSize;
                            float tileMaxX = (tx + 1) * tm.tileSize;
                            float tileMinY = ty * tm.tileSize;
                            float tileMaxY = (ty + 1) * tm.tileSize;

                            float overlapX0 = localActorMaxX - tileMinX;
                            float overlapX1 = tileMaxX - localActorMinX;
                            float overlapY0 = localActorMaxY - tileMinY;
                            float overlapY1 = tileMaxY - localActorMinY;

                            if (overlapX0 > 0 && overlapX1 > 0 && overlapY0 > 0 && overlapY1 > 0) {
                                float minOverlapX = (overlapX0 < overlapX1) ? overlapX0 : -overlapX1;
                                float minOverlapY = (overlapY0 < overlapY1) ? overlapY0 : -overlapY1;

                                glm::vec3 resolveVec(0.0f);
                                if (std::abs(minOverlapX) < std::abs(minOverlapY)) {
                                    resolveVec.x = -minOverlapX;
                                } else {
                                    resolveVec.y = -minOverlapY;
                                }

                                trans.position += resolveVec;
                                localActorPos += resolveVec;
                                localActorMinX += resolveVec.x; localActorMaxX += resolveVec.x;
                                localActorMinY += resolveVec.y; localActorMaxY += resolveVec.y;

                                if (rb) {
                                    if (resolveVec.x != 0.0f) rb->velocity.x = 0.0f;
                                    if (resolveVec.y != 0.0f) rb->velocity.y = 0.0f;
                                    rb->hadContactThisFrame = true;
                                }
                            }
                        }
                    }
                }
            }

            // 3. Post-collision Sleep / Velocity-Floor Pass
            {
                const float sleepLinThresh = 0.08f;   // m/s
                const float sleepAngThresh = 0.15f;   // rad/s

                for (auto [entity, transform, rb] : registry.view<Transform, RigidBodyComponent>()) {
                    if (rb.type == RigidBodyType::Static) continue;
                    if (rb.sleeping) continue;

                    float linSpeed = glm::length(rb.velocity);
                    float angSpeed = glm::length(rb.angularVelocity);

                    if (linSpeed < sleepLinThresh && angSpeed < sleepAngThresh) {
                        if (rb.hadContactThisFrame && !rb.unstableContactThisFrame) {
                            rb.sleepTimer += dt;
                            if (rb.sleepTimer >= 0.5f) {
                                rb.velocity        = glm::vec3(0.0f);
                                rb.angularVelocity = glm::vec3(0.0f);
                                rb.sleeping        = true;
                                rb.sleepTimer      = 0.0f;
                            }
                        } else {
                            rb.sleepTimer = 0.0f;
                        }
                    } else {
                        rb.sleeping   = false;
                        rb.sleepTimer = 0.0f;
                    }
                }
            }
        }

        RaycastHit raycast(const Ray& ray) {
            RaycastHit closestHit;
            closestHit.distance = std::numeric_limits<float>::max();

            for (auto [entity, transform, collider] : registry.view<Transform, ColliderComponent>()) {
                glm::mat4 worldM = getEntityWorldMatrix(entity);
                glm::vec3 colPos = glm::vec3(worldM * glm::vec4(collider.offset, 1.0f));
                RaycastHit hit;

                if (collider.shape == ColliderShape::Sphere) {
                    float scaleX = glm::length(glm::vec3(worldM[0]));
                    float scaleY = glm::length(glm::vec3(worldM[1]));
                    float scaleZ = glm::length(glm::vec3(worldM[2]));
                    float worldRadius = collider.radius * std::max({scaleX, scaleY, scaleZ});

                    glm::vec3 oc = ray.origin - colPos;
                    float b = glm::dot(oc, ray.direction);
                    float c = glm::dot(oc, oc) - worldRadius * worldRadius;
                    
                    if (c > 0.0f && b > 0.0f) continue;
                    float discriminant = b * b - c;
                    
                    if (discriminant >= 0.0f) {
                        float t = -b - std::sqrt(discriminant);
                        if (t < 0.0f) t = -b + std::sqrt(discriminant);
                        if (t >= 0.0f) {
                            hit.hit = true;
                            hit.distance = t;
                            hit.position = ray.origin + ray.direction * t;
                            hit.normal = glm::normalize(hit.position - colPos);
                        }
                    }
                } else if (collider.shape == ColliderShape::AABB) {
                    glm::vec3 aabbMin = colPos - collider.extents;
                    glm::vec3 aabbMax = colPos + collider.extents;

                    float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
                    bool intersect = true;

                    for (int i = 0; i < 3; ++i) {
                        if (std::abs(ray.direction[i]) < 1e-6f) {
                            if (ray.origin[i] < aabbMin[i] || ray.origin[i] > aabbMax[i]) {
                                intersect = false;
                                break;
                            }
                        } else {
                            float invD = 1.0f / ray.direction[i];
                            float t1 = (aabbMin[i] - ray.origin[i]) * invD;
                            float t2 = (aabbMax[i] - ray.origin[i]) * invD;
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) {
                                intersect = false;
                                break;
                            }
                        }
                    }

                    if (intersect && tmax >= 0.0f) {
                        hit.hit = true;
                        hit.distance = tmin;
                        hit.position = ray.origin + ray.direction * tmin;

                        glm::vec3 hitLocal = hit.position - colPos;
                        glm::vec3 normal(0.0f);
                        float minDist = std::numeric_limits<float>::max();
                        for (int i = 0; i < 3; ++i) {
                            float distToMax = std::abs(collider.extents[i] - hitLocal[i]);
                            if (distToMax < minDist) {
                                minDist = distToMax;
                                normal = glm::vec3(0.0f);
                                normal[i] = 1.0f;
                            }
                            float distToMin = std::abs(-collider.extents[i] - hitLocal[i]);
                            if (distToMin < minDist) {
                                minDist = distToMin;
                                normal = glm::vec3(0.0f);
                                normal[i] = -1.0f;
                            }
                        }
                        hit.normal = normal;
                    }
                } else if (collider.shape == ColliderShape::OBB) {
                    glm::mat4 invM = glm::inverse(worldM);
                    glm::vec3 localOrigin = glm::vec3(invM * glm::vec4(ray.origin, 1.0f));
                    glm::vec3 localDir = glm::normalize(glm::vec3(invM * glm::vec4(ray.direction, 0.0f)));

                    glm::vec3 boxMin = collider.offset - collider.extents;
                    glm::vec3 boxMax = collider.offset + collider.extents;

                    float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
                    bool intersect = true;

                    for (int i = 0; i < 3; ++i) {
                        if (std::abs(localDir[i]) < 1e-6f) {
                            if (localOrigin[i] < boxMin[i] || localOrigin[i] > boxMax[i]) {
                                intersect = false;
                                break;
                            }
                        } else {
                            float invD = 1.0f / localDir[i];
                            float t1 = (boxMin[i] - localOrigin[i]) * invD;
                            float t2 = (boxMax[i] - localOrigin[i]) * invD;
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) {
                                intersect = false;
                                break;
                            }
                        }
                    }

                    if (intersect && tmax >= 0.0f) {
                        glm::vec3 localHitPos = localOrigin + localDir * tmin;
                        glm::vec3 worldHitPos = glm::vec3(worldM * glm::vec4(localHitPos, 1.0f));

                        hit.hit = true;
                        hit.distance = glm::distance(ray.origin, worldHitPos);
                        hit.position = worldHitPos;

                        glm::vec3 hitLocal = localHitPos - collider.offset;
                        glm::vec3 localNormal(0.0f);
                        float minDist = std::numeric_limits<float>::max();
                        for (int i = 0; i < 3; ++i) {
                            float distToMax = std::abs(collider.extents[i] - hitLocal[i]);
                            if (distToMax < minDist) {
                                minDist = distToMax;
                                localNormal = glm::vec3(0.0f);
                                localNormal[i] = 1.0f;
                            }
                            float distToMin = std::abs(-collider.extents[i] - hitLocal[i]);
                            if (distToMin < minDist) {
                                minDist = distToMin;
                                localNormal = glm::vec3(0.0f);
                                localNormal[i] = -1.0f;
                            }
                        }
                        hit.normal = glm::normalize(glm::vec3(worldM * glm::vec4(localNormal, 0.0f)));
                    }
                }

                if (hit.hit && hit.distance < closestHit.distance) {
                    closestHit = hit;
                    closestHit.entity = entity;
                }
            }

            return closestHit;
        }

    private:
        struct OBB {
            glm::vec3 center{0.0f};
            glm::vec3 axes[3]{glm::vec3(1,0,0), glm::vec3(0,1,0), glm::vec3(0,0,1)};
            glm::vec3 extents{0.5f};
        };

        glm::mat4 getEntityWorldMatrix(Entity entity, int depth = 0) {
            if (depth > 100) return glm::mat4(1.0f);
            glm::mat4 m = glm::mat4(1.0f);
            if (auto* t = registry.get<Transform>(entity)) {
                m = t->matrix();
            }
            if (auto* h = registry.get<HierarchyComponent>(entity)) {
                if (h->parent.getId() != Entity::INVALID_ENTITY && registry.isValid(h->parent)) {
                    m = getEntityWorldMatrix(h->parent, depth + 1) * m;
                }
            }
            return m;
        }

        OBB getOBB(Entity entity, const ColliderComponent& col) {
            glm::mat4 worldM = getEntityWorldMatrix(entity);
            
            OBB obb;
            obb.center = glm::vec3(worldM * glm::vec4(col.offset, 1.0f));
            
            glm::vec3 baseExtents = col.extents;
            if (col.shape == ColliderShape::Capsule) {
                baseExtents = glm::vec3(col.radius, col.height * 0.5f, col.radius);
            }

            for (int i = 0; i < 3; ++i) {
                glm::vec3 colAxis = glm::vec3(worldM[i]);
                float axisLen = glm::length(colAxis);
                obb.axes[i] = (axisLen > 1e-4f) ? (colAxis / axisLen) : glm::vec3(i == 0, i == 1, i == 2);
                obb.extents[i] = baseExtents[i] * (axisLen > 1e-4f ? axisLen : 1.0f);
            }
            return obb;
        }

        OBB getOBBFromAABB(const ColliderComponent& col, const glm::vec3& pos) {
            OBB obb;
            obb.center = pos;
            obb.axes[0] = glm::vec3(1.0f, 0.0f, 0.0f);
            obb.axes[1] = glm::vec3(0.0f, 1.0f, 0.0f);
            obb.axes[2] = glm::vec3(0.0f, 0.0f, 1.0f);
            
            if (col.shape == ColliderShape::Capsule) {
                obb.extents = glm::vec3(col.radius, col.height * 0.5f, col.radius);
            } else {
                obb.extents = col.extents;
            }
            return obb;
        }

        CollisionInfo checkSphereAABB(const ColliderComponent& sphere, const glm::vec3& sPos,
                                       const ColliderComponent& aabb, const glm::vec3& aPos,
                                       bool flipNormal) {
            CollisionInfo info;
            glm::vec3 closestPoint = glm::clamp(sPos, aPos - aabb.extents, aPos + aabb.extents);
            float dist = glm::distance(sPos, closestPoint);

            if (dist < sphere.radius) {
                info.collided = true;
                info.penetration = sphere.radius - dist;

                glm::vec3 dir = sPos - closestPoint;
                float dirLen = glm::length(dir);
                if (dirLen > 1e-4f) {
                    info.normal = glm::normalize(dir);
                } else {
                    glm::vec3 offset = sPos - aPos;
                    float minOverlap = std::numeric_limits<float>::max();
                    glm::vec3 pushNormal(0.0f, 1.0f, 0.0f);

                    for (int i = 0; i < 3; ++i) {
                        float overlap = aabb.extents[i] - std::abs(offset[i]);
                        if (overlap < minOverlap) {
                            minOverlap = overlap;
                            pushNormal = glm::vec3(0.0f);
                            pushNormal[i] = (offset[i] > 0.0f) ? 1.0f : -1.0f;
                        }
                    }
                    info.penetration = sphere.radius + minOverlap;
                    info.normal = pushNormal;
                }

                if (flipNormal) {
                    info.normal = -info.normal;
                }
                info.contactPoints[0] = closestPoint;
                info.contactPenetrations[0] = info.penetration;
                info.contactCount = 1;
            }
            return info;
        }

        CollisionInfo checkOBBSphere(const OBB& obb, const glm::vec3& sphereCenter, float sphereRadius, bool flipNormal) {
            CollisionInfo info;
            
            glm::vec3 relCenter = sphereCenter - obb.center;
            glm::vec3 localSpherePos(
                glm::dot(relCenter, obb.axes[0]),
                glm::dot(relCenter, obb.axes[1]),
                glm::dot(relCenter, obb.axes[2])
            );
            
            glm::vec3 localClosestPoint = glm::clamp(localSpherePos, -obb.extents, obb.extents);
            
            glm::vec3 worldClosestPoint = obb.center +
                localClosestPoint.x * obb.axes[0] +
                localClosestPoint.y * obb.axes[1] +
                localClosestPoint.z * obb.axes[2];
                
            float dist = glm::distance(sphereCenter, worldClosestPoint);
            
            if (dist < sphereRadius) {
                info.collided = true;
                info.penetration = sphereRadius - dist;
                
                glm::vec3 dir = sphereCenter - worldClosestPoint;
                float dirLen = glm::length(dir);
                if (dirLen > 1e-4f) {
                    info.normal = dir / dirLen;
                } else {
                    float minOverlap = std::numeric_limits<float>::max();
                    glm::vec3 localPushNormal(0.0f, 1.0f, 0.0f);
                    
                    for (int i = 0; i < 3; ++i) {
                        float overlap = obb.extents[i] - std::abs(localSpherePos[i]);
                        if (overlap < minOverlap) {
                            minOverlap = overlap;
                            localPushNormal = glm::vec3(0.0f);
                            localPushNormal[i] = (localSpherePos[i] > 0.0f) ? 1.0f : -1.0f;
                        }
                    }
                    info.penetration = sphereRadius + minOverlap;
                    info.normal = glm::normalize(obb.center +
                        localPushNormal.x * obb.axes[0] +
                        localPushNormal.y * obb.axes[1] +
                        localPushNormal.z * obb.axes[2] - obb.center);
                }
                
                if (flipNormal) {
                    info.normal = -info.normal;
                }
                info.contactPoints[0] = worldClosestPoint;
                info.contactPenetrations[0] = info.penetration;
                info.contactCount = 1;
            }
            
            return info;
        }

        CollisionInfo checkOBBOBB(const OBB& obbA, const OBB& obbB) {
            CollisionInfo info;
            
            glm::vec3 T = obbB.center - obbA.center;
            glm::vec3 candidateAxes[15];
            int axisCount = 0;
            
            for (int i = 0; i < 3; ++i) candidateAxes[axisCount++] = obbA.axes[i];
            for (int i = 0; i < 3; ++i) candidateAxes[axisCount++] = obbB.axes[i];
            for (int i = 0; i < 3; ++i) {
                for (int j = 0; j < 3; ++j) {
                    glm::vec3 axis = glm::cross(obbA.axes[i], obbB.axes[j]);
                    float axisLen = glm::length(axis);
                    if (axisLen > 1e-4f) {
                        candidateAxes[axisCount++] = axis / axisLen;
                    }
                }
            }
            
            float minBiasedOverlap = std::numeric_limits<float>::max();
            float bestUnbiasedOverlap = 0.0f;
            glm::vec3 bestAxis(0.0f);
            
            for (int i = 0; i < axisCount; ++i) {
                glm::vec3 L = candidateAxes[i];
                float d = std::abs(glm::dot(T, L));
                
                float rA = obbA.extents.x * std::abs(glm::dot(obbA.axes[0], L)) +
                           obbA.extents.y * std::abs(glm::dot(obbA.axes[1], L)) +
                           obbA.extents.z * std::abs(glm::dot(obbA.axes[2], L));
                           
                float rB = obbB.extents.x * std::abs(glm::dot(obbB.axes[0], L)) +
                           obbB.extents.y * std::abs(glm::dot(obbB.axes[1], L)) +
                           obbB.extents.z * std::abs(glm::dot(obbB.axes[2], L));
                           
                float overlap = (rA + rB) - d;
                if (overlap <= 0.0f) return info;
                
                float biasedOverlap = overlap;
                if (i >= 6) biasedOverlap *= 1.15f; // Face preference bias
                
                if (biasedOverlap < minBiasedOverlap) {
                    minBiasedOverlap = biasedOverlap;
                    bestUnbiasedOverlap = overlap;
                    bestAxis = L;
                }
            }
            
            info.collided = true;
            info.penetration = bestUnbiasedOverlap;
            if (glm::dot(T, bestAxis) < 0.0f) bestAxis = -bestAxis;
            info.normal = bestAxis;
            
            int bestAxisIndex = -1;
            for (int i = 0; i < axisCount; ++i) {
                if (glm::length(candidateAxes[i] - bestAxis) < 1e-3f || glm::length(candidateAxes[i] + bestAxis) < 1e-3f) {
                    bestAxisIndex = i;
                    break;
                }
            }

            if (bestAxisIndex >= 0 && bestAxisIndex < 6) {
                // Face contact: project vertices of both boxes to construct a stable manifold
                glm::vec3 planePointA = obbA.center;
                for (int i = 0; i < 3; ++i) {
                    planePointA += (glm::dot(obbA.axes[i], bestAxis) >= 0.0f ? 1.0f : -1.0f) * obbA.extents[i] * obbA.axes[i];
                }
                
                glm::vec3 planePointB = obbB.center;
                for (int i = 0; i < 3; ++i) {
                    planePointB += (glm::dot(obbB.axes[i], -bestAxis) >= 0.0f ? 1.0f : -1.0f) * obbB.extents[i] * obbB.axes[i];
                }
                
                // Collect vertices of Box B penetrating A's face boundary bounds
                glm::vec3 verticesB[8];
                int idx = 0;
                for (float x : {-1.0f, 1.0f}) {
                    for (float y : {-1.0f, 1.0f}) {
                        for (float z : {-1.0f, 1.0f}) {
                            verticesB[idx++] = obbB.center + x * obbB.extents.x * obbB.axes[0] + y * obbB.extents.y * obbB.axes[1] + z * obbB.extents.z * obbB.axes[2];
                        }
                    }
                }
                for (const auto& V : verticesB) {
                    float depth = glm::dot(planePointA - V, bestAxis);
                    if (depth > -1e-4f) {
                        glm::vec3 toV = V - obbA.center;
                        bool inside = true;
                        for (int i = 0; i < 3; ++i) {
                            if (std::abs(glm::dot(toV, obbA.axes[i])) > obbA.extents[i] + 0.05f) { inside = false; break; }
                        }
                        if (inside && info.contactCount < 8) {
                            info.contactPoints[info.contactCount] = V;
                            info.contactPenetrations[info.contactCount++] = std::max(0.0f, depth);
                        }
                    }
                }
                
                // Collect vertices of Box A penetrating B's face boundary bounds
                glm::vec3 verticesA[8];
                idx = 0;
                for (float x : {-1.0f, 1.0f}) {
                    for (float y : {-1.0f, 1.0f}) {
                        for (float z : {-1.0f, 1.0f}) {
                            verticesA[idx++] = obbA.center + x * obbA.extents.x * obbA.axes[0] + y * obbA.extents.y * obbA.axes[1] + z * obbA.extents.z * obbA.axes[2];
                        }
                    }
                }
                for (const auto& V : verticesA) {
                    float depth = glm::dot(V - planePointB, bestAxis);
                    if (depth > -1e-4f) {
                        glm::vec3 toV = V - obbB.center;
                        bool inside = true;
                        for (int i = 0; i < 3; ++i) {
                            if (std::abs(glm::dot(toV, obbB.axes[i])) > obbB.extents[i] + 0.05f) { inside = false; break; }
                        }
                        if (inside && info.contactCount < 8) {
                            info.contactPoints[info.contactCount] = V;
                            info.contactPenetrations[info.contactCount++] = std::max(0.0f, depth);
                        }
                    }
                }
            }

            // Fallback for edge-edge contacts or missed feature clipping pairs
            if (info.contactCount == 0) {
                glm::vec3 supportA = obbA.center;
                for (int i = 0; i < 3; ++i) supportA += (glm::dot(obbA.axes[i], bestAxis) >= 0.0f ? 1.0f : -1.0f) * obbA.extents[i] * obbA.axes[i];
                glm::vec3 supportB = obbB.center;
                for (int i = 0; i < 3; ++i) supportB += (glm::dot(obbB.axes[i], -bestAxis) >= 0.0f ? 1.0f : -1.0f) * obbB.extents[i] * obbB.axes[i];

                if (bestAxisIndex >= 0 && bestAxisIndex < 3) info.contactPoints[0] = supportB;
                else if (bestAxisIndex >= 3 && bestAxisIndex < 6) info.contactPoints[0] = supportA;
                else info.contactPoints[0] = 0.5f * (supportA + supportB);
                
                info.contactPenetrations[0] = bestUnbiasedOverlap;
                info.contactCount = 1;
            }
            
            return info;
        }

        CollisionInfo checkCollision(Entity entityA, const ColliderComponent& colA,
                                     Entity entityB, const ColliderComponent& colB) {
            CollisionInfo info;
            glm::mat4 worldMatrixA = getEntityWorldMatrix(entityA);
            glm::mat4 worldMatrixB = getEntityWorldMatrix(entityB);
            glm::vec3 posA = glm::vec3(worldMatrixA * glm::vec4(colA.offset, 1.0f));
            glm::vec3 posB = glm::vec3(worldMatrixB * glm::vec4(colB.offset, 1.0f));

            if (colA.shape == ColliderShape::Sphere && colB.shape == ColliderShape::Sphere) {
                float rSum = colA.radius + colB.radius;
                float dist = glm::distance(posA, posB);
                if (dist < rSum) {
                    info.collided = true;
                    info.penetration = rSum - dist;
                    glm::vec3 dir = posB - posA;
                    float dirLen = glm::length(dir);
                    info.normal = (dirLen > 1e-4f) ? glm::normalize(dir) : glm::vec3(0.0f, 1.0f, 0.0f);
                    info.contactPoints[0] = (dirLen > 1e-4f) ? (posA + info.normal * colA.radius) : (0.5f * (posA + posB));
                    info.contactPenetrations[0] = info.penetration;
                    info.contactCount = 1;
                }
            } else if (colA.shape == ColliderShape::Sphere && colB.shape == ColliderShape::AABB) {
                return checkSphereAABB(colA, posA, colB, posB, true);
            } else if (colA.shape == ColliderShape::AABB && colB.shape == ColliderShape::Sphere) {
                return checkSphereAABB(colB, posB, colA, posA, false);
            } else if (colA.shape == ColliderShape::AABB && colB.shape == ColliderShape::AABB) {
                float overlapX = (colA.extents.x + colB.extents.x) - std::abs(posB.x - posA.x);
                float overlapY = (colA.extents.y + colB.extents.y) - std::abs(posB.y - posA.y);
                float overlapZ = (colA.extents.z + colB.extents.z) - std::abs(posB.z - posA.z);
                if (overlapX > 0.0f && overlapY > 0.0f && overlapZ > 0.0f) {
                    info.collided = true;
                    if (overlapX < overlapY && overlapX < overlapZ) {
                        info.penetration = overlapX;
                        info.normal = glm::vec3((posB.x > posA.x) ? 1.0f : -1.0f, 0.0f, 0.0f);
                    } else if (overlapY < overlapX && overlapY < overlapZ) {
                        info.penetration = overlapY;
                        info.normal = glm::vec3(0.0f, (posB.y > posA.y) ? 1.0f : -1.0f, 0.0f);
                    } else {
                        info.penetration = overlapZ;
                        info.normal = glm::vec3(0.0f, 0.0f, (posB.z > posA.z) ? 1.0f : -1.0f);
                    }
                    glm::vec3 supportA = posA + info.normal * colA.extents;
                    glm::vec3 supportB = posB - info.normal * colB.extents;
                    info.contactPoints[0] = 0.5f * (supportA + supportB);
                    info.contactPenetrations[0] = info.penetration;
                    info.contactCount = 1;
                }
            } else if (colA.shape == ColliderShape::Sphere && colB.shape == ColliderShape::OBB) {
                return checkOBBSphere(getOBB(entityB, colB), posA, colA.radius, true);
            } else if (colA.shape == ColliderShape::OBB && colB.shape == ColliderShape::Sphere) {
                return checkOBBSphere(getOBB(entityA, colA), posB, colB.radius, false);
            } else {
                OBB obbA = (colA.shape == ColliderShape::OBB) ? getOBB(entityA, colA) : getOBBFromAABB(colA, posA);
                OBB obbB = (colB.shape == ColliderShape::OBB) ? getOBB(entityB, colB) : getOBBFromAABB(colB, posB);
                return checkOBBOBB(obbA, obbB);
            }
            return info;
        }

        void resolveCollision(Entity entityA, Transform& transA, ColliderComponent& colA,
                              Entity entityB, Transform& transB, ColliderComponent& colB,
                              const CollisionInfo& colInfo, float dt) {
            RigidBodyComponent* rbA = registry.get<RigidBodyComponent>(entityA);
            RigidBodyComponent* rbB = registry.get<RigidBodyComponent>(entityB);

            bool staticA = !rbA || (rbA->type == RigidBodyType::Static);
            bool staticB = !rbB || (rbB->type == RigidBodyType::Static);
            if (staticA && staticB) return;

            glm::mat4 worldMatrixA = getEntityWorldMatrix(entityA);
            glm::mat4 worldMatrixB = getEntityWorldMatrix(entityB);
            OBB obbA = (colA.shape == ColliderShape::OBB) ? getOBB(entityA, colA) : getOBBFromAABB(colA, glm::vec3(worldMatrixA * glm::vec4(colA.offset, 1.0f)));
            OBB obbB = (colB.shape == ColliderShape::OBB) ? getOBB(entityB, colB) : getOBBFromAABB(colB, glm::vec3(worldMatrixB * glm::vec4(colB.offset, 1.0f)));

            glm::vec3 normal = glm::normalize(colInfo.normal);

            if (staticA || staticB) {
                const OBB& staticOBB = staticA ? obbA : obbB;
                float maxDot = 0.0f; int bestAxisIndex = -1; float bestSign = 1.0f;
                for (int i = 0; i < 3; ++i) {
                    float dotVal = glm::dot(normal, staticOBB.axes[i]);
                    if (std::abs(dotVal) > maxDot) { maxDot = std::abs(dotVal); bestAxisIndex = i; bestSign = (dotVal >= 0.0f) ? 1.0f : -1.0f; }
                }
                if (maxDot > 0.95f) normal = bestSign * staticOBB.axes[bestAxisIndex];
            }

            if (!staticA) rbA->hadContactThisFrame = true;
            if (!staticB) rbB->hadContactThisFrame = true;

            bool aSleeping = rbA && rbA->sleeping;
            bool bSleeping = rbB && rbB->sleeping;
            auto wakeIfMoving = [](RigidBodyComponent* rb, RigidBodyComponent* other, bool otherStatic) {
                if (!rb || !rb->sleeping || otherStatic || !other || other->sleeping) return;
                rb->sleeping = false; rb->sleepTimer = 0.0f;
            };
            wakeIfMoving(rbA, rbB, staticB); wakeIfMoving(rbB, rbA, staticA);

            aSleeping = rbA && rbA->sleeping; bSleeping = rbB && rbB->sleeping;
            bool skipVelocityA = aSleeping && (staticB || bSleeping);
            bool skipVelocityB = bSleeping && (staticA || aSleeping);

            // --- Compute Proper Inertia Tensors (1/12th scale factor) ---
            glm::mat3 invInertiaWorldA(0.0f);
            float invMassA = (staticA || rbA->mass < 1e-4f) ? 0.0f : 1.0f / rbA->mass;
            if (!staticA && rbA->mass > 1e-4f) {
                glm::mat3 R = glm::mat3(obbA.axes[0], obbA.axes[1], obbA.axes[2]);
                glm::mat3 invInertiaLocal(0.0f);
                if (colA.shape == ColliderShape::Sphere) {
                    invInertiaLocal = glm::mat3(2.5f / (rbA->mass * colA.radius * colA.radius));
                } else {
                    float ex = obbA.extents.x; float ey = obbA.extents.y; float ez = obbA.extents.z;
                    invInertiaLocal[0][0] = 1.0f / ((1.0f / 12.0f) * rbA->mass * (ey * ey + ez * ez));
                    invInertiaLocal[1][1] = 1.0f / ((1.0f / 12.0f) * rbA->mass * (ex * ex + ez * ez));
                    invInertiaLocal[2][2] = 1.0f / ((1.0f / 12.0f) * rbA->mass * (ex * ex + ey * ey));
                }
                invInertiaWorldA = R * invInertiaLocal * glm::transpose(R);
            }

            glm::mat3 invInertiaWorldB(0.0f);
            float invMassB = (staticB || rbB->mass < 1e-4f) ? 0.0f : 1.0f / rbB->mass;
            if (!staticB && rbB->mass > 1e-4f) {
                glm::mat3 R = glm::mat3(obbB.axes[0], obbB.axes[1], obbB.axes[2]);
                glm::mat3 invInertiaLocal(0.0f);
                if (colB.shape == ColliderShape::Sphere) {
                    invInertiaLocal = glm::mat3(2.5f / (rbB->mass * colB.radius * colB.radius));
                } else {
                    float ex = obbB.extents.x; float ey = obbB.extents.y; float ez = obbB.extents.z;
                    invInertiaLocal[0][0] = 1.0f / ((1.0f / 12.0f) * rbB->mass * (ey * ey + ez * ez));
                    invInertiaLocal[1][1] = 1.0f / ((1.0f / 12.0f) * rbB->mass * (ex * ex + ez * ez));
                    invInertiaLocal[2][2] = 1.0f / ((1.0f / 12.0f) * rbB->mass * (ex * ex + ey * ey));
                }
                invInertiaWorldB = R * invInertiaLocal * glm::transpose(R);
            }

            auto computeK = [&](float invM_A, float invM_B, const glm::mat3& invI_A, const glm::mat3& invI_B, const glm::vec3& r_A, const glm::vec3& r_B, const glm::vec3& dir) {
                float K = invM_A + invM_B;
                if (invM_A > 0.0f) K += glm::dot(glm::cross(invI_A * glm::cross(r_A, dir), r_A), dir);
                if (invM_B > 0.0f) K += glm::dot(glm::cross(invI_B * glm::cross(r_B, dir), r_B), dir);
                return K;
            };

            int contactCount = colInfo.contactCount;
            if (contactCount == 0) return;

            // --- 1. Instant Position Projection ---
            float slop = 0.0001f;
            float baumgarte = 0.5f;
            for (int cp = 0; cp < contactCount; ++cp) {
                glm::vec3 rA = colInfo.contactPoints[cp] - transA.position;
                glm::vec3 rB = colInfo.contactPoints[cp] - transB.position;
                float penetration = colInfo.contactPenetrations[cp];

                if (penetration > slop) {
                    float K_pos = computeK(invMassA, invMassB, invInertiaWorldA, invInertiaWorldB, rA, rB, normal);
                    if (K_pos > 1e-4f) {
                        float J_p = ((penetration - slop) * baumgarte / K_pos) / static_cast<float>(contactCount);
                        glm::vec3 C_p = J_p * normal;
                        if (!staticA) {
                            transA.position -= invMassA * C_p;
                            applyAngularDisplacement(transA, -invInertiaWorldA * glm::cross(rA, C_p));
                        }
                        if (!staticB) {
                            transB.position += invMassB * C_p;
                            applyAngularDisplacement(transB, invInertiaWorldB * glm::cross(rB, C_p));
                        }
                    }
                }
            }

            if (skipVelocityA && skipVelocityB) return;

            // --- 2. Cache Initial Velocities for Stable Manifold Pass Evaluation ---
            glm::vec3 initialVelA = rbA ? rbA->velocity : glm::vec3(0.0f);
            glm::vec3 initialVelB = rbB ? rbB->velocity : glm::vec3(0.0f);
            glm::vec3 initialAngVelA = rbA ? rbA->angularVelocity : glm::vec3(0.0f);
            glm::vec3 initialAngVelB = rbB ? rbB->angularVelocity : glm::vec3(0.0f);

            glm::vec3 accumulatedVelA{0.0f}, accumulatedVelB{0.0f};
            glm::vec3 accumulatedAngVelA{0.0f}, accumulatedAngVelB{0.0f};
            float totalImpulseScalar = 0.0f;

            for (int cp = 0; cp < contactCount; ++cp) {
                glm::vec3 rA = colInfo.contactPoints[cp] - transA.position;
                glm::vec3 rB = colInfo.contactPoints[cp] - transB.position;

                glm::vec3 contactVelA = initialVelA + glm::cross(initialAngVelA, rA);
                glm::vec3 contactVelB = initialVelB + glm::cross(initialAngVelB, rB);
                glm::vec3 relativeVel = contactVelB - contactVelA;

                float velAlongNormal = glm::dot(relativeVel, normal);
                float restitutionA = rbA ? rbA->restitution : 0.5f;
                float restitutionB = rbB ? rbB->restitution : 0.5f;
                float e = std::min(restitutionA, restitutionB);

                if (velAlongNormal > -0.25f || glm::dot(initialVelB - initialVelA, normal) > -0.25f) e = 0.0f;

                float K_vel = computeK(invMassA, invMassB, invInertiaWorldA, invInertiaWorldB, rA, rB, normal);
                float impulseScalar = 0.0f;
                
                if (K_vel > 1e-4f && velAlongNormal < 0.0f) {
                    impulseScalar = (-(1.0f + e) * velAlongNormal / K_vel) / static_cast<float>(contactCount);
                }

                if (impulseScalar > 0.0f) {
                    glm::vec3 impulse = impulseScalar * normal;
                    if (!staticA && !skipVelocityA) {
                        accumulatedVelA -= invMassA * impulse;
                        accumulatedAngVelA -= invInertiaWorldA * glm::cross(rA, impulse);
                    }
                    if (!staticB && !skipVelocityB) {
                        accumulatedVelB += invMassB * impulse;
                        accumulatedAngVelB += invInertiaWorldB * glm::cross(rB, impulse);
                    }
                    totalImpulseScalar += impulseScalar;
                }

                // --- Friction Processing ---
                glm::vec3 tangent = relativeVel - glm::dot(relativeVel, normal) * normal;
                float tangentLen = glm::length(tangent);
                if (tangentLen > 1e-5f) {
                    tangent = glm::normalize(tangent);
                    float K_tangent = computeK(invMassA, invMassB, invInertiaWorldA, invInertiaWorldB, rA, rB, tangent);
                    if (K_tangent > 1e-4f) {
                        float tangentImpulseScalar = (-tangentLen / K_tangent) / static_cast<float>(contactCount);
                        float frictionA = rbA ? rbA->friction : 0.3f;
                        float frictionB = rbB ? rbB->friction : 0.3f;
                        float mu = std::sqrt(frictionA * frictionB);
                        
                        float effectiveNormalImpulse = impulseScalar;
                        if (effectiveNormalImpulse < 1e-5f && colInfo.contactPenetrations[cp] > 1e-5f) {
                            float activeMass = (rbA && !staticA) ? rbA->mass : ((rbB && !staticB) ? rbB->mass : 1.0f);
                            effectiveNormalImpulse = (activeMass * 9.81f * dt) / static_cast<float>(contactCount);
                        }
                        float maxFriction = mu * effectiveNormalImpulse;

                        if (std::abs(tangentImpulseScalar) > maxFriction) {
                            tangentImpulseScalar = (tangentImpulseScalar > 0.0f) ? maxFriction : -maxFriction;
                        }

                        glm::vec3 tangentImpulse = tangentImpulseScalar * tangent;
                        if (!staticA && !skipVelocityA) {
                            accumulatedVelA -= invMassA * tangentImpulse;
                            accumulatedAngVelA -= invInertiaWorldA * glm::cross(rA, tangentImpulse);
                        }
                        if (!staticB && !skipVelocityB) {
                            accumulatedVelB += invMassB * tangentImpulse;
                            accumulatedAngVelB += invInertiaWorldB * glm::cross(rB, tangentImpulse);
                        }
                    }
                }
            }

            // Flush stacked velocity changes to bodies
            if (rbA && !staticA && !skipVelocityA) { rbA->velocity += accumulatedVelA; rbA->angularVelocity += accumulatedAngVelA; }
            if (rbB && !staticB && !skipVelocityB) { rbB->velocity += accumulatedVelB; rbB->angularVelocity += accumulatedAngVelB; }

            // --- 3. Torsional Friction ---
            glm::vec3 relAngVel = (rbB ? rbB->angularVelocity : glm::vec3(0.0f)) - (rbA ? rbA->angularVelocity : glm::vec3(0.0f));
            float angVelAlongN = glm::dot(relAngVel, normal);
            if (std::abs(angVelAlongN) > 1e-5f) {
                float K_rot = 0.0f;
                if (!staticA) K_rot += glm::dot(invInertiaWorldA * normal, normal);
                if (!staticB) K_rot += glm::dot(invInertiaWorldB * normal, normal);

                if (K_rot > 1e-5f) {
                    float rotImpulseScalar = -angVelAlongN / K_rot;
                    float frictionA = rbA ? rbA->friction : 0.3f;
                    float frictionB = rbB ? rbB->friction : 0.3f;
                    float maxRotFriction = std::sqrt(frictionA * frictionB) * 0.1f * totalImpulseScalar;

                    if (std::abs(rotImpulseScalar) > maxRotFriction) rotImpulseScalar = (rotImpulseScalar > 0.0f) ? maxRotFriction : -maxRotFriction;
                    glm::vec3 rotImpulse = rotImpulseScalar * normal;

                    if (!staticA && !skipVelocityA) rbA->angularVelocity -= invInertiaWorldA * rotImpulse;
                    if (!staticB && !skipVelocityB) rbB->angularVelocity += invInertiaWorldB * rotImpulse;
                }
            }

            // Enforce hard constraints back on velocities
            auto applyVelocityConstraints = [](RigidBodyComponent* rb) {
                if (!rb) return;
                if (rb->freezePositionX) rb->velocity.x = 0.0f;
                if (rb->freezePositionY) rb->velocity.y = 0.0f;
                if (rb->freezePositionZ) rb->velocity.z = 0.0f;
                if (rb->freezeRotationX) rb->angularVelocity.x = 0.0f;
                if (rb->freezeRotationY) rb->angularVelocity.y = 0.0f;
                if (rb->freezeRotationZ) rb->angularVelocity.z = 0.0f;
            };
            if (!staticA) applyVelocityConstraints(rbA);
            if (!staticB) applyVelocityConstraints(rbB);
        }

        Registry& registry;
        EditorModeState& editorMode;
    };

} // namespace Engine
