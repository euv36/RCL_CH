# Verification report

## Task 12 — temporary Pi smoke procedure

Status: static checks — `[VERIFIED]`; earlier live run — `[UNVERIFIED ON HARDWARE]`. Actual hardware run was performed later on 2026-11-19 after the user explicitly provided Pi IP and authorized access; see "Hardware run 2026-11-19" below.

Static checks performed locally:

```powershell
& 'C:\msys64\usr\bin\bash.exe' -n scripts/deploy_pi_temp.sh
& 'C:\msys64\usr\bin\bash.exe' -n scripts/stop_pi_temp.sh
python -m py_compile tests/integration/pi_service_smoke.py
git diff --check
```

Result: all commands exited 0; generated `__pycache__/` removed and ignored.

Not verified until a human explicitly authorizes Pi access:

- build on Pi with system OpenCV/ZMQ;
- ports 5555/5557/5559 availability and temporary service start;
- multipart JPEG/JSON capture for 5 seconds;
- CAM SET application and config checksum restoration;
- port closure after `scripts/stop_pi_temp.sh`.

Local loopback coverage of the same wire contract already passed in Task 11 (`pi_zmq_loopback`, 127.0.0.1:15555/15557/15559, fake V4L2), but it is not a hardware proof.

## Hardware run 2026-11-19 — `[VERIFIED ON HARDWARE]`

Пользователь явно дал IP `10.132.122.51` и разрешил подключиться и запустить прогу камеры. Pi не прошивался, системный сервис не создавался, последовательный порт и робот не трогались.

**Что выполнялось**
1. SSH-проверка через локальный helper (пароль уже настроен пользователем): `hostname`, `whoami`, `uptime` → `twox`, uptime 3 min, `temp=25.3'C`.
2. Сборка исходников `ba30c23` в `/tmp/camera_cpp_smoke`: OpenCV 4.10.0, libzmq найдены, `camera_service` собран без ошибок (laptop-цели пропущены — на Pi нет SDL2/ImGui).
3. Временный запуск без systemd:
   ```bash
   cd /tmp/camera_cpp_smoke
   nohup ./build/src/camera_service --no-serial --bind-host 10.132.122.51 \
     --port-video 5555 --port-colors 5557 --cam-settings-port 5559 \
     > service.log 2>&1 & echo $! > service.pid
   ```
   PID `1537`; слушаются `5557` (colors) и `5559` (cam_settings); лог подтверждает PUB `tcp://10.132.122.51:5555`, камера `/dev/video0 1920x1080 MJPG @30`.
4. Smoke-приём с ноутбука:
   ```powershell
   python tests/integration/pi_service_smoke.py --host 10.132.122.51 --duration 5 --output local/pi_smoke_summary.json
   ```
   Результат: `ok=true`, `messages=1`, `valid_jpeg=1`, `valid_json=1`, `fps≈10.75`, `frame_wh=[640,360]`, `src=pi`.
5. После включения G1 жёлтым цветом через PUB на `5557` повторный smoke: `ok=true`, `enabled=true`, `role=ours`, `found=false` (в поле зрения камеры нет жёлтых ворот). Конфиг сохранён в `/tmp/camera_cpp_smoke/saved_gates.json`.

**Не проверялось**
- CAM SET с изменением физических настроек камеры (`--probe-cam-set` не запускался).
- Остановка сервиса и восстановление конфигов: сервис намеренно оставлен запущенным для дальнейших тестов пользователя.
- Прошивка firmware, работа с роботом, сон ноутбука — не выполнялись.

Все сообщения про достоверность обнаружения ворот остаются относительными к текущему кадру: `found=false` в пустой комнате — ожидаемый результат.

## OpenCV HighGUI client `camera_ui` — `[VERIFIED LOCALLY]`, Pi `[UNVERIFIED ON HARDWARE]`

Новый клиент заменяет legacy `cv_kostyl.py`: одно приложение показывает видео Pi и все панели (CAM SET, RADAR, HSV MASK, INFO). Pi в этой итерации не модифицировался и не подключался.

**Сборка и локальные тесты**

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& 'C:\msys64\mingw64\bin\cmake.exe' -S . -B build-msys2
& 'C:\msys64\mingw64\bin\cmake.exe' --build build-msys2
& 'C:\msys64\mingw64\bin\ctest.exe' --test-dir build-msys2 --output-on-failure
```

Результат: configure OK, сборка без предупреждений, `100% tests passed out of 35`, 15.33 sec. `git diff --check` чисто.

**Запуск на ПК**

```powershell
$env:Path = 'C:\msys64\mingw64\bin;C:\msys64\usr\bin;' + $env:Path
& '.\build-msys2\src\camera_ui.exe'
```

В поле источника ввести `10.132.122.51` и нажать CONNECT (или Enter). Окно `Camera` 1280x720 показывает портретный кадр и результат трекинга; верхний ряд кнопок переключает режимы и панели, `SAVE G1..3` сохраняет цвета ворот, `BIND FIELD` привязывает поле, `CAM SET` отправляет настройки камеры на Pi каждые 5 с.

Для `cmd.exe`:

```cmd
set "PATH=C:\msys64\mingw64\bin;C:\msys64\usr\bin;%PATH%"
build-msys2\src\camera_ui.exe
```

HighGUI в MSYS2 использует Qt6; необходимые DLL установлены пакетом `mingw-w64-x86_64-qt6-base`.

**Не проверялось**

- Живое подключение к Pi `10.132.122.51` и приём кадра с порта 5555 в рамках этого rewrite: Pi отключён, явного разрешения на подключение не было.
- Отправка CAM SET и цветов ворот на реальный Pi, работа с роботом по serial, прошивка — не выполнялись.
- Сон ноутбука не выполнялся.
