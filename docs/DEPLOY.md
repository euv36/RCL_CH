# Временный запуск нативного сервиса на Raspberry Pi

Статус: процедура впервые задокументирована как `[UNVERIFIED ON HARDWARE]`, но 2026-11-19 пользователь явно разрешил доступ к Pi, и временный запуск был выполнен — details и фактические результаты в `docs/VERIFICATION.md`. Ниже остаётся описание самой процедуры.

## Принципы безопасности

- Запуск только во временном каталоге `/tmp/camera_cpp_smoke`; постоянный systemd-сервис не создаётся и не включается.
- Сервис запускается только с `--no-serial`; последовательный порт и робот не трогаются.
- Перед запуском сохраняются и хешируются существующие конфигурационные файлы; после остановки они восстанавливаются.
- `ssh` и `rsync` используют уже настроенный ключ оператора; пароли и ключи в репозиторий не попадают.
- Если не выполняется хотя бы один шаг, результат остаётся `[UNVERIFIED ON HARDWARE]` и в отчёте пишется `BLOCKED`, а не `done`.

## Переменные окружения

```bash
export PI_HOST=10.132.122.51
export PI_USER=twox
export PI_DIR=/tmp/camera_cpp_smoke
export PI_BIND_HOST="$PI_HOST"
```

`PI_BIND_HOST` — адрес интерфейса Pi, на котором слушает сервис. Для локальной проверки на самом Pi можно использовать `127.0.0.1`, но тогда smoke-скрипт с ноутбука подключиться не сможет.

## 1. Подготовка и резервная копия

```bash
bash scripts/deploy_pi_temp.sh
```

Скрипт:

1. проверяет наличие `ssh`, `rsync`, `cmake` и компилятора на Pi;
2. создаёт `$PI_DIR` и `$PI_DIR/backup/<метка времени>`;
3. копирует туда найденные `saved_gates.json`, `camera_settings.json`, `field_map.json`, `distance_calib.json` и сохраняет `sha256sum`;
4. синхронизирует исходники из этого репозитория без `build*/`, `local/`, `.git`, `graphify-out/`;
5. собирает `camera_service` в `$PI_DIR/build` с `-DCMAKE_BUILD_TYPE=Release`;
6. **не запускает** сервис автоматически.

## 2. Временный запуск

```bash
ssh "${PI_USER}@${PI_HOST}" \
  "cd ${PI_DIR} && nohup ./build/src/camera_service \
   --no-serial --bind-host '${PI_BIND_HOST}' \
   --port-video 5555 --port-colors 5557 --cam-settings-port 5559 \
   > service.log 2>&1 & echo \$! > service.pid"
```

Проверка слушающего порта:

```bash
ssh "${PI_USER}@${PI_HOST}" "ss -ltnp | grep ':5555'"
```

Ожидается: процесс `camera_service`, порт 5555 слушается. Если порт занят старым Python-сервисом, не используйте `--port-video 5555`; выберите временные порты по разделу Task 20 плана и передайте их в smoke-скрипт.

## 3. Захват мультипарт-кадра

С ноутбука:

```bash
python tests/integration/pi_service_smoke.py \
  --host "$PI_HOST" --duration 5 \
  --output local/pi_smoke_summary.json
```

Ожидается: кадры с префиксом JPEG `FF D8` и суффиксом `FF D9`, второй кадр — корректный JSON с полями `src`, `gates`, `fps`. Скрипт печатает JSON-сводку и возвращает код 0 только если получено хотя бы одно валидное сообщение.

## 4. Необязательная проверка CAM SET

По умолчанию отключена, так как изменяет физические настройки камеры. Для осознанного запуска:

```bash
python tests/integration/pi_service_smoke.py \
  --host "$PI_HOST" --duration 5 --probe-cam-set
```

Безопасное значение — целочисленный `gamma` в пределах legacy-диапазона `72..500`. Ответа на сокете CAM SET нет: успех определяется только по логу `service.log` и readback камеры. Значение нужно вернуть из резервной копии.

## 5. Остановка и восстановление

```bash
bash scripts/stop_pi_temp.sh
```

Скрипт читает `service.pid`, проверяет, что процесс действительно является `camera_service` из `$PI_DIR`, посылает `TERM`, ждёт исчезновения порта и печатает результат. После остановки:

```bash
ssh "${PI_USER}@${PI_HOST}" "ss -ltnp | grep ':5555' || echo 'port closed'"
```

Восстановление конфигов:

```bash
ssh "${PI_USER}@${PI_HOST}" \
  "cd ${PI_DIR} && LATEST=\$(ls -1d backup/* | tail -1) && \
   for f in saved_gates.json camera_settings.json field_map.json distance_calib.json; do \
     if [ -f \"\$LATEST/\$f\" ]; then cp -a \"\$LATEST/\$f\" ./\"\$f\"; fi; \
   done && sha256sum saved_gates.json camera_settings.json field_map.json distance_calib.json 2>/dev/null"
```

Сравните хеши с сохранёнными до запуска.

## 6. Документирование

Фактические команды, вывод, версии пакетов, тайминги и все незавершённые пункты вносятся в `docs/VERIFICATION.md`. Пока hardware-прогон не выполнен, все утверждения остаются `[UNVERIFIED ON HARDWARE]`.
