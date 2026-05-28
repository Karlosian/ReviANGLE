# ReviANGLE v2.0.0 — Changelog

## Что изменилось с v1.0.2

### Новые модули

**d3d11_unlock** — снимает мьютекс с ANGLE. Cocos2d-x делает всё GL с одного потока, а ANGLE каждый D3D11-вызов тратит время на EnterCriticalSection. Убираем — +6-12% CPU.

**process_perf** — выключает Power Throttling Windows. Шедулер иногда помечает игру как "background" и загоняет на E-cores с пониженной частотой → рандомные стуттеры. Фикс: принудительно ставим ExecutionSpeed процессу и рендер-потоку.

**pcore_pin** — привязывает рендер-поток к P-cores (Intel 12+ / AMD Ryzen hybrid). Без этого поток может мигрировать на E-core → просадка 50-70% на пару кадров. Пин держит его на быстрых ядрах.

**gpu_residency** — резервирует 70% свободной VRAM. Когда под давлением памяти Windows эвиктит текстуры из VRAM → через кадр ANGLE затаскивает их обратно через DMA → микрохич 3-10ms. Резервация говорит Windows: "это моё, не трожь".

**stutter_monitor** — диагностика микрофризов. Логирует что именно вызывает тормоза: CPU_SPIKE, GPU_BOUND или THREAD_SCHEDULING. Только для диагностики, по умолчанию выключен.

### Оптимизации

- **anti_stutter** — агрессивный режим: процессу HIGH_PRIORITY_CLASS, рендер-потоку TIME_CRITICAL. Убирает scheduler jitter.
- **no_fs_optimizations** — отключает Fullscreen Optimizations Windows (добавляют 15-20ms input lag).
- **waitable_swap / present_hints** — переписаны с vtable validation, больше не крашатся на старых драйверах.
- **low_latency** — защищён от крашей той же vtable validation.
- **frame_pacing** — jitter margin ужат с 1.5ms до 0.1ms, кадры приходят строго по расписанию.
- **boost_fmod** — оптимизация аудио-движка.
- **boost_silent** — подавление debug-вывода cocos2d-x.
- **effect_lod** — LOD для эффектов, drawcall_budget, static_batch, iocp_loader, thread_qos.

### Удалено

- **unlock_fps** — вызывал input lag и конфликты с frame_pacing.
- **atlas_merge, static_batch, batch_force, frustum_cull, trigger_cache** — несовместимы с новыми оптимизациями.
- **scene_bvh, shader_simplify, halfres_fx, d3d11_mt, halfres_render** — устаревшие модули.


### Исправлено

- Микрофризы на гибридных CPU (Intel 12+ / AMD Ryzen 7000+).
- EXCEPTION_ACCESS_VIOLATION в d3d11.dll при активных модах.
- Input lag от Fullscreen Optimizations Windows.
- Unicode кракозябры в логах (заменены на ASCII).

### Заметки

- v1.0.2 был чисто пакинг-релизом (исправление 32/64-bit DLL). Весь функционал — новый.

---

**Автор:** Reviusion
**GitHub:** https://github.com/Reviusion/ReviANGLE
