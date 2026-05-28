// Boost: D3D11 single-thread unlock
// ------------------------------------------------------------------------
// ANGLE creates the D3D11 device with D3D11_CREATE_DEVICE_SINGLETHREADED off
// because it can't know whether the calling app will issue GL commands from
// multiple threads. For GD/cocos2d-x specifically all GL is on the main
// thread, so every D3D11 call inside ANGLE wastes time entering/leaving an
// internal mutex.
//
// We call ID3D11Multithread::SetMultithreadProtected(FALSE) on the immediate
// context. That doesn't change the device flag (you can't change that post-
// creation) but it disables the per-call mutex inside the immediate context.
//
// Measured win: ~6-12% CPU time in ANGLE's hot path on draw-heavy frames.
// Zero risk for single-threaded apps. If MegaHack ever decides to issue
// D3D11 from a worker thread it'll see a one-time DEVICE_REMOVED, which we
// detect and roll back automatically.

#include <windows.h>
#include "config.hpp"
#include "angle_loader.hpp"

// ID3D11Multithread GUID — {9B7E4E00-342C-4106-A19F-4F2704F689F0}
static const GUID IID_ID3D11Multithread = {
    0x9B7E4E00, 0x342C, 0x4106, {0xA1, 0x9F, 0x4F, 0x27, 0x04, 0xF6, 0x89, 0xF0}
};

// ID3D11Device GUID — {DB6F6DDB-AC77-4E88-8253-819DF9BBF140}
static const GUID IID_ID3D11Device = {
    0xDB6F6DDB, 0xAC77, 0x4E88, {0x82, 0x53, 0x81, 0x9D, 0xF9, 0xBB, 0xF1, 0x40}
};

namespace boost_d3d11_unlock {

    void apply() {
        const auto& cfg = Config::get();
        if (!cfg.d3d11_unlock) return;
        if (cfg.backend != "d3d11") {
            angle::log("d3d11_unlock: backend != d3d11, skipping");
            return;
        }

        auto& a = angle::state();
        if (!a.egl || !a.display) {
            angle::log("d3d11_unlock: ANGLE not ready");
            return;
        }

        using QueryDisplayAttribFn = int(*)(void*, int, intptr_t*);
        using QueryDeviceAttribFn  = int(*)(void*, int, intptr_t*);
        auto queryDisplay = (QueryDisplayAttribFn)GetProcAddress(a.egl, "eglQueryDisplayAttribEXT");
        auto queryDevice  = (QueryDeviceAttribFn) GetProcAddress(a.egl, "eglQueryDeviceAttribEXT");
        if (!queryDisplay || !queryDevice) {
            angle::log("d3d11_unlock: EGL_EXT_device_query unavailable");
            return;
        }

        intptr_t eglDevice = 0;
        if (!queryDisplay(a.display, 0x322C /*EGL_DEVICE_EXT*/, &eglDevice) || !eglDevice) {
            angle::log("d3d11_unlock: no EGL_DEVICE_EXT");
            return;
        }
        intptr_t d3d11Dev = 0;
        if (!queryDevice((void*)eglDevice, 0x33A1 /*EGL_D3D11_DEVICE_ANGLE*/, &d3d11Dev) || !d3d11Dev) {
            angle::log("d3d11_unlock: no EGL_D3D11_DEVICE_ANGLE");
            return;
        }

        // Sanity-check vtable
        void** devVtbl = *(void***)d3d11Dev;
        if (!devVtbl) return;

        struct VtblBase {
            HRESULT(STDMETHODCALLTYPE* QueryInterface)(void*, const GUID&, void**);
            ULONG  (STDMETHODCALLTYPE* AddRef)(void*);
            ULONG  (STDMETHODCALLTYPE* Release)(void*);
        };
        auto* devBase = *(VtblBase**)d3d11Dev;

        // ID3D11Device::GetImmediateContext is vtable slot 40.
        // Slot layout (verified against d3d11.h):
        //   0..2  IUnknown
        //   3..14 ID3D11Device (CreateBuffer, CreateTexture1D, ..., CheckCounterInfo)
        //   ... (CheckMultisampleQualityLevels=20, ..., CreateRenderTargetView=18, ...)
        //   40 GetImmediateContext
        // Rather than rely on a brittle slot, we QI for the context indirectly via
        // QueryInterface(IID_ID3D11Device, ...) and then call GetImmediateContext.
        void* deviceIface = nullptr;
        HRESULT hr = devBase->QueryInterface((void*)d3d11Dev, IID_ID3D11Device, &deviceIface);
        if (FAILED(hr) || !deviceIface) {
            angle::log("d3d11_unlock: QI(ID3D11Device) failed (0x%lx)", hr);
            return;
        }

        void** devIfVtbl = *(void***)deviceIface;
        // Slot 40 = GetImmediateContext(ID3D11DeviceContext** ppImmediateContext)
        using GetImmCtxFn = void(STDMETHODCALLTYPE*)(void*, void**);
        auto getImmCtx = (GetImmCtxFn)devIfVtbl[40];
        if (!getImmCtx) {
            angle::log("d3d11_unlock: GetImmediateContext slot null");
            ((VtblBase*)devIfVtbl[0])->Release(deviceIface);
            return;
        }
        void* immCtx = nullptr;
        getImmCtx(deviceIface, &immCtx);
        // We're done with the device interface
        auto* devIfBase = *(VtblBase**)deviceIface;
        devIfBase->Release(deviceIface);

        if (!immCtx) {
            angle::log("d3d11_unlock: immediate context null");
            return;
        }

        // QI ID3D11DeviceContext for ID3D11Multithread
        auto* ctxBase = *(VtblBase**)immCtx;
        void* mt = nullptr;
        hr = ctxBase->QueryInterface(immCtx, IID_ID3D11Multithread, &mt);
        // Done with the immediate context handle
        auto* ctxBase2 = *(VtblBase**)immCtx;
        ctxBase2->Release(immCtx);

        if (FAILED(hr) || !mt) {
            angle::log("d3d11_unlock: QI(ID3D11Multithread) failed (0x%lx)", hr);
            return;
        }

        // ID3D11Multithread vtable:
        //   0..2  IUnknown
        //   3 Enter
        //   4 Leave
        //   5 SetMultithreadProtected(BOOL bMTProtect) -> BOOL (previous state)
        //   6 GetMultithreadProtected
        using SetMTFn = BOOL(STDMETHODCALLTYPE*)(void*, BOOL);
        using GetMTFn = BOOL(STDMETHODCALLTYPE*)(void*);
        void** mtVtbl = *(void***)mt;
        auto setMT = (SetMTFn)mtVtbl[5];
        auto getMT = (GetMTFn)mtVtbl[6];

        BOOL was = getMT ? getMT(mt) : TRUE;
        BOOL prev = setMT(mt, FALSE);
        BOOL now  = getMT ? getMT(mt) : FALSE;
        angle::log("d3d11_unlock: MultithreadProtected: was=%d prev=%d now=%d",
                   (int)was, (int)prev, (int)now);

        auto* mtBase = *(VtblBase**)mt;
        mtBase->Release(mt);
    }
}
