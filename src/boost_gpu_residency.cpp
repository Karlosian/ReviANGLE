// Boost: GPU memory residency priority
// ------------------------------------------------------------------------
// Under VRAM pressure (DWM compositor, browser GPU process, antivirus
// scanning textures, other games) Windows may evict our textures from
// VRAM to system RAM. On the next frame ANGLE re-uploads them via DMA,
// which causes 3-10 ms hitches that don't show up in any GPU profiler
// — they show up as random stutters.
//
// We get the IDXGIDevice4 from ANGLE's D3D11 device and call
// SetOfferedResourcePriority / OfferResources isn't quite right here;
// the correct API for "don't evict me" is on the per-resource level
// via IDXGIResource::SetEvictionPriority(DXGI_RESOURCE_PRIORITY_*),
// but ANGLE creates and owns all resources, so we can't reach them.
//
// What we CAN do, that's almost as effective and very simple, is:
//   IDXGIDevice1::SetGPUThreadPriority(+7)   already done in low_latency
//   IDXGIAdapter3::SetVideoMemoryReservation — reserve VRAM exclusively
//   IDXGIAdapter3::RegisterVideoMemoryBudgetChangeNotificationEvent —
//     get notified BEFORE we get evicted, so we can MakeResident them
//
// For Reviangle's purpose the simplest effective trick is
// SetVideoMemoryReservation: reserve enough VRAM to fit the game's
// working set, so Windows won't evict us when another app spikes.

#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

// IDXGIDevice4 — {95B4F95F-D8DA-4CA4-9EE6-3B76D5968A10}
static const GUID IID_IDXGIDevice4 = {
    0x95B4F95F, 0xD8DA, 0x4CA4, {0x9E, 0xE6, 0x3B, 0x76, 0xD5, 0x96, 0x8A, 0x10}
};
// IDXGIAdapter3 — {645967A4-1392-4310-A798-8053CE3E93FD}
static const GUID IID_IDXGIAdapter3 = {
    0x645967A4, 0x1392, 0x4310, {0xA7, 0x98, 0x80, 0x53, 0xCE, 0x3E, 0x93, 0xFD}
};

namespace boost_gpu_residency {

    void apply() {
        const auto& cfg = Config::get();
        if (!cfg.gpu_residency) return;
        if (cfg.backend != "d3d11") return;

        auto& a = angle::state();
        if (!a.egl || !a.display) return;

        // Get D3D11 device from ANGLE (same path as boost_low_latency)
        using QueryDisplayAttribFn = int(*)(void*, int, intptr_t*);
        using QueryDeviceAttribFn  = int(*)(void*, int, intptr_t*);
        auto queryDisplay = (QueryDisplayAttribFn)GetProcAddress(a.egl, "eglQueryDisplayAttribEXT");
        auto queryDevice  = (QueryDeviceAttribFn) GetProcAddress(a.egl, "eglQueryDeviceAttribEXT");
        if (!queryDisplay || !queryDevice) return;

        intptr_t eglDevice = 0;
        if (!queryDisplay(a.display, 0x322C, &eglDevice) || !eglDevice) return;
        intptr_t d3d11Dev = 0;
        if (!queryDevice((void*)eglDevice, 0x33A1, &d3d11Dev) || !d3d11Dev) return;

        struct VtblBase {
            HRESULT(STDMETHODCALLTYPE* QueryInterface)(void*, const GUID&, void**);
            ULONG  (STDMETHODCALLTYPE* AddRef)(void*);
            ULONG  (STDMETHODCALLTYPE* Release)(void*);
        };
        auto* devBase = *(VtblBase**)d3d11Dev;

        // QI IDXGIDevice4
        void* dxgiDev4 = nullptr;
        HRESULT hr = devBase->QueryInterface((void*)d3d11Dev, IID_IDXGIDevice4, &dxgiDev4);
        if (FAILED(hr) || !dxgiDev4) {
            angle::log("gpu_residency: IDXGIDevice4 unavailable (Win10 1709+ required)");
            return;
        }

        // IDXGIDevice4 vtable:
        //   0..2  IUnknown
        //   3..6  IDXGIObject
        //   7..11 IDXGIDevice (GetAdapter at slot 7)
        //   12..13 IDXGIDevice1
        //   14    IDXGIDevice2::OfferResources / EnqueueSetEvent  (we don't need)
        //   ...
        // Get adapter via slot 7.
        using GetAdapterFn = HRESULT(STDMETHODCALLTYPE*)(void*, void**);
        void** dev4Vtbl = *(void***)dxgiDev4;
        auto getAdapter = (GetAdapterFn)dev4Vtbl[7];
        void* adapter = nullptr;
        hr = getAdapter(dxgiDev4, &adapter);

        auto* dev4Base = *(VtblBase**)dxgiDev4;
        dev4Base->Release(dxgiDev4);

        if (FAILED(hr) || !adapter) {
            angle::log("gpu_residency: GetAdapter failed (0x%lx)", hr);
            return;
        }

        // QI for IDXGIAdapter3 (residency budget API)
        auto* adBase = *(VtblBase**)adapter;
        void* adapter3 = nullptr;
        hr = adBase->QueryInterface(adapter, IID_IDXGIAdapter3, &adapter3);
        adBase->Release(adapter);

        if (FAILED(hr) || !adapter3) {
            angle::log("gpu_residency: IDXGIAdapter3 unavailable");
            return;
        }

        // IDXGIAdapter3 adds 5 methods after IDXGIAdapter2 (which has 18 total):
        //   0..2  IUnknown
        //   3..6  IDXGIObject
        //   7..11 IDXGIAdapter
        //   12..14 IDXGIAdapter1 (GetDesc1)
        //   15..17 IDXGIAdapter2 (GetDesc2)
        //   18 RegisterHardwareContentProtectionTeardownStatusEvent
        //   19 UnregisterHardwareContentProtectionTeardownStatus
        //   20 QueryVideoMemoryInfo(NodeIndex, MemorySegmentGroup, *VideoMemoryInfo)
        //   21 SetVideoMemoryReservation(NodeIndex, MemorySegmentGroup, Reservation)
        //   22 RegisterVideoMemoryBudgetChangeNotificationEvent
        //   23 UnregisterVideoMemoryBudgetChangeNotification
        struct DXGI_QUERY_VIDEO_MEMORY_INFO {
            UINT64 Budget;
            UINT64 CurrentUsage;
            UINT64 AvailableForReservation;
            UINT64 CurrentReservation;
        };
        using QueryVRAMFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, int, DXGI_QUERY_VIDEO_MEMORY_INFO*);
        using SetReservationFn = HRESULT(STDMETHODCALLTYPE*)(void*, UINT, int, UINT64);
        void** ad3Vtbl = *(void***)adapter3;
        auto queryVRAM = (QueryVRAMFn)ad3Vtbl[20];
        auto setRes    = (SetReservationFn)ad3Vtbl[21];

        DXGI_QUERY_VIDEO_MEMORY_INFO info{};
        hr = queryVRAM(adapter3, 0, 0 /*LOCAL = VRAM*/, &info);
        if (SUCCEEDED(hr)) {
            // Reserve ~70% of "AvailableForReservation" — that's the cap
            // Windows lets us claim without affecting other apps. We don't
            // ask for the full budget because that triggers eviction of
            // OTHER apps (DWM, browser) — counter-productive.
            UINT64 want = (info.AvailableForReservation * 7) / 10;
            // But not less than 128MB and not more than ~2GB (GD's working set
            // is well under that even with hi-res textures).
            const UINT64 MIN_RES = 128ull * 1024 * 1024;
            const UINT64 MAX_RES = 2048ull * 1024 * 1024;
            if (want < MIN_RES) want = MIN_RES;
            if (want > MAX_RES) want = MAX_RES;
            if (want > info.AvailableForReservation) want = info.AvailableForReservation;

            hr = setRes(adapter3, 0, 0, want);
            if (SUCCEEDED(hr)) {
                angle::log("gpu_residency: reserved %llu MB VRAM (budget=%llu MB, available=%llu MB)",
                           (unsigned long long)(want / (1024*1024)),
                           (unsigned long long)(info.Budget / (1024*1024)),
                           (unsigned long long)(info.AvailableForReservation / (1024*1024)));
            } else {
                angle::log("gpu_residency: SetVideoMemoryReservation failed (0x%lx)", hr);
            }
        } else {
            angle::log("gpu_residency: QueryVideoMemoryInfo failed (0x%lx)", hr);
        }

        auto* ad3Base = *(VtblBase**)adapter3;
        ad3Base->Release(adapter3);
    }
}
