// =====================================================================
//  Boost: Process-Wide Thread QoS  (thread_qos)
// =====================================================================
//
//  Что делает: проходит по ВСЕМ существующим потокам нашего процесса
//  и явно выключает Windows EcoQoS / Power Throttling для каждого.
//  Кроме того, ставит "always-high QoS" на уровне процесса, чтобы
//  любые НОВЫЕ потоки наследовали этот флаг.
//
//  Зачем это нужно (что такое EcoQoS):
//    Начиная с Windows 11 (и частично с Win10 1709) ОС умеет помечать
//    "background" потоки и принудительно понижать их частоту CPU
//    через Energy Aware Scheduling. Хорошо для батареи лаптопа в
//    браузере. Плохо для GD: cocos2d audio worker, FMOD mixer thread,
//    наш asset loader и pthread workers получают throttling, что
//    переводится в:
//      - audio dropouts на batt-питании
//      - micro-stutters при первой загрузке текстур
//      - нестабильный 1% low FPS
//
//    boost_anti_stutter уже выключает EcoQoS для main thread.
//    boost_power и boost_gamemode — для процесса целиком (через
//    PROCESS_POWER_THROTTLING). НО per-thread флаг приоритетнее
//    process-wide-флага, поэтому потоки, которые сами себя пометили
//    как Eco (audio engines делают это), всё равно тротлятся.
//
//  Что делаем:
//    1. process-wide PROCESS_POWER_THROTTLING ControlMask=0,
//       StateMask=0 — означает "наследуй системный default" вместо
//       любого ранее выставленного Eco.
//    2. EnumProcessThreads → SetThreadInformation(ThreadPowerThrottling,
//       ControlMask=ExecutionSpeed, StateMask=0) для КАЖДОГО потока.
//    3. Bumpaем background-mode disable.
//
//  Безопасность:
//    - На Windows 7/8/8.1 SetThreadInformation отсутствует, модуль
//       просто silently skip-ает каждый ThreadPowerThrottling-вызов.
//    - НЕ меняет приоритет потоков, НЕ трогает affinity — только
//       Eco/throttle биты.
//    - On battery laptops пользователь может выключить модуль
//       (увеличит расход батареи на 5-10 % при заряде).
//
//  Параметр конфига [BoostExtreme]: thread_qos (default ON).
//
//  Авторы: ReviANGLE / Reviusion
// =====================================================================

#include <windows.h>
#include <tlhelp32.h>
#include "config.hpp"
#include "angle_loader.hpp"

namespace boost_thread_qos {

// ---- forward decls (avoid pulling in full processthreadsapi.h) -----------
#ifndef ProcessPowerThrottling
#define ProcessPowerThrottling 4
#endif
#ifndef ThreadPowerThrottling
#define ThreadPowerThrottling  4
#endif
#ifndef PROCESS_POWER_THROTTLING_EXECUTION_SPEED
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#endif
#ifndef THREAD_POWER_THROTTLING_EXECUTION_SPEED
#define THREAD_POWER_THROTTLING_EXECUTION_SPEED  0x1
#endif

typedef struct _MY_PROCESS_POWER_THROTTLING_STATE {
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} MY_PROCESS_POWER_THROTTLING_STATE;

typedef BOOL (WINAPI* SetProcInfoFn)(HANDLE, int, PVOID, ULONG);
typedef BOOL (WINAPI* SetThreadInfoFn)(HANDLE, int, PVOID, ULONG);

// ---- per-thread visitor --------------------------------------------------
static int s_threadsHardened = 0;

static void hardenThread(HANDLE thread, SetThreadInfoFn fn) {
    MY_PROCESS_POWER_THROTTLING_STATE st = {};
    st.Version     = 1;
    st.ControlMask = THREAD_POWER_THROTTLING_EXECUTION_SPEED;
    st.StateMask   = 0;     // 0 = HighQoS / no throttling
    if (fn(thread, ThreadPowerThrottling, &st, sizeof(st))) {
        s_threadsHardened++;
    }
}

// ---- module entry point --------------------------------------------------
void apply() {
    if (!Config::get().thread_qos) return;

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    if (!k32) return;

    auto setProcInfo   = (SetProcInfoFn)  GetProcAddress(k32, "SetProcessInformation");
    auto setThreadInfo = (SetThreadInfoFn)GetProcAddress(k32, "SetThreadInformation");
    if (!setProcInfo && !setThreadInfo) {
        angle::log("thread_qos: APIs unavailable (Win <10 1709)");
        return;
    }

    // Step 1: process-wide reset — ControlMask=0 means "no override; use
    // system default" which Windows treats as "high perf".
    if (setProcInfo) {
        MY_PROCESS_POWER_THROTTLING_STATE st = {};
        st.Version     = 1;
        st.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        st.StateMask   = 0;
        if (setProcInfo(GetCurrentProcess(), ProcessPowerThrottling, &st, sizeof(st))) {
            angle::log("thread_qos: process-wide EcoQoS disabled");
        }
    }

    // Step 2: enumerate every existing thread of our process and harden it.
    if (setThreadInfo) {
        DWORD pid = GetCurrentProcessId();
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if (snap != INVALID_HANDLE_VALUE) {
            THREADENTRY32 te = { sizeof(te) };
            if (Thread32First(snap, &te)) {
                do {
                    if (te.th32OwnerProcessID != pid) continue;
                    HANDLE th = OpenThread(THREAD_SET_LIMITED_INFORMATION,
                                           FALSE, te.th32ThreadID);
                    if (!th) {
                        // Newer SDK constant unavailable on older toolchains
                        th = OpenThread(THREAD_SET_INFORMATION,
                                        FALSE, te.th32ThreadID);
                    }
                    if (th) {
                        hardenThread(th, setThreadInfo);
                        CloseHandle(th);
                    }
                } while (Thread32Next(snap, &te));
            }
            CloseHandle(snap);
        }
        angle::log("thread_qos: hardened %d threads against EcoQoS", s_threadsHardened);
    }
}

} // namespace boost_thread_qos
