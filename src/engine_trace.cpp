// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "engine_trace.h"

#include <atomic>
#include <cmath>
#include <mutex>
#include <cstdint>
#include <cstring>

#include <cameraunlock/unreal/ue_runtime.h>

#include "builds/build_registry.h"
#include "logging.h"
#include "ue_abi.h"

namespace ecr_ht::trace {

namespace {

using cameraunlock::camera::LeanObstruction;
using cameraunlock::math::Vec3;

// The shipping signatures, read off the exec thunks rather than the UE4 headers:
// a shipping build compiles out the debug colour and duration arguments.
using LineTraceSingle_t = bool(__fastcall*)(void* world_context,
                                            const FVector3f* start,
                                            const FVector3f* end,
                                            int trace_channel,
                                            bool trace_complex,
                                            const void* actors_to_ignore,
                                            int draw_debug_type,
                                            void* out_hit,
                                            bool ignore_self);

using SphereTraceSingle_t = bool(__fastcall*)(void* world_context,
                                              const FVector3f* start,
                                              const FVector3f* end,
                                              float radius,
                                              int trace_channel,
                                              bool trace_complex,
                                              const void* actors_to_ignore,
                                              int draw_debug_type,
                                              void* out_hit,
                                              bool ignore_self);

// UE4's TArray is {allocator data pointer, int32 Num, int32 Max}. The traces
// only read it and ours is always empty, so an empty one satisfies the
// parameter with no allocator to imitate.
struct ActorArray {
    void* data;
    std::int32_t num;
    std::int32_t max;
};

// Larger than any FHitResult a profile can name, so the engine's write always
// lands inside our own frame. The profile's size is what gets zeroed, and it is
// checked against this before the first call.
constexpr std::size_t kHitResultCapacity = 256;

LineTraceSingle_t g_lineTrace = nullptr;
SphereTraceSingle_t g_sphereTrace = nullptr;

std::atomic<int> g_aimChannel{0};
std::atomic<int> g_leanChannel{0};
std::atomic<float> g_leanRadius{15.0f};
std::atomic<bool> g_resolved{false};

FVector3f ToEngine(const ue::FVector& v) {
    return FVector3f{static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)};
}

// Runs whichever trace the caller set up in `invoke` and reports the contact.
// Both queries want the same three things out of FHitResult - was it blocked,
// where did the swept/traced shape stop, how far is that from the start - so the
// buffer, its guards and the read live here once.
struct Contact {
    bool queried = false;
    bool blocked = false;
    bool start_penetrating = false;
    FVector3f location{0.0f, 0.0f, 0.0f};
    float distance = 0.0f;
};

template <typename Invoke>
Contact Run(const FVector3f& from, Invoke&& invoke) {
    Contact out;
    const OffsetTable& offsets = Offsets();

    alignas(16) unsigned char hit[kHitResultCapacity];
    std::memset(hit, 0, offsets.kHitResultSize);
    const ActorArray ignore_list{nullptr, 0, 0};

    // The return value is discarded on purpose: the engine fills bBlockingHit
    // whatever it returns, and reading the struct keeps this independent of how
    // the shipping build passes a bool back.
    invoke(&ignore_list, hit);

    const unsigned flags = hit[offsets.kHitResultBlockingHitOffset];

    out.queried = true;
    out.blocked = (flags & 1u) != 0;
    // Bit 1 of the same byte is bStartPenetrating. Reported raw here: what it
    // means depends on the shape that was swept, so each caller decides.
    out.start_penetrating = (flags & 2u) != 0;
    if (out.blocked) {
        std::memcpy(&out.location, hit + offsets.kHitResultLocationOffset, sizeof(out.location));
        const float dx = out.location.X - from.X;
        const float dy = out.location.Y - from.Y;
        const float dz = out.location.Z - from.Z;
        out.distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return out;
}

// Resolution runs exactly once. The bootstrap thread calls Ready() after the
// camera hook is already live, so the game thread can be inside TraceAim calling
// it at the same moment: without this both threads take the slow path, both
// write the two function pointers, and the resolution line appears twice.
std::once_flag g_resolveOnce;

// Everything Resolve() decides, published together by the once_flag.
void Resolve() {
    const OffsetTable& offsets = Offsets();
    if (offsets.kLineTraceSingleRva == 0 || offsets.kSphereTraceSingleRva == 0) {
        Log::Line("trace: this build profile carries no world queries - the lean "
                  "collision clamp is unavailable, and the crosshair stays where the "
                  "game draws it rather than following the aim");
        return;
    }
    if (offsets.kHitResultSize == 0 || offsets.kHitResultSize > kHitResultCapacity) {
        Log::Line("trace: FHitResult size 0x%zx does not fit our buffer - world queries unavailable",
                  offsets.kHitResultSize);
        return;
    }
    // Both members are read out of the buffer the engine filled, and only the
    // first kHitResultSize bytes of it are zeroed before the call, so a member
    // the profile places outside the struct reads whatever was on the stack.
    if (offsets.kHitResultBlockingHitOffset >= offsets.kHitResultSize ||
        offsets.kHitResultLocationOffset + sizeof(FVector3f) > offsets.kHitResultSize) {
        Log::Line("trace: FHitResult bBlockingHit at 0x%zx / Location at 0x%zx do not both fit "
                  "the 0x%zx-byte struct - world queries unavailable",
                  offsets.kHitResultBlockingHitOffset, offsets.kHitResultLocationOffset,
                  offsets.kHitResultSize);
        return;
    }

    g_lineTrace = reinterpret_cast<LineTraceSingle_t>(
        ue::ModuleBase() + offsets.kLineTraceSingleRva);
    g_sphereTrace = reinterpret_cast<SphereTraceSingle_t>(
        ue::ModuleBase() + offsets.kSphereTraceSingleRva);
    g_resolved.store(true, std::memory_order_release);
    Log::Line("trace: LineTraceSingle @ RVA 0x%08llx, SphereTraceSingle @ RVA 0x%08llx, "
              "FHitResult 0x%zx bytes",
              static_cast<unsigned long long>(offsets.kLineTraceSingleRva),
              static_cast<unsigned long long>(offsets.kSphereTraceSingleRva),
              offsets.kHitResultSize);
}

}  // namespace

void SetAimChannel(int trace_type_query) {
    g_aimChannel.store(trace_type_query, std::memory_order_relaxed);
}

void SetLeanChannel(int trace_type_query) {
    g_leanChannel.store(trace_type_query, std::memory_order_relaxed);
}

void SetLeanRadius(float centimetres) {
    g_leanRadius.store(centimetres, std::memory_order_relaxed);
}

bool Ready() {
    std::call_once(g_resolveOnce, &Resolve);
    return g_resolved.load(std::memory_order_acquire);
}

AimHit TraceAim(void* context, const ue::FVector& start, const ue::FVector& direction,
                float max_distance) {
    AimHit out;
    if (!Ready() || context == nullptr) return out;

    const FVector3f from = ToEngine(start);
    const FVector3f to{from.X + static_cast<float>(direction.X) * max_distance,
                       from.Y + static_cast<float>(direction.Y) * max_distance,
                       from.Z + static_cast<float>(direction.Z) * max_distance};
    const int channel = g_aimChannel.load(std::memory_order_relaxed);

    const Contact contact = Run(from, [&](const void* ignore_list, void* hit) {
        g_lineTrace(context, &from, &to, channel, /*traceComplex*/ false, ignore_list,
                    /*drawDebugType*/ 0, hit, /*ignoreSelf*/ true);
    });

    out.queried = contact.queried;
    out.blocked = contact.blocked;
    out.distance = contact.distance;
    out.point = ue::FVector{contact.location.X, contact.location.Y, contact.location.Z};
    return out;
}

LeanObstruction QueryLean(void* context, const Vec3& start, const Vec3& direction,
                          float max_distance) {
    LeanObstruction out;
    if (!Ready() || context == nullptr) return out;

    const FVector3f from{start.x, start.y, start.z};
    const FVector3f to{start.x + direction.x * max_distance,
                       start.y + direction.y * max_distance,
                       start.z + direction.z * max_distance};
    const float radius = g_leanRadius.load(std::memory_order_relaxed);
    const int channel = g_leanChannel.load(std::memory_order_relaxed);

    const Contact contact = Run(from, [&](const void* ignore_list, void* hit) {
        g_sphereTrace(context, &from, &to, radius, channel, /*traceComplex*/ false, ignore_list,
                      /*drawDebugType*/ 0, hit, /*ignoreSelf*/ true);
    });

    out.queried = contact.queried;
    out.blocked = contact.blocked;
    out.distance = contact.distance;

    // A sweep that began already overlapping geometry answers Time 0 and
    // Location == Start, which reads as a wall at zero distance and refuses the
    // lean in every direction, including away from the surface. That is a real
    // limitation of a 15cm sphere indoors, and it is the SAFE reading: the
    // alternative - reporting the sweep as unqueried - makes core's clamp pass
    // the whole lean through unchecked, which puts the rendered eye inside the
    // wall. A view inside the wall is worse than a lean that will not move.
    if (contact.start_penetrating) {
        out.blocked = true;
        out.distance = 0.0f;
    }
    return out;
}

}  // namespace ecr_ht::trace
