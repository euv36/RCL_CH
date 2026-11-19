# OpenCV UI Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Собрать на ПК единый нативный клиент `camera_ui` на OpenCV HighGUI, который подключается к Pi по IP, показывает живое видео и воспроизводит весь интерфейс старого `cv_kostyl.py`, при этом Pi остаётся автономным роботом.

**Architecture:** Новый исполняемый файл `camera_ui` (OpenCV `cv::namedWindow`/`imshow`/ mouse callbacks) становится основным путём запуска. Чистые helper-ы (ZMQ-клиент видео, разбор статуса, издатель ворот, виджеты) отделены от UI и покрываются тестами. Уже портированные `camera_vision*`, `camera_common`, `camera_laptop_core/hsv/preview` переиспользуются. Pi не модифицируется.

**Tech Stack:** C++17, MSYS2 MinGW64, OpenCV 5 (core/imgproc/highgui/imgcodecs), libzmq, CMake/CTest, nlohmann/json.

**Spec:** `docs/superpowers/specs/2026-11-19-opencv-ui-rewrite-design.md`

## Global Constraints

- Ответы пользователю — кратко, на русском; идентификаторы/коммиты — английский; комментарии к функциям — русский.
- Pi физически отключён; к Pi подключаться только после явного разрешения. Serial/firmware не трогать.
- Pi остаётся автономным: он шлёт видео и вспомогательный статус; весь UI и его обработка выполняются на ПК.
- Тесты только через `CAMERA_TEST_CHECK`; полный MSYS2-прогон без `-j`; все тесты обязаны оставаться зелёными.
- После реализации: русская запись в `FIX_LOG.md`, отдельный commit, `git push origin native-optimization` без force.
- Новые цели CMake собираются только при `CAMERA_CPP_HAS_OPENCV AND CAMERA_CPP_HAS_ZMQ`.

## Review Focus

- **Biтый/пустой JSON статуса 5555** → не должен ронять UI; тест в Task 2.
- **Устаревший статус Pi (>2 c)** → помечается stale, панели строятся из локальных результатов; тест в Task 2.
- **Multipart без JSON (только JPEG)** → кадр всё равно показывается; тест в Task 1.
- **Потеря соединения с Pi** → последний кадр остаётся на экране, робот не зависит от ПК; тест в Task 1.
- **`unset` роль ворот** → не передаётся в payload 5557; тест в Task 3.
- **Непечатные символы в поле IP** → отбрасываются, лимит 256; переиспользовать `clipboard_text` + тест в Task 5.

---

### Task 1: Клиент видео Pi (ZMQ SUB 5555 + JPEG)

**Files:**
- Create: `src/laptop/pi_video_client.hpp`
- Create: `src/laptop/pi_video_client.cpp`
- Test: `tests/laptop/pi_video_client_test.cpp`

**Interfaces:**
- Consumes: libzmq, OpenCV `imdecode`, nlohmann/json.
- Produces:
  - `struct PiVideoFrame { cv::Mat rgb; bool has_status=false; std::string status_json; };`
  - `class PiVideoClient { public: bool connect(std::string_view host, int port=5555); void disconnect(); bool receive(PiVideoFrame& out); bool connected() const noexcept; };`

- [ ] **Step 1: Write the failing test** — тест поднимает фейковый ZMQ PUB на `tcp://127.0.0.1:15555`, шлёт multipart `[JPEG bytes, JSON bytes]`, проверяет, что `receive()` вернул декодированный `rgb` 640x360 и `has_status=true`, `status_json` совпал. Второй тест шлёт только JPEG → `has_status=false`. Третий — после `disconnect()` `connected()==false`.

- [ ] **Step 2: Run test to verify it fails** — собрать цель,Expected: FAIL (нет заголовка).

- [ ] **Step 3: Implement `PiVideoClient`** — `zmq_ctx_new`, `ZMQ_SUB` + `SUBSCRIBE ""` + `ZMQ_CONFLATE=1` + `connect`; `recv_multipart(ZMQ_DONTWAIT)`; если parts≥1 — `cv::imdecode(parts[0], IMREAD_COLOR)` и `cv::cvtColor(BGR2RGB)`; parts≥2 → `status_json`. При `EAGAIN` вернуть `false`.

- [ ] **Step 4: Run test to verify it passes** — Expected: PASS.

- [ ] **Step 5: Commit** — `git add ... && git commit -m "feat(ui): add Pi video ZMQ client"`

### Task 2: Разбор статуса Pi (stale, битый JSON)

**Files:**
- Create: `src/laptop/status_panel_client.hpp`
- Create: `src/laptop/status_panel_client.cpp`
- Test: `tests/laptop/status_panel_client_test.cpp`

**Interfaces:**
- Consumes: nlohmann/json.
- Produces:
  - `struct GateStatusView { int id; bool enabled=false; bool found=false; bool held=false; float angle_deg=0; float area_px=0; float dist_cm=0; bool dist_reliable=false; std::string role="unset"; };`
  - `struct ParsedStatus { bool valid=false; bool stale=false; float fps=0; std::string src; std::array<int,2> frame_wh{0,0}; std::array<GateStatusView,3> gates; };`
  - `ParsedStatus parse_status_payload(std::string_view text, std::chrono::steady_clock::time_point now, double stale_seconds=2.0);`

- [ ] **Step 1: Write failing tests** — валидный JSON с тремя воротами → поля заполнены; невалидный JSON → `valid=false`, без исключений; пустая строка → `valid=false`; статус с `ts` старше 2 c → `stale=true`.

- [ ] **Step 2: Run to verify FAIL.**

- [ ] **Step 3: Implement `parse_status_payload`** — обернуть `json::parse` в try/catch; читать ключи `"1".."3"`; `enabled=false` нормализовать в `found=false/held=false/area=0` (как в legacy viewer); `stale` по возрасту `ts` либо по времени последнего успешного приёма (в тесте передать `now`).

- [ ] **Step 4: Run to verify PASS.**

- [ ] **Step 5: Commit** — `git commit -m "feat(ui): add Pi status parser"`

### Task 3: Издатель ворот и настроек камеры (PUB 5557/5559)

**Files:**
- Create: `src/laptop/gate_publisher.hpp`
- Create: `src/laptop/gate_publisher.cpp`
- Test: `tests/laptop/gate_publisher_test.cpp`

**Interfaces:**
- Consumes: libzmq.
- Produces:
  - `struct GateColorOut { int id; std::array<int,3> rgb; std::string role; };`
  - `std::string build_gate_payload(const std::vector<GateColorOut>& gates);`
  - `class GatePublisher { public: bool bind(int colors_port=5557, int cam_port=5559); void publish_gates(const std::vector<GateColorOut>&); void publish_cam_settings(const std::array<float,8>& controls); void close(); };`

- [ ] **Step 1: Write failing tests** — `build_gate_payload` для G1/G2 с role `ours/opponent` → JSON содержит `gates[].id/rgb/role` и legacy `rgb` только для G1; `role="unset"` → поле `role` отсутствует; пустой список → `{"gates":[]}`. Интеграционный: фейковый SUB получает сообщение после `publish_gates`.

- [ ] **Step 2: Run to verify FAIL.**

- [ ] **Step 3: Implement** — PUB sockets bind `tcp://*:5557/5559`, `SNDHWM`, payload как в legacy; `publish_cam_settings` шлёт `{"cmd":"set","controls":{...}}` с именами 8 контролов из `cam_settings_config.hpp`.

- [ ] **Step 4: Run to verify PASS.**

- [ ] **Step 5: Commit** — `git commit -m "feat(ui): add gate and camera publisher"`

### Task 4: OpenCV-виджеты (кнопки, слайдеры, список, ввод, лог)

**Files:**
- Create: `src/laptop/cv_widgets.hpp`
- Create: `src/laptop/cv_widgets.cpp`
- Test: `tests/laptop/cv_widgets_test.cpp`

**Interfaces:**
- Consumes: OpenCV core/imgproc.
- Produces:
  - `struct CvRect { int x,y,w,h; bool contains(int px,int py) const noexcept; };`
  - `struct CvButton { CvRect rect; std::string label; bool hover=false; void draw(cv::Mat&, bool active) const; };`
  - `struct CvSlider { CvRect rect; int value=0,min=0,max=100; std::string label; void draw(cv::Mat&) const; int value_from_x(int px) const noexcept; };`
  - `struct CvTextInput { CvRect rect; std::string text; bool active=false; void draw(cv::Mat&) const; };`
  - `class CvLog { public: void push(std::string line); void draw(cv::Mat&, const CvRect&) const; };`

- [ ] **Step 1: Write failing tests** — `CvRect::contains` границы; `CvSlider::value_from_x` крайние и середина; `CvLog::push` держит только последние 10 записей; `draw` на пустом `cv::Mat` не бросает.

- [ ] **Step 2: Run to verify FAIL.**

- [ ] **Step 3: Implement** — рисование через `cv::rectangle/putText`, кириллица латиницей в UI-лейблах где нет шрифта (или truncate), лог — кольцевой буфер 10.

- [ ] **Step 4: Run to verify PASS.**

- [ ] **Step 5: Commit** — `git commit -m "feat(ui): add OpenCV widgets"`

### Task 5: Главное окно `camera_ui` — видео, источник, CONNECT, трекинг, панели

**Files:**
- Create: `src/laptop/opencv_app.hpp`
- Create: `src/laptop/opencv_app.cpp`
- Create: `src/laptop/opencv_main.cpp`
- Modify: `src/CMakeLists.txt`
- Test: `tests/laptop/opencv_app_test.cpp` (чистые helpers; GUI в ctest не открываем)

**Interfaces:**
- Consumes: Task 1–4, `AppState`, `parse_connect`, `sanitize_clipboard_bytes`, `HelpRu`, `radar_gate_visible`, `TrackingEngine`, `GateStore`, `GatePublisher`.
- Produces: `class OpenCVApp { public: int run(); void process_frame(const cv::Mat& rgb); std::string source_input() const; };`

- [ ] **Step 1: Write failing tests** — `process_frame` с синтетическим кадром не падает и обновляет internal state; `source_input()` после `append_source_text("10.0.0.1")` возвращает IP; `parse_connect` для `10.0.0.1` даёт `Pi` хост/порт 5555.

- [ ] **Step 2: Run to verify FAIL.**

- [ ] **Step 3: Implement `OpenCVApp`** — `cv::namedWindow("Camera", WINDOW_NORMAL)`, canvas 1280x720, `setMouseCallback`; цикл: `client.receive()` → `parse_status_payload` → `TrackingEngine::track` на повёрнутом/масштабированном кадре → отрисовка `img_src`/`img_proc`, верхнего ряда, левых/правых панелей, нижних слайдеров, кнопок; `cv::imshow`, `cv::waitKey(1)`. Окна RADAR/HSV/CAM SET/INFO — отдельные `namedWindow`, ESC/крестик закрывают. PUB цветов при SAVE и автоresend 10 c/2 c.

- [ ] **Step 4: Register CMake targets** — `camera_ui` + `kCameraCppOpenCVAppTest`, gate `CAMERA_CPP_HAS_OPENCV AND CAMERA_CPP_HAS_ZMQ`, link `camera_laptop_core/hsv/preview`, `camera_vision_cv`, `camera_common`, `${OpenCV_LIBS}`, `${ZMQ_LIBRARIES}`.

- [ ] **Step 5: Run targeted tests** — `ctest -R '^(pi_video_client|status_panel_client|gate_publisher|cv_widgets|opencv_app)$'`.

- [ ] **Step 6: Commit** — `git commit -m "feat(ui): add OpenCV camera UI"`

### Task 6: Полный перенос функций (RADAR, HSV, CAM SET, INFO, field binding)

**Files:**
- Modify: `src/laptop/opencv_app.cpp`
- Modify: `src/laptop/opencv_app.hpp`
- Test: `tests/laptop/opencv_app_test.cpp`

**Interfaces:**
- Consumes: `HelpRu::sections()`, `build_hsv_mask_bgra`, `RadarGate`, `cam_apply_status_text`, `FieldMap`, `GateStore`.

- [ ] **Step 1: Write failing tests** — генерация кадра радара/HSV/INFO из синтетических данных не пустая и не падает; `cam_apply_status_text` для `Applied`/`Idle` не пустой; роль `unset` не попадает в payload.

- [ ] **Step 2: Run to verify FAIL.**

- [ ] **Step 3: Implement** — отдельные окна: RADAR 440x440 (дуги, роли, est/EDGE-совет), HSV MASK (letterbox + легенда), CAM SET (8 trackbars + canvas-кнопки SAFE DEFAULTS/SAVE NOW/SEND TO PI/role*, SEND каждые 5 c), INFO (7 вкладок). Кнопки `BIND FIELD`, `POS`, `TARGET`, `SAVE G1..3` ↔ `FieldMap`/`GateStore`/`GatePublisher`. `R/+/-` калибровка LAB по выделению прямоугольника. Локальная камера через `cv::VideoCapture` (1920x1080 MJPG, rotate 90 CCW).

- [ ] **Step 4: Run targeted tests.**

- [ ] **Step 5: Commit** — `git commit -m "feat(ui): complete legacy feature parity"`

### Task 7: Верификация, документация, push

**Files:**
- Modify: `FIX_LOG.md`
- Modify: `docs/VERIFICATION.md`

- [ ] **Step 1: Полная сборка MSYS2 без -j** — `cmake -S . -B build-msys2` + `cmake --build build-msys2` (без `-j`) + `ctest --test-dir build-msys2 --output-on-failure`. Expected: все тесты Passed, без предупреждений.

- [ ] **Step 2: git diff --check** — Expected: чисто.

- [ ] **Step 3: Написать русскую запись в FIX_LOG.md** — причина, файлы, исправление, проверка (тесты, Pi отключён).

- [ ] **Step 4: Обновить docs/VERIFICATION.md** — как запускать `camera_ui.exe`, локальные тесты, что живая проверка с Pi не выполнялась (Pi отключён).

- [ ] **Step 5: Commit + push** — `git commit -m "feat(ui): OpenCV client with full legacy UI"`, `git push origin native-optimization` (без force).

---

## Self-Review

- **Spec coverage:** all spec sections map to Tasks 1–7; Pi/PC split in Global Constraints; phased delivery represented by Tasks 5–6.
- **Step scan:** each step has one checkable result; no TBD lines.
- **Type consistency:** signatures defined once in each task's Interfaces and reused verbatim later.
- **Review Focus:** each listed failure mode has a test in its owning task.
- **Proportion:** plan is shorter than the code it describes; no full bodies written.
