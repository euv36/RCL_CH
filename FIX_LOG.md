# FIX_LOG — native camera rewrite

Structured log of audit fixes and completed tasks. Newest entries are appended at the end.

Legend: `[VERIFIED]` = reproduced locally with a command; `[UNVERIFIED ON HARDWARE]` = logic tested with fakes/static review only.

---

## Task 3 — Atomic config and JSON foundation

**Bug / root cause**
1. `atomic_write_text` rejected a valid relative path because an empty `parent_path()` was treated as a missing directory.
2. POSIX rename did not fsync the parent directory, so the rename could be lost after a power failure.
3. Rename failures reported a stale `errno` string instead of the actual filesystem error.
4. `read_json_file` had no size limit and could exhaust memory on a huge or hostile file.
5. Tests did not cover replacement of an existing destination, UTF-8/nested JSON round-trips, or non-object roots.

**Modified files**
- `src/common/atomic_file.cpp`
- `src/common/json_config.cpp`
- `tests/common/atomic_file_test.cpp`
- `tests/common/json_config_test.cpp`

**Fix / optimization**
- Empty parent now resolves to `"."`; the temp file is still created beside the destination.
- Added `fsync` of the parent directory after a successful POSIX rename.
- Windows replacement keeps `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` and now reports `GetLastError()`.
- POSIX rename errors now use `std::error_code::message()`.
- JSON reads are capped at 8 MiB with a truthful warning.
- Added regression checks for overwrite, relative path, nested objects, Russian UTF-8, unknown-key preservation, and array root rejection.

**Verification**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure -R 'atomic_file|json_config'
```
Result: `atomic_file` Passed, `json_config` Passed (2/2).

---

## Task 4 — Vendored Dear ImGui and laptop dependency gate

**Bug / root cause**
- The laptop UI required an external `imgui` package that MSYS2 does not provide, so the target could never be built on this machine.
- The dependency gate checked only for `imgui.h` and did not build a reusable ImGui library or verify its version.

**Modified files**
- `third_party/imgui/*` (vendored Dear ImGui v1.91.8 + SDL2/OpenGL3 backends)
- `third_party/THIRD_PARTY.md`
- `cmake/LaptopDependencies.cmake`
- `CMakeLists.txt`
- `tests/laptop/dependency_gate_test.cpp`

**Fix / optimization**
- Vendored Dear ImGui v1.91.8 with its MIT license, upstream URL and archive SHA-256 recorded.
- Added the `camera_imgui` static target, compiled once and linked to SDL2/OpenGL.
- Gate now accepts either imported CMake targets or library variables.
- The dependency-gate test includes `imgui.h` and asserts `IMGUI_VERSION_NUM >= 19180` when laptop dependencies are present.
- No ImGui binary is committed; only source.

**Verification**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Result: `laptop_dependency_gate` Passed; full suite 17/17 Passed. `[VERIFIED]`

---

## Task 6 — Ориентированная проверка формы кластера ворот

**Баг / корневая причина**
- Защита от ложных ворот проверяла только стороны axis-aligned bbox.
- Тонкая диагональная стойка лампы или полоса изоленты даёт почти квадратный bbox, поэтому отношение сторон проходило порог `max_aspect`, хотя реальная толщина объекта была несколько пикселей.

**Изменённые файлы**
- `src/vision/gate_cluster.cpp`
- `tests/vision/gate_cluster_test.cpp`

**Исправление / оптимизация**
- Добавлено построение выпуклой оболочки по углам bbox компонентов и поиск минимального по площади описанного прямоугольника вращающимися калиперами.
- Фильтр теперь использует ориентированные `min_side`/`max_side`; диагональная тонкая полоса отбрасывается, а реальные ворота под углом остаются.
- Реализация остаётся без зависимости от OpenCV, поэтому модуль `camera_vision` по-прежнему собирается в no-OpenCV профиле.
- Добавлены регрессии: диагональная полоса из 20 квадратов 14×14 должна быть отброшена, толстые диагональные ворота 40×40 должны пройти.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2 --target kCameraCppGateClusterTest
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R gate_cluster --output-on-failure
```
Результат: `gate_cluster` Passed (1/1). `[VERIFIED]`

---

## Task 1 — CMake scaffold and dependency validation

**Bug / root cause**
- `cmake/Dependencies.cmake` had hardcoded absolute MSYS2/vcpkg fallback paths. A foreign MSYS2 toolchain could satisfy detection while compiling with WinLibs GCC, causing `corecrt_wstdlib.h`/`wchar.h` redefinitions.
- `${ZMQ_LIBRARIES}` was supplemented with a literal `zmq`, which breaks vcpkg/MSVC where the import library is `libzmq.lib`.
- OpenCV and libzmq were optional even for runtime targets, with no strict mode.
- No `Threads::Threads`, `install()`, `GNUInstallDirs`, `BUILD_TESTING`, or CTest skip code for the laptop dependency gate.
- `kCameraCppSmoke` binary was built but not exercised by CTest; `${CMAKE_SOURCE_DIR}` was used instead of `${PROJECT_SOURCE_DIR}`.
- Warning options had no opt-in `-Werror`/`/WX` and MSVC did not force UTF-8.

**Modified files**
- `cmake/Dependencies.cmake`
- `cmake/Warnings.cmake`
- `CMakePresets.json`
- `CMakeLists.txt`
- `src/CMakeLists.txt`
- `tests/laptop/dependency_gate_test.cpp`
- `.gitignore`
- `FIX_LOG.md`

**Fix / optimization**
- Removed all hardcoded dependency paths; discovery now uses `find_package()`, pkg-config hints and `CMAKE_PREFIX_PATH`, with every accepted include/lib validated against the active compiler prefix.
- Added `CAMERA_CPP_REQUIRE_RUNTIME_DEPS` (root option, default OFF); strict mode turns missing/rejected dependencies into `FATAL_ERROR`.
- Added `Threads::Threads`, `GNUInstallDirs`, `install(TARGETS camera_service ...)`, `BUILD_TESTING`, and CTest `SKIP_RETURN_CODE 77`.
- Removed the literal `zmq` link; tests now include the smoke binary and the dependency gate returns 77 when laptop deps are absent.
- Added opt-in `CAMERA_CPP_WERROR` and MSVC `/utf-8`; added toolchain hook, `msys2-release`, and test presets.
- Switched remaining source-tree references to `${PROJECT_SOURCE_DIR}`; ignored `graphify-out/`.

**Verification**
```powershell
$env:Path='C:\msys64\mingw64\bin;C:\msys64\usr\bin;'+$env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Result: configure OK, build OK, `100% tests passed out of 18`. `[VERIFIED]`

---

## Task 2 — Пути документации и шаблоны игнорирования секретов

**Баг / корневая причина**
- В `docs/spec.md` и `docs/implementation_plan.md` legacy-путь `C:\Users\sweetener\Desktop\camera` был перепутан с путём нового проекта `camera_cpp`; встречался опечаточный путь `camera_cpp_cpp`.
- Часть строк содержала повреждённые последовательности (`вЂ`, `В°`, `в†’`, `В±`) из-за неправильной кодировки.
- `.gitignore` не закрывал часто используемые имена приватных ключей и локальных секретов.

**Изменённые файлы**
- `.gitignore`
- `docs/spec.md`
- `docs/implementation_plan.md`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Все пути продукта приведены к `C:\Users\sweetener\Desktop\camera_cpp`; legacy-источник обозначен как `C:\Users\sweetener\Desktop\camera`.
- Удалены все вхождения `camera_cpp_cpp` и повреждённые последовательности; повреждённый фрагмент `никогда не менять исходный проект` теперь ссылается на legacy-репозиторий `camera`.
- Добавлены ignore-шаблоны `id_rsa*`, `id_ecdsa*`, `*.p12`, `*.pfx`, `.env*`, `.netrc`, `known_hosts`, `*.log`.

**Проверка**
```powershell
git diff --check
```
Результат: ошибок нет. Поиск по `docs/spec.md` и `docs/implementation_plan.md` не нашёл `camera_cpp_cpp`, `вЂ`, `В°`, `в†’`, `В±`. `[VERIFIED]`

---

## Task 2 — Store выбранных ворот: строгая проверка legacy-контракта

**Баг / корневая причина**
- `GateStore::load()` принимал любую версию JSON, материализовал все три слота и превращал повреждённый `rgb` в белый цвет.
- `gate()` возвращал запись для отсутствующих ворот; `role: unset` записывался в файл вместо удаления ключа.
- `upsert()` не сохранял `ts` и `lab`, поэтому native-контракт расходился с legacy `gate_store.py`.

**Изменённые файлы**
- `src/common/gate_store.cpp`
- `src/common/gate_store.hpp`
- `tests/common/gate_store_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Загрузка требует целочисленную `version == 1`, отбрасывает повреждённые `rgb`/`lab`, сохраняет неизвестные ключи и не создаёт пустые слоты.
- `rgb` должен содержать ровно три числа 0..255; `lab` — шесть чисел в диапазонах legacy-контракта, иначе `null`.
- `gate()` возвращает `nullopt` для отсутствующих ворот; роль `unset` удаляет ключ.
- `upsert()` записывает локальную ISO-метку времени, nullable `lab` и сохраняет неизвестные поля.
- `save()` записывает только реально существующие слоты 1..3.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R gate_store --output-on-failure
```
Результат: `gate_store` Passed (1/1, 0.20 sec). Полный набор после интеграции: 18/18 Passed (2.44 sec). `[VERIFIED]`

---

## Task 2 — Field map: соответствие контракту и расчёт направлений

**Баг / корневая причина**
- `FieldMap` писал неконтрактные поля `field_width_cm`, `gate_width_cm`, `gate_height_cm` с масштабом в 100 раз больше legacy.
- `load()` всегда возвращал успех и принимал неверную версию; `bind_from_results()` писал `heading_delta_deg`, `enabled`, `found` и `estimated: true`, не нормализуя углы и дистанции.
- `estimated_directions()` добавлял смещениеHeading вместо вычитания и возвращал -180..180 вместо 0..360.

**Изменённые файлы**
- `src/common/field_map.cpp`
- `src/common/field_map.hpp`
- `tests/common/field_map_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Использован контракт `version`, `bound_at`, `robot_pos`, `heading_offset_deg`, `gates`, `boundary`; неизвестные ключи и `boundary` сохраняются.
- `load()` отклоняет отсутствующий, повреждённый и не-v1 JSON, нормализует записи ворот.
- `bind_from_results()` берёт слот +1, только `enabled && found`, нормализует угол в 0..360, дистанцию — в `null` при неположительном/нечисловом значении и до 500 см; `estimated` берётся из `held`.
- `estimated_directions()` вычитает смещениеHeading, отклоняет нечисловые значения и возвращает 0..360 вместе с `dist_cm`.
- Обновлён тест: для смещения +10 ожидается направление 5, а не 25.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R field_map --output-on-failure
```
Результат: `field_map` Passed (1/1, 0.41 sec). `[VERIFIED]`

---

## Task 2 — Настройки камеры: канонические диапазоны и безопасные значения

**Баг / корневая причина**
- `CamSettingsConfig` дублировал диапазоны, принимал произвольные имена контролов, не сбрасывал `root_` при повторной загрузке и создавал null-записи через `operator[]`.
- Нечисловые значения не заменялись безопасными legacy-значениями; `save()` не писал `version`/`ts` и сохранял числа с плавающей точкой.
- Повторная загрузка повреждённого файла могла оставить устаревшие значения в памяти.

**Изменённые файлы**
- `src/common/cam_settings_config.cpp`
- `tests/common/cam_settings_config_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Введена каноническая таблица из восьми UVC-контролов legacy-контракта; `set()` отклоняет неизвестные имена.
- `load()` очищает состояние, подставляет безопасные значения 300/0/0/100/60/32/1/0, ограничивает и округляет числа до целых.
- `get()` использует `find()` и не создаёт записи; неизвестные ключи верхнего уровня и контролов сохраняются.
- `save()` пишет `version: 1`, локальную ISO-метку времени и целочисленные значения.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R cam_settings_config --output-on-failure
```
Результат: сначала тест упал на `!settings.set("unknown_control", 7)`; после исправления `cam_settings_config` Passed (1/1, 0.52 sec). `[VERIFIED]`
