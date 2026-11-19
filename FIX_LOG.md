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
