"""Rebuild and relaunch the local desktop bundle when source files change."""

import subprocess
import sys
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
if sys.platform == "darwin":
    WATCH_FILES = ("config.toml.example", "packaging/macos-entitlements.plist")
    WATCH_DIRS = ("assets", "native/macos")
else:
    WATCH_FILES = ("config.toml.example",)
    WATCH_DIRS = ("assets", "native/windows")


def snapshot() -> dict[str, tuple[int, int]]:
    paths = [ROOT / name for name in WATCH_FILES]
    for directory in WATCH_DIRS:
        paths.extend(
            path for path in (ROOT / directory).rglob("*")
            if path.is_file() and not {".build", "target"}.intersection(path.parts)
        )
    return {
        str(path.relative_to(ROOT)): (path.stat().st_mtime_ns, path.stat().st_size)
        for path in paths if path.is_file()
    }


def build_command() -> tuple[list[str], Path]:
    if sys.platform == "darwin":
        return (["bash", "native/macos/build.sh"],
                ROOT / "dist-native" / "KeyScribe.app" / "Contents" / "MacOS" / "KeyScribe")
    if sys.platform == "win32":
        return (["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                 "-File", "native/windows/build.ps1"],
                ROOT / "dist-native" / "KeyScribe.exe")
    raise SystemExit("개발 빌드는 macOS와 Windows에서만 지원합니다.")


def stop(process: subprocess.Popen | None) -> None:
    if process is None or process.poll() is not None:
        return
    process.terminate()
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait()


def main() -> None:
    command, executable = build_command()
    process = None
    observed = snapshot()
    print("소스 변경을 감시합니다. 종료: Ctrl+C", flush=True)
    try:
        while True:
            print("개발용 앱을 빌드합니다…", flush=True)
            stop(process)
            process = None
            build_snapshot = snapshot()
            result = subprocess.run(command, cwd=ROOT, check=False)
            if snapshot() != build_snapshot:
                print("빌드 중 추가 변경이 있어 다시 빌드합니다.", flush=True)
                continue
            if result.returncode == 0 and executable.is_file():
                try:
                    process = subprocess.Popen([str(executable)], cwd=ROOT)
                    print(f"앱 실행: {executable}", flush=True)
                except OSError as exc:
                    print(f"앱 실행 실패: {exc}", file=sys.stderr, flush=True)
            else:
                print("빌드 실패. 소스를 수정하면 다시 시도합니다.", file=sys.stderr, flush=True)

            observed = snapshot()
            while True:
                time.sleep(0.5)
                current = snapshot()
                if current == observed:
                    continue
                print("변경을 감지했습니다. 저장이 끝나기를 기다립니다…", flush=True)
                time.sleep(0.8)
                current = snapshot()
                if current != observed:
                    observed = current
                    break
    except KeyboardInterrupt:
        print("개발 모드를 종료합니다.", flush=True)
    finally:
        stop(process)


if __name__ == "__main__":
    main()
