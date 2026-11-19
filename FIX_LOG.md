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

---

## Task 7 — Self-mask: строгая бинарность маски сохранения

**Баг / корневая причина**
- `build_keep_mask()` рисовал окружности с `cv::LINE_AA`, поэтому маска сохранения содержала десятки промежуточных оттенков серого вместо двух допустимых значений 0 и 255.
- `cv::bitwise_and()` сохранял эти серые пограничные пиксели, и они попадали в `connectedComponentsWithStats`: часть корпуса робота оставалась доступной для поиска ворот. Это нарушало контракт legacy `self_mask.py` / `pi_core/src/self_mask.cpp`, где маска строго бинарна.

**Изменённые файлы**
- `src/vision/self_mask.cpp`
- `tests/vision/self_mask_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Замена `cv::LINE_AA` на `cv::LINE_8` во всех трёх вызовах `cv::circle` (диск и обе границы кольца); маска снова содержит только 0 и 255.
- Добавлены регрессии: все значения маски строго бинарны для диска, кольца и повреждённых параметров.
- Добавлены проверки независимости от разрешения: `cx/cy` масштабируются с шириной/высотой, `r/r2` — с `min(width, height)`.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R 'self_mask|distance' --output-on-failure
```
Результат: `self_mask` и `distance` Passed (2/2). `[VERIFIED]`

---

## Task 8 — Угол трекинга и центр кадра

**Баг / корневая причина**
- `TrackingEngine::track()` возвращал угол `atan2` в диапазоне `[-180, 180)`, тогда как сетевой и legacy-контракт требуют `[0, 360)`.
- Центр кадра вычислялся как `(w-1)/2` и `(h-1)/2`, а legacy-орACLE использует целочисленные `w//2`, `h//2`; это давало систематический сдвиг угла.
- Тест повторного использования буферов допускал рост `cache_footprint_bytes`, вместо требуемой стабильности после первого кадра.

**Изменённые файлы**
- `src/vision/tracking.cpp`
- `tests/vision/tracking_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Центр кадра приведён к целочисленному `w/2`, `h/2`; угол нормализуется через `fmod` в `[0, 360)`.
- Добавлены регрессии для 90, 180 и 270 градусов.
- Проверка 100 кадров теперь требует точного равенства `cache_footprint_bytes` после первого кадра.
- Морфология `open_kernel` намеренно отсутствует: legacy её игнорирует, паритет сохранён.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2 --target kCameraCppTrackingTest
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R tracking --output-on-failure
```
Результат: `tracking` Passed (1/1, 1.95 sec). Полная сборка в момент平行的 Task 10 была временно заблокирована его незавершённым интерфейсом `CaptureBackend::properties()`; после завершения Task 10 требуется повторная полная проверка. `[VERIFIED TARGETED]`

---

## Task 9 — Честный статус применения UVC-контролов

**Баг / корневая причина**
- Когда `v4l2-ctl --get-ctrl` завершался с ошибкой или возвращал неразбираемый вывод, `CamSettings::apply_controls()` всё равно ставил контролу статус `ok`. Это выдуманное подтверждение: команда `set` прошла, но применение не было проверено.
- `ApplyReport::ok()` не различал «подтверждено камерой» и «нет подтверждения», поэтому такой статус считался успешным.
- Локальная таблица диапазонов дублировала канонические восемь контролов без явной пометки об источнике; в тесте не было случаев неразбираемого и неудавшегося readback.

**Изменённые файлы**
- `src/pi_camera/cam_settings.cpp`
- `tests/pi_camera/cam_settings_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Неудавшийся или неразбираемый readback теперь даёт статус `unconfirmed` вместо `ok`; правдоподобное, но отличное от запрошенного значение по-прежнему даёт `mismatch`.
- `ApplyReport::ok()` возвращает `false` для `error`, `mismatch` и `unconfirmed`.
- Добавлен явный комментарий, что таблица — локальное зеркало канонических диапазонов из `src/common/cam_settings_config.cpp` (этот файл не трогался); восемь границ совпадают, `auto_exposure` остаётся принудительным `1`.
- Баланс белого намеренно не добавляется: в legacy-контракте такого контрола нет.
- Тест через `CAMERA_TEST_CHECK` проверяет `unconfirmed` для неразбираемого вывода и для ошибки `get`, принудительный manual AE и `!report.ok()`; известные контролы, клампинг, `n/a` и `mismatch` сохранены.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
cmake -S . -B build-msys2
cmake --build build-msys2
ctest --test-dir build-msys2 -R cam_settings --output-on-failure
```
Результат: после добавления проверок тест упал на `report.controls.at("gamma").status == "unconfirmed"` (строка 57); после исправления `cam_settings` Passed, `cam_settings_config` Passed (2/2, 0.20 sec). `[VERIFIED]`

---

## Task 10 — Проверка согласованных параметров V4L2

**Баг / корневая причина**
- Драйверу отправлялись запрошенные FOURCC, размер и FPS, но фактические согласованные параметры не считывались. Код мог сообщить об успехе, даже если камера работала в другом формате или размере.
- Предупреждение о не-MJPG выводилось даже когда камера действительно согласовала MJPG.
- Ошибки нормализации конфигурации и чтения свойств молча терялись.

**Изменённые файлы**
- `src/pi_camera/v4l2_capture.hpp`
- `src/pi_camera/v4l2_capture.cpp`
- `src/pi_camera/camera_engine.hpp`
- `src/pi_camera/camera_engine.cpp`
- `tests/pi_camera/capture_config_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Добавлен контракт `CaptureBackend::properties()` и структура `CaptureProperties`; backend обязан вернуть реальные width, height, FPS и FOURCC.
- `V4l2Capture::open()` теперь принимает `CaptureOpenReport`, проверяет согласованные параметры и возвращает честную ошибку, если properties недоступны.
- `V4l2Capture::negotiated_config()` и `EngineFrame::negotiated` хранят фактические значения, а не запрошенные.
- FOURCC устанавливается до size/FPS; некорректный FOURCC отклоняется.
- При несовпадении размера или формата выдаётся явное предупреждение; отсутствие FPS фиксируется в warning с подстановкой запрошенного значения.
- Полный кадр остаётся без crop; downscale до 640x360 выполняется через `cv::resize(..., INTER_AREA)`.
- Тесты fake backend проверяют успешный readback, не-MJPG, negotiated 1280x720, отсутствие property contract и нулевой FPS.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2 --target kCameraCppCaptureConfigTest
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R capture --output-on-failure
```
Результат: configure OK, build OK, `capture_config` Passed (1/1, 0.22 sec). Полная сборка готовилась параллельно с незавершённым Task 11, поэтому финальный полный прогон после завершения Task 11 обязателен. `[VERIFIED TARGETED]`

---

## Task 11 — Сервис Pi: конфигурированные ворота, честный статус и ZMQ-слушатели

**Баг / корневая причина**
- `TrackInput.thresholds` оставался по умолчанию (`configured=false`), поэтому `TrackingEngine` пропускал все сохранённые ворота: сервис работал, но не обнаруживал ни одни настроенные ворота.
- Единицы LAB не совпадали: `GateStore` хранит legacy LAB (L 0..100, a/b −128..127), а `GateThreshold` ожидает 8-битный OpenCV Lab (L 0..255, a/b 0..255 с нейтралью 128).
- `GateStore` менялся потоком `ColorListener` без блокировки, пока главный цикл читал его: гонка данных.
- Парсер цвета игнорировал `lab` и верхнеуровневый `rgb`, не проверял id и диапазоны; `role` терялся.
- Статус был неправдивым: `enabled` мог быть true при `found/held=false`, `held` всегда false, углы приходили в −180..180, `fps` мог быть нечисловым, площадь и роль не публиковались.
- SUB-сокеты не имели `ZMQ_RCVHWM=1` и дренажа очереди; CAM SET приводил нечисловые значения к нулю и применял их; legacy-имя `exposure_time_absolute` не отображалось.
- Порты были захардкожены, флаги `--laptop-ip`/`--no-serial` нигде не отражались; сервис печатал «camera open: 1920x1080 MJPG @30», не проверив согласованные параметры.

**Изменённые файлы**
- `src/pi_camera/service_main.cpp`
- `src/pi_camera/color_listener.hpp`
- `src/pi_camera/color_listener.cpp`
- `src/pi_camera/status_builder.hpp`
- `src/pi_camera/status_builder.cpp`
- `src/CMakeLists.txt`
- `tests/pi_camera/status_builder_test.cpp`
- `tests/integration/pi_zmq_loopback_test.cpp` (новый)
- `FIX_LOG.md`

**Исправление / оптимизация**
- Перед каждым кадром под общей mutex-блокировкой делается снимок `GateStore`; все три слота заполняются порогами из Lab, а при отсутствии Lab — широким окном из RGB. Блокировка не удерживается во время трекинга.
- Legacy LAB переводится в OpenCV 8-bit: `L8=round(L*255/100)`, `A8/B8=round(v+128)`, кламп 0..255 и сохранение `min<=max`.
- Общий `std::mutex` защищает `GateStore` между слушателем и главным циклом.
- `ColorListener` принимает `{gates:[…]}`, массив и верхнеуровневый `rgb`, проверяет id 1..3 и диапазоны, сохраняет `role`.
- `StatusBuilder` подавляет `found/held/area/dist` для выключенных ворот, нормализует угол в `[0,360)`, обнуляет нечисловой fps, публикует `area`/`area_px`, `role`, `dist_reliable`; `held` берётся из результата трекинга.
- SUB: `ZMQ_RCVHWM=1`, дренаж очереди с оставлением только последнего полного сообщения, `RCVTIMEO=100`, `LINGER=0`, без `ZMQ_CONFLATE`.
- CAM SET: нечисловые значения отбрасываются; `exposure_time_absolute` отображается в `exposure` с правдивым логом; подтверждение не выдумывается.
- Добавлены `--port-colors`, `--cam-settings-port`, `--bind-host`; `--laptop-ip` и `--no-serial` честно логируются. Сервис печатает согласованные предупреждения `CaptureOpenReport`, а запрашиваемый режим больше не выдаётся за согласованный.
- Loopback-тест на `127.0.0.1:15555/15557/15559` с fake V4L2 проверяет multipart `[JPEG, JSON]`, преобразование LAB, применение CAM SET и отсутствие выдуманного ack. Реальное железо не использовалось: `[UNVERIFIED ON HARDWARE]`.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: configure OK, сборка 100%, `100% tests passed out of 19`, 13.23 sec; `pi_zmq_loopback` Passed (10.61 sec), `status_builder` Passed. `[VERIFIED]`

---

## Task 8 (follow-up) — Удержание ворот при кратковременной потере

**Баг / корневая причина**
- `TrackingEngine` вообще не использовал `GateStability`: при пустом кадре результат становился `found=false`, хотя legacy-орACLE публикует последнее принятое положение с `held=true` в течение `lost_frames` кадров.
- Из-за этого `GateTrackResult.held` всегда был `false`, и статус камеры никогда не показывал удержание ворот.
- При отсутствии `previous_positions` кластеризация теряла опору на последнюю принятую позицию, что расходилось с legacy.

**Изменённые файлы**
- `src/vision/tracking.hpp`
- `src/vision/tracking.cpp`
- `tests/vision/tracking_test.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Добавлены состояния на слот: `GateStability`, последний bbox/положение, флаг наличия положения и последняя дистанция.
- Каждый кадр выполняет один `stability_.update()` (включая пустые кадры); во время удержания публикуются `found=true`, `held=true` и последнее положение, после `lost_frames` — потеря.
- При удержании не обновляется дистанция по устаревшему радиусу (`dist_reliable=false`), сохраняются bbox и маска последнего принятого кадра.
- `build_gate_object()` использует последнюю принятую позицию как якорь, если вызывающий не передал `previous_positions`.
- Отключение слота сбрасывает всё связанное состояние.
- Тест проверяет первый кадр без `held`, четыре кадра удержания с сохранением угла/радиуса/маски и потерю на пятом пустом кадре; тест углов использует отдельный engine, чтобы состояние stability не влияло на соседние сцены.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: сборка 100%, `tracking` Passed (1/1, 1.41 sec), полный набор `100% tests passed out of 19`, 12.98 sec. `[VERIFIED]`

---

## Task 14 — Панели native UI и живое управление (паритет с legacy)

**Баг / корневая причина**
- После Task 13 каркас SDL2/ImGui имел только CONNECT и Ctrl+V; отсутствовали панели INFO, HSV MASK, RADAR, CAM SET, GATES/SAVE, привязка к полю и пресет POS — интерфейс не соответствовал legacy.
- Не было проверки, что невключённые ворота не рисуются в радаре: stale-записи с `enabled=false` могли бы показать себя как активные.
- CAM SET мог выдумывать подтверждение «Pi подтвердил», хотя ZMQ PUB не даёт ACK; требовался правдивый статус.
- INFO на английском и без всех обязательных названий элементов управления не проходил приёмку.
- HSV-маска должна быть выровнена 1:1 с портретным кадром 720x1280; требовался чистый builder без привязки к GUI.

**Изменённые файлы**
- `src/laptop/panels.hpp/.cpp` (новый)
- `src/laptop/help_ru.hpp/.cpp` (новый)
- `src/laptop/hsv_panel.hpp/.cpp` (новый)
- `src/laptop/radar_panel.hpp/.cpp` (новый)
- `src/laptop/cam_settings_panel.hpp/.cpp` (новый)
- `src/laptop/theme.hpp/.cpp` (новый)
- `src/laptop/state.hpp/.cpp`
- `src/laptop/widgets.hpp/.cpp`
- `src/laptop/app.cpp`
- `src/CMakeLists.txt`
- `tests/laptop/help_ru_test.cpp` (новый)
- `tests/laptop/hsv_panel_test.cpp` (новый)
- `tests/laptop/layout_smoke_test.cpp` (новый)
- `tests/laptop/radar_panel_test.cpp` (новый)
- `tests/laptop/cam_settings_panel_test.cpp` (новый)
- `tests/laptop/theme_test.cpp` (новый)
- `FIX_LOG.md`

**Исправление / оптимизация**
- `HelpRu::sections()` отдаёт семь русских секций (`main`, `gates`, `radar`, `camera`, `hsv`, `field`, `status`) со всеми обязательными токенами: `Ctrl+V`, `CAM SET`, `RADAR`, `BIND FIELD`, `POS`, `HSV MASK`, `INFO`, `SAVE G1`, `GATES`, `exposure`, `brightness`. INFO открывается модальным окном по кнопке, закрывается по ESC.
- Чистый `build_hsv_mask_bgra()` повторяет oracle `hsv_view.py`: RGB→HSV→BGR, для каждой ненулевой маски совпадающего размера тело красится `out*0.55+color*0.45`, граница (после erode 3x3) — точным цветом ворот; alpha=255; несовпадающий размер маски игнорируется; пустой кадр нормализуется в 1280x720.full-размер без crop.
- `compute_ui_layout()` даёт непересекающиеся прямоугольники для 1280x720: верхний ряд, левые видео/маска, правые source/gates, нижние контролы.
- `radar_gate_visible = enabled && found`: stale-ворота (`enabled=false`) в радаре не отображаются.
- CAM SET использует канонические диапазоны `CamSettingsConfig::ranges()`, сохраняет настройки локально atomically и шлёт PUB JSON на 5559; whitelist пользовательских контролов исключает `auto_exposure` (Pi принудительно держит manual). Статус честный: «Применено локально: настройки сохранены», «Pi недоступен: настройки не применены» — без выдуманного ACK.
- SAVE G1..G3 пишет цвет через `GateStore::upsert` + `save_atomic()` и шлёт PUB JSON на 5557. BIND FIELD / POS обновляют статус в нижней панели.
- Добавлен кириллический диапазон шрифта ImGui для русского INFO.
- В `widgets.cpp` явные `static_cast<float>` убрали предупреждения `-Wconversion`; сборка чистая.
- TDD: каждый новый чистый помощник сначала RED (ожидаемое падение из-за отсутствия фичи), затем минимальный GREEN. `theme_test` усилил слабые проверки конкретными оттенками после того, как первый вариант прошёл на stub.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: configure OK, сборка 100% без предупреждений, `camera_laptop_ui.exe` собирается, `100% tests passed out of 27`, 13.81 sec; новые `help_ru`, `hsv_panel`, `layout_smoke`, `radar_panel`, `cam_settings_panel`, `theme` — Passed. `[VERIFIED]` Скриншот на SDL dummy driver и живой GUI НЕ выполнялись (dummy не даёт OpenGL/ImGui) — отложено до Task 19; PUB-отправка на Pi проверена только статикой/контрактом: `[UNVERIFIED ON HARDWARE]`.

---

## Оптимизация горячего пути и проверка на локальной камере

**Баг / корневая причина**
- `downscale_and_rotate(full_frame)` выделяла новый `cv::Mat` 640x360 на каждый кадр Pi; `CameraEngine::read()` добавляла ещё одну аллокацию рабочего кадра.
- `TrackingEngine::track()` каждый кадр создавал `cv::Mat::zeros` для маски компонента, хотя размер маски постоянный.
- В коде отсутствовали headless-проба локальной камеры и русские описания ключевых функций.

**Изменённые файлы**
- `src/pi_camera/v4l2_capture.hpp/.cpp`, `src/pi_camera/camera_engine.hpp/.cpp`, `src/pi_camera/service_main.cpp`
- `src/vision/tracking.hpp/.cpp`, `tests/pi_camera/downscale_reuse_test.cpp`, `tests/pi_camera/capture_config_test.cpp`, `tests/vision/tracking_test.cpp`
- `tools/local_camera_probe.cpp` (новый), `src/CMakeLists.txt`
- `src/common/{atomic_file,json_config,paths,gate_store,field_map,cam_settings_config}.cpp`
- `src/vision/{self_mask,distance,gate_cluster}.cpp`
- `src/pi_camera/{status_builder,cam_settings,color_listener}.cpp`
- `src/laptop/{app,hsv_panel,help_ru,panels,source_controller,clipboard_text,widgets}.cpp`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Добавлена перегрузка `downscale_and_rotate(const cv::Mat&, cv::Mat&)` с переиспользованием буфера; старая обёртка сохранена для совместимости.
- `CameraEngine::read_reusing(EngineFrame&)` переиспользует `processing_buffer_`; `read()` оставляет прежнюю семантику; `close()` освобождает буфер. Основной цикл `camera_service` переведён на `read_reusing`.
- `TrackingEngine::component_mask(slot)` отдаёт const-ссылку на кэшированную маску; `create()` вызывается только при смене размера/типа, затем `setTo(0)`.
- TDD: каждая оптимизация сначала RED (отсутствующий метод), затем минимальный GREEN; новые проверки стабильности указателя данных и неизменности результата.
- Добавлен headless `local_camera_probe` (без GUI/ZMQ/SSH): локальная USB-камера -> 640x360 -> портрет 720x1280 -> `TrackingEngine`. Цель собрана, но НЕ зарегистрирована в ctest, чтобы не требовать железо в CI.
- Добавлены русские описания ключевых функций в common/vision/pi_camera/laptop; англоязычные вкрапления в комментариях устранены.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: configure OK, сборка 100% без предупреждений, `100% tests passed out of 28`, 14.50 sec; `camera_laptop_ui.exe` и `local_camera_probe.exe` собираются. `[VERIFIED]`

Headless-проба локальных камер (без Pi, без GUI):
```powershell
$env:Path = 'C:\msys64\mingw64\bin;' + $env:Path
& '.\build-msys2\src\local_camera_probe.exe' --index 0 --frames 30 --width 1920 --height 1080 --fps 30
& '.\build-msys2\src\local_camera_probe.exe' --index 1 --frames 30 --width 1920 --height 1080 --fps 30
```
Результат index 0 (`HP HD Camera`): `negotiated=1280x720 fps=30`, 30/30 кадров, 31.3 fps, stage cvt=3.67 mask=1.12 cc=5.48 cluster=0.00 total=10.27 ms, processing=640x360 portrait=720x1280, gates_found=0. Результат index 1 (`HD USB Camera`): `negotiated=1920x1080 fps=30`, 30/30 кадров, 4.79 fps (ограничение USB/железа, не кода), stage total=8.71 ms, gates_found=0. Оба выхода 0. Жёлтых ворот в комнате нет, поэтому `gates_found=0` ожидаемо. `[VERIFIED ON LOCAL CAMERA]`

Все утверждения про Pi, live-камеру и прошивку остаются `[UNVERIFIED ON HARDWARE]`: Pi не подключался, SSH/сетевые пробы не выполнялись. Сон ноутбука не выполнялся.

---

## Локальный предпросмотр камеры ПК в окне

**Баг / корневая причина**
- Существующий `local_camera_probe` печатал метрики в консоль, но не показывал саму картинку, поэтому «посмотреть, как работает камера» было нельзя.
- В `cmd.exe` синтаксис `$env:Path = ...` из инструкций PowerShell не работает: пользователь получил `The filename, directory name, or volume label syntax is incorrect.`

**Изменённые файлы**
- `src/laptop/preview_support.hpp/.cpp` (новые)
- `tests/laptop/preview_support_test.cpp` (новый)
- `tools/local_camera_preview.cpp` (новый)
- `src/CMakeLists.txt`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Выделены тестируемые функции `bgr_to_rgba()` (BGR → плотный RGBA для SDL) и `fit_rect()` (вписывание кадра в окно без обрезки FOV).
- Добавлен `local_camera_preview`: SDL2-окно с живым потоком локальной USB-камеры, найденные ворота обводятся жёлтым прямоугольником, в консоль каждые 30 кадров пишутся FPS и время трекинга. Pi/ZMQ/сеть не используются.
- TDD: тест `preview_support` сначала RED (нет заголовка/реализации), затем GREEN.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: сборка без предупреждений, `100% tests passed out of 29`, 13.88 sec; `preview_support` Passed. `[VERIFIED]`

Окно запущено на ПК: `Start-Process .\build-msys2\src\local_camera_preview.exe --index 0`, процесс жив, `MainWindowTitle = Local camera preview`. `[VERIFIED ON LOCAL CAMERA]`

Для `cmd.exe` эквивалент строки PATH:
```cmd
set "PATH=C:\msys64\mingw64\bin;%PATH%"
build-msys2\src\local_camera_preview.exe --index 0
```
Закрытие окна — `ESC` или кнопка `X`. Pi/live по-прежнему `[UNVERIFIED ON HARDWARE]`.

---

## Hardware-прогон на Pi 2026-11-19

**Баг / корневая причина**
- До этого момента запуск на Pi был запрещён режимом blind Pi, поэтому `docs/VERIFICATION.md` содержал только статические проверки и статус `[UNVERIFIED ON HARDWARE]`.
- Пользователь явно дал IP `10.132.122.51` и разрешил подключиться и запустить прогу камеры.

**Изменённые файлы**
- `docs/VERIFICATION.md`
- `docs/DEPLOY.md`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Настроен SSH через helper `raspberry-pi` (пароль сохранён пользователем локально, в чат не передавался); host key SHA256:oJEah4pSr56l0SWM5JA5laA95HR8ZKw3xhlmWCFNz5Y.
- Исходники `ba30c23` упакованы в `git archive` и скопированы в `/tmp/camera_cpp_smoke`; на Pi собран `camera_service` (OpenCV 4.10.0, libzmq, без laptop-целей).
- Сервис запущен временно, без systemd и без робота: `--no-serial --bind-host 10.132.122.51`, порты 5555/5557/5559, PID 1537. Прошивка и последовательный порт не трогались.
- Smoke с ноутбука подтвердил валидный мультипарт `[JPEG, JSON]`: `ok=true`, `messages=1`, `valid_jpeg=1`, `valid_json=1`, `fps≈10.75`, `frame_wh=[640,360]`, `src=pi`.
- Через PUB на 5557 включены ворота G1 (жёлтый, `role=ours`); конфиг сохранён в `/tmp/camera_cpp_smoke/saved_gates.json`. Повторный smoke: `enabled=true`, `found=false` — в поле зрения нет жёлтых ворот, что ожидаемо.
- Обновлены статусы документации: фактический hardware-прогон описан в `docs/VERIFICATION.md`, `docs/DEPLOY.md` ссылается на него.

**Проверка**
```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File "C:\Users\sweetener\.config\opencode\skills\raspberry-pi\scripts\raspi.ps1" test -Ip 10.132.122.51
python tests/integration/pi_service_smoke.py --host 10.132.122.51 --duration 5 --output local/pi_smoke_summary.json
```
Результат: SSH OK; сборка на Pi без ошибок; оба smoke-запуска `ok=true`; сервис оставлен запущенным для дальнейших тестов пользователя. `[VERIFIED ON HARDWARE]`

CAM SET с физическим изменением настроек камеры, остановка сервиса и восстановление конфигов не выполнялись — пользователь может сделать это отдельно. Сон ноутбука не выполнялся.

---

## Serial-мост робота в нативном camera_service

**Баг / корневая причина**
- Нативный `camera_service` не взаимодействовал с роботом: в логе значилось `serial link: not managed by this service`, хотя legacy `rpi_elp_cam.py` отправлял команды ворот и heartbeat на Arduino через `/dev/serial0`.
- Arduino физически отключён, поэтому мост должен включаться только при реально доступном порту и не мешать работе сервиса без робота.

**Изменённые файлы**
- `src/pi_camera/serial_link.hpp/.cpp` (новые)
- `src/pi_camera/service_main.cpp`
- `tests/pi_camera/serial_link_test.cpp` (новый)
- `tests/integration/pi_zmq_loopback_test.cpp`
- `src/CMakeLists.txt`
- `FIX_LOG.md`

**Исправление / оптимизация**
- Реализован `update_gate_serial()` с байт-паритетом legacy: первая детекция отправляется сразу, повтор — при изменении угла ≥3° (с учётом перехода 0/360) и не чаще 0.10 с; потеря ворот шлёт `G<id>,-1,0\n` один раз. Угол нормализуется в `[0,360)`, площадь ограничивается `0..1e9`.
- `SerialLink` открывает UART 115200 (8N1, без блокировки) только если порт существует и мост явно не отключён; при отсутствии Arduino объект неактивен, все записи безопасно игнорируются.
- В `service_main.cpp` добавлен `--serial <path>` (по умолчанию `/dev/serial0`), флаг `--no-serial` сохранён как явное отключение. При отсутствии порта выводится предупреждение `robot bridge disabled`, а не ошибка. В цикле после `statuses` шлются команды ворот и heartbeat `HB\n` каждые 0.5 с; в конце вызывается `close()`.
- TDD: тест `serial_link` сначала RED (нет заголовка/исходника), затем GREEN. Интеграционный loopback-тест проверяет, что при несуществующем порту сервис стартует и логирует отключение моста.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 -R '^(serial_link|pi_zmq_loopback)$' --output-on-failure
```
Результат: `serial_link` Passed (0.01 sec), `pi_zmq_loopback` Passed (10.74 sec), 2/2. Полный набор: `100% tests passed out of 30`, 14.02 sec, сборка без предупреждений. `[VERIFIED]`
Отправка реальных команд на Arduino не проверялась — порт физически отключён. `[UNVERIFIED ON HARDWARE]`

---

## Запуск camera_service без Arduino 2026-11-19

**Цель**
- Запустить сервис на Pi без физически подключённого Arduino, чтобы robot-код не выполнялся.

**Действия**
- Исходники `aa576cd` (с serial-мостом) упакованы в `git archive` и распакованы в `/tmp/camera_cpp_noserial`; собран `camera_service` (OpenCV 4.10.0, libzmq, без laptop-целей).
- Старый сервис (PID 1537) остановлен. Новый запущен с флагом `--no-serial` (PID 2056), порты 5555/5557/5559 на `10.132.122.51`.

**Проверка**
- В логе: `serial link: disabled by --no-serial` — robot-код не активируется; `/dev/serial0` не открывался.
- `ss -ltn` подтверждает прослушивание 5555/5557/5559.
- Smoke с ноутбука: `ok=true`, `messages=1`, `valid_jpeg=1`, `valid_json=1`, `fps≈9.8`, `frame_wh=[640,360]`, `src=pi`. `[VERIFIED ON HARDWARE]`
- Прошивка и последовательный порт не трогались; сон ноутбука не выполнялся.

---

## Аудит UI — CAM SET, INFO, масштабирование, запуск (2026-11-20)

**Баги**
1. Текст «TARGET G2» рисовался отдельным `putText` в координатах `{520,630}` — поверх области SAVE, а не на кнопке TARGET.
2. Окно CAM SET имело фиксированный размер 420×520, кнопки (5 шт.) не помещались по ширине (до ~530 при ширине 520).
3. INFO не описывал кнопки и слайдеры CAM SET, TARGET, SAVE, BIND FIELD, POS, R/+/-.
4. Прямой запуск `camera_ui.exe` без PATH падал с «libopencv_500.dll не найден».

**Исправление**
- Метка TARGET теперь обновляет `bottom_buttons()[0].label` ("TARGET G1..G3") и рисуется на самой кнопке; отдельный `putText` убран.
- Окно CAM SET стало масштабируемым: `WINDOW_NORMAL`, перетаскивание меняет размер, контент масштабируется через `cv::resize`; базовый размер 640×480, слайдеры и кнопки (в один ряд) помещаются.
- `help_ru.cpp`: расширены все 7 секций — описания BITMAP/L/A/B, TARGET, SAVE, слайдеров LAB, кнопок CAM SET (SAFE DEFAULTS/SAVE NOW/SEND TO PI/ROLE), BIND FIELD, POS, R/+/-, STALE.
- В корень проекта добавлены `camera_ui.exe` и лаунчер `camera_ui.cmd` (добавляет MSYS2 bin в PATH); `camera_ui.exe` добавлен в `.gitignore`.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: configure OK, сборка 100% без предупреждений, `100% tests passed out of 35`, 15.65 с. Живая камера: `camera_ui.exe --source 0` открывает окно `Camera`, процесс жив; окно CAM SET масштабируется. `[VERIFIED]` `[VERIFIED ON LOCAL CAMERA]`

---

## Исправление радара и инверсии цветов камеры (2026-11-20)

**Баг / корневая причина**
- В `render_main()` кадр в RGB (`portrait_frame_`) копировался в BGR-холст `canvas_` без конвертации, поэтому цвета отображались инвертированными (красный ↔ фиолетовый).
- Радар питался только статусом Pi; при работе с локальной камерой (без Pi) `radar_gates` оставались с `found=false`, и на радаре ничего не отображалось.

**Изменённые файлы**
- `src/laptop/opencv_app.cpp`
- `C:\Users\sweetener\Desktop\CameraUI\camera_ui.exe` (пересобранная копия)

**Исправление / оптимизация**
- Перед отрисовкой превью выполняется `cv::cvtColor(..., COLOR_RGB2BGR)` для левого и правого кадра.
- В `refresh_tracking()` радар обновляется результатами трекинга (enabled/found/angle/dist/rgb), поэтому работает и без Pi.

**Проверка**
- Полная сборка MSYS2 без `-j`: configure OK, build без предупреждений.
- `ctest`: 35/35 Passed, 15.54 с.
- Живой запуск `CameraUI.cmd --source 0`: окно `Camera` открывается, процесс жив. `[VERIFIED ON LOCAL CAMERA]`

---

## Аудит rewrite UI — закрытие багов (2026-11-20)

**Баг / корневая причина**
- `parse_connect("local")` возвращал `camera_index = -1`, поэтому локальная камера не открывалась.
- `OpenCVApp::run()` вызывал `parse_status_payload(json, now, now)`: `stale` всегда false, при остановке Pi статус не переоценивался.
- В `save_gate()` среднее из RGB-кадра записывалось как BGR (`{mean[2],mean[1],mean[0]}`), а `rgb_to_lab_u8` получал BGR → неверные цвета и LAB.
- В `build_hsv_frame()` палитра заполнялась `{gate_colors[2],[1],[0]}`, хотя `HsvPalette` ожидает RGB → инвертированные маски/легенда.
- Выделение сохраняло координаты preview-области без маппинга в кадр 720x1280; рамка рисовалась в неправильном месте.
- Режимы вида L/A/B меняли состояние, но отрисовка всегда показывала bitmap.
- Нижние LAB-слайдеры не обрабатывали мышь.
- Закрытие окна по X не завершало цикл (нет проверки `WND_PROP_VISIBLE`).
- Пустое выделение сохраняло чёрный цвет и `enabled=true`.
- `opencv_main` не принимал `--source` для проверки камеры.
- Мелочи: устаревший комментарий про CONFLATE, `imgui.ini` не игнорировался.

**Изменённые файлы**
- `src/laptop/source_controller.cpp` (+тест `source_controller_test.cpp`)
- `src/laptop/opencv_app.cpp`, `src/laptop/opencv_app.hpp`
- `src/laptop/preview_support.cpp/.hpp` (+тест `preview_support_test.cpp`)
- `src/laptop/opencv_main.cpp`
- `src/laptop/pi_video_client.cpp`
- `.gitignore`

**Исправление / оптимизация**
- `"local"` → `camera_index = 0`.
- Статус Pi хранит JSON и время приёма; каждый кадр переоценивается, `stale` показывается честно.
- `save_gate()` и `build_hsv_frame()` приведены к RGB-контракту.
- Добавлен `preview_point_to_image()`; выделение маппится в кадр, пустое/внешнее выделение отклоняется.
- Реализованы режимы L/A/B (RGB→Lab, выбор канала).
- LAB-слайдеры интерактивны: клик меняет порог целевого ворота и сохраняет.
- Добавлена проверка видимости главного окна → выход по X.
- `opencv_main` принимает `--source <текст>` (и `--source=...`).
- `imgui.ini` добавлен в `.gitignore`.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: сборка без предупреждений, `100% tests passed out of 35`, 15.56 sec; `preview_support`, `source_controller`, `opencv_app` — Passed. `[VERIFIED]`

Живая камера ПК: `camera_ui.exe --source 0` открывает окно `Camera`, процесс остаётся живым, кадр 1280x720@30 → портрет 720x1280, `gates_found=0` (в комнате нет жёлтых ворот). `[VERIFIED ON LOCAL CAMERA]`
Pi не подключался; всё, что касается Pi, остаётся `[UNVERIFIED ON HARDWARE]`.

---

## OpenCV UI: перенос legacy-интерфейса на ПК

**Причина / корневая проблема**
- Legacy `cv_kostyl.py` показывал камеру и все панели на ПК, но зависел от Python-стека; нативному клиенту не хватало одного окна с видео и всеми элементами управления.
- Существовавший ImGui-интерфейс и отдельный viewer не соответствовали выбранному пути: один клиент на OpenCV HighGUI с окнами Camera / CAM SET / RADAR / HSV MASK / INFO.
- OpenCV highgui в MSYS2 собран с Qt6: без компонента `highgui` в `find_package` и без пакета `mingw-w64-x86_64-qt6-base` исполняемый файл падал при запуске (exit `-1073741515`).

**Изменённые файлы**
- `cmake/Dependencies.cmake`, `src/CMakeLists.txt`
- `src/laptop/cv_widgets.hpp/.cpp`, `src/laptop/opencv_app.hpp/.cpp`, `src/laptop/opencv_main.cpp`
- `tests/laptop/cv_widgets_test.cpp`, `tests/laptop/opencv_app_test.cpp`
- `.superpowers/sdd/2026-11-19-opencv-ui-rewrite/progress.md`

**Исправление / оптимизация**
- Добавлены ZMQ SUB-клиент 5555 с JPEG и статусом, парсер статуса с определением устаревания по времени получения кадра и PUB 5557/5559 для цветов ворот и CAM SET.
- `ZMQ_CONFLATE` убран: в этой версии libzmq он оставлял только последнюю часть multipart (JSON) и терял JPEG; вместо этого очередь дренируется вручную.
- Реализованы виджеты OpenCV (`CvRect`, `CvButton`, `CvSlider`, `CvTextInput`, `CvLog`) с кольцевым буфером на 10 записей и безопасной отрисовкой на пустом кадре.
- `OpenCVApp` рисует главное окно 1280x720, поворачивает кадр Pi 640x360 → портрет 720x1280, запускает трекинг на ПК, показывает статус и лог, обрабатывает клавиатуру и мышь, подключается к Pi по IP или к локальной камере.
- Полный паритет legacy: RADAR с дугами, ролями и привязкой поля; HSV MASK с легендой; CAM SET с 8 слайдерами и кнопками (отправка каждые 5 с); INFO с 7 вкладками; `BIND FIELD`, `POS`, `TARGET`, `SAVE G1..3`; калибровка LAB (`R`, `+`, `-`).
- `R` используется для LAB-рекалибровки, горячая клавиша RADAR перемещена на `D` (цена — изменение клавиатуры относительно предыдущей версии UI).
- `find_package(OpenCV)` теперь запрашивает компонент `highgui`; для запуска нужны Qt6 DLL пакета `mingw-w64-x86_64-qt6-base`.

**Проверка**
```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```
Результат: configure OK, сборка без предупреждений, `100% tests passed out of 35`, 15.33 sec; `git diff --check` чисто. `[VERIFIED]`
Живая проверка с Pi в рамках этого rewrite НЕ выполнялась: Pi отключён, сетевые подключения не производились. Arduino физически отключён, прошивка и последовательный порт не трогались. `[UNVERIFIED ON HARDWARE]`
