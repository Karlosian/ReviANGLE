// Boost: P-core thread pinning (Intel hybrid CPU, AMD heterogeneous)
// ------------------------------------------------------------------------
// On Intel 12th gen+ (Alder Lake / Raptor Lake / Meteor Lake) and AMD
// heterogeneous CPUs (some Ryzen 7000+ mobile and 8000G APUs), the
// scheduler is free to migrate any thread between P-cores and E-cores.
// E-cores can be 50-70% slower per clock and lack AVX2 on some Intels.
// When the render thread randomly lands on an E-core for a few ms,
// you get a visible stutter.
//
// We detect P-cores via GetLogicalProcessorInformationEx with the
// RelationProcessorCore relation, looking at the EfficiencyClass field
// (>0 = P-core on Intel; lower number = slower on AMD). We build an
// affinity mask covering only P-cores and apply it to the render
// thread.
//
// Fallback: on uniform CPUs (every core has the same EfficiencyClass)
// we just leave Windows alone — no-op, no penalty.
//
// We ALSO set SetThreadPriorityBoost(thread, FALSE) on the render
// thread. The "dynamic priority boost" feature briefly raises a
// thread's priority after I/O wake-ups, which sounds nice but on
// modern CPUs actually causes thermal/scheduler jitter. Killing it
// gives more consistent frame times.

#include <windows.h>
#include <vector>
#include "config.hpp"
#include "angle_loader.hpp"

namespace boost_pcore_pin {

    void apply() {
        const auto& cfg = Config::get();
        if (!cfg.pcore_pin) return;

        // 1) Disable dynamic priority boost (helps consistency on every CPU).
        if (SetThreadPriorityBoost(GetCurrentThread(), FALSE)) {
            angle::log("pcore_pin: priority-boost disabled on render thread");
        }

        // 2) Detect P-cores via processor topology.
        DWORD bufLen = 0;
        GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bufLen);
        if (bufLen == 0) {
            angle::log("pcore_pin: GetLogicalProcessorInformationEx returned 0");
            return;
        }

        std::vector<BYTE> buf(bufLen);
        if (!GetLogicalProcessorInformationEx(
                RelationProcessorCore,
                (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)buf.data(),
                &bufLen)) {
            angle::log("pcore_pin: GetLogicalProcessorInformationEx failed err=%lu", GetLastError());
            return;
        }

        // Walk the variable-length blob.
        DWORD_PTR pcoreMask = 0;
        DWORD_PTR allCoreMask = 0;
        BYTE bestClass = 0;       // highest efficiency class seen
        int totalCores = 0;
        std::vector<std::pair<BYTE, DWORD_PTR>> coreMasks; // (effClass, mask)

        BYTE* p = buf.data();
        BYTE* end = buf.data() + bufLen;
        while (p < end) {
            auto* info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*)p;
            if (info->Relationship == RelationProcessorCore) {
                // EfficiencyClass: 0 = lowest perf (E-core); higher = better.
                BYTE eff = info->Processor.EfficiencyClass;
                DWORD_PTR mask = 0;
                for (WORD g = 0; g < info->Processor.GroupCount; ++g) {
                    // Single-group systems (the common case) — just OR in.
                    mask |= info->Processor.GroupMask[g].Mask;
                }
                coreMasks.emplace_back(eff, mask);
                if (eff > bestClass) bestClass = eff;
                allCoreMask |= mask;
                totalCores++;
            }
            p += info->Size;
        }

        if (totalCores == 0) {
            angle::log("pcore_pin: no cores enumerated?!");
            return;
        }

        // Build P-core mask = union of cores at the highest efficiency class.
        int pcoreCount = 0;
        for (auto& pr : coreMasks) {
            if (pr.first == bestClass) {
                pcoreMask |= pr.second;
                pcoreCount++;
            }
        }

        if (pcoreCount == totalCores) {
            // Uniform CPU — pinning is pointless and counter-productive (less
            // scheduler freedom = lower headroom under load).
            angle::log("pcore_pin: uniform CPU (%d cores, eff=%d), skipping pin",
                       totalCores, (int)bestClass);
            return;
        }

        // Hybrid CPU detected — pin render thread to P-cores only.
        // Also set process affinity so worker threads (audio, network) prefer
        // E-cores by elimination. We do this carefully: PROCESS affinity
        // restricts ALL threads, so we keep process affinity = ALL cores
        // (don't restrict) and only pin OUR thread.
        DWORD_PTR prev = SetThreadAffinityMask(GetCurrentThread(), pcoreMask);
        if (prev) {
            angle::log("pcore_pin: render thread pinned to %d P-cores (mask=0x%llX, prev=0x%llX, %d E-cores left for workers)",
                       pcoreCount, (unsigned long long)pcoreMask,
                       (unsigned long long)prev, totalCores - pcoreCount);
        } else {
            angle::log("pcore_pin: SetThreadAffinityMask failed err=%lu", GetLastError());
        }
    }
}
