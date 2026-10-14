"""Exercise a fresh installer in an isolated workspace folder; preserve user files."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import winreg

ROOT = Path(__file__).resolve().parents[1]
TEST_ROOT = ROOT / "artifacts" / "installer-smoke"
TARGET = TEST_ROOT / "Trie FrameViewer"
UNINSTALL_KEY = r"Software\Microsoft\Windows\CurrentVersion\Uninstall\TrieFrameViewer"
INSTALL_KEY = r"Software\Trie\FrameViewer\Installation"
CONFIG = json.loads((ROOT / "config/windows-integration.json").read_text())


def read_registry(key, name=""):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as handle:
            return winreg.QueryValueEx(handle, name)[0]
    except FileNotFoundError:
        return None


def key_exists(key):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, key, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY):
            return True
    except FileNotFoundError:
        return False


def clean_environment():
    environment = {key.upper(): value for key, value in os.environ.items()}
    system = Path(environment["SYSTEMROOT"])
    environment["PATH"] = str(system / "System32") + ";" + str(system)
    for key in list(environment):
        if key.startswith(("QT_", "QML_", "QSG_")):
            environment.pop(key)
    return environment


def run(arguments, timeout=90):
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return subprocess.run(arguments, cwd=TEST_ROOT, env=clean_environment(), startupinfo=startup,
                          capture_output=True, check=True, timeout=timeout)


def uninstall(executable):
    # NSIS requires _?= to be the last, unquoted directory argument.
    run(f'"{executable}" /S _?={TARGET}')
    if executable.exists():
        executable.unlink()  # The direct-running test uninstaller cannot delete its own image.


def main():
    if TARGET.resolve() != ROOT.resolve() / "artifacts" / "installer-smoke" / "Trie FrameViewer":
        raise RuntimeError("Unexpected installer test target")
    if key_exists(UNINSTALL_KEY) or key_exists(INSTALL_KEY):
        raise RuntimeError("A registered installation already exists; do not replace it during testing")
    if TARGET.exists() and any(path.name != "user-project.tfv" for path in TARGET.iterdir()):
        raise RuntimeError("Installer test directory is not empty; inspect it before retrying")
    version = re.search(r"project\(TrieFrameViewer VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    installer = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "out/installer" / f"TrieFrameViewer-Setup-{version}-x64.exe"
    TEST_ROOT.mkdir(parents=True, exist_ok=True)
    classes = "Software\\Classes\\"
    owned_keys = [classes + "Applications\\" + CONFIG["executableName"], classes + CONFIG["mediaProgId"], classes + CONFIG["projectProgId"]]
    owned_keys += [classes + f"SystemFileAssociations\\{extension}\\shell\\{CONFIG['verbId']}" for extension in CONFIG["videoExtensions"]]
    test_registration = not any(key_exists(key) for key in owned_keys)
    before_default = read_registry(classes + ".mp4")
    before_choice = read_registry(r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.mp4\UserChoice", "ProgId")
    arguments = [str(installer), "/S", "/NOSHORTCUTS"]
    if not test_registration:
        arguments.append("/NOCONTEXTMENU")
    # Like _?=, NSIS /D= consumes the final unquoted path, including spaces.
    command_line = subprocess.list2cmdline(arguments) + f" /D={TARGET}"
    report = {"contextRegistrationTested": test_registration}
    uninstaller = TARGET / "Uninstall.exe"
    try:
        run(command_line)
        report["installed"] = uninstaller.is_file() and (TARGET / CONFIG["executableName"]).is_file()
        assert report["installed"], "Installer did not install the app"
        manifest = json.loads((TARGET / "installation-manifest.json").read_text())
        for relative, digest in manifest["files"].items():
            file = (TARGET / relative).resolve()
            assert file.is_relative_to(TARGET.resolve()), "Unsafe manifest path"
            with file.open("rb") as stream:
                assert hashlib.file_digest(stream, "sha256").hexdigest() == digest, relative
        report["payloadHashesMatch"] = True
        executable = TARGET / CONFIG["executableName"]
        help_text = run([str(executable), "--help"]).stdout
        assert b"--smoke-test" not in help_text and b"--benchmark" not in help_text
        report["productionBuildWithoutTestCommands"] = True
        media = TEST_ROOT / "shot.v002 with space.mp4"
        shutil.copy2(ROOT / "artifacts/reference-test.mp4", media)
        screenshot = TEST_ROOT / "installed-playback.png"
        if test_registration:
            command = read_registry(classes + f"SystemFileAssociations\\.mp4\\shell\\{CONFIG['verbId']}\\command")
            assert command == f'"{executable}" "%1"', command
            command = command.replace('"%1"', subprocess.list2cmdline([str(media)]))
            command += " " + subprocess.list2cmdline(["--screenshot", str(screenshot)])
            run(command, timeout=30)
            report["registeredCommandOpensVideoWithSpaces"] = True
        else:
            run([str(executable), str(media), "--screenshot", str(screenshot)], timeout=30)
        assert screenshot.is_file() and screenshot.stat().st_size > 1000
        report["launchesWithoutSdkPaths"] = True
        project = TARGET / "user-project.tfv"
        project.write_text('{"userNote": "Keep this project"}\n', encoding="utf-8")
    finally:
        if uninstaller.exists():
            uninstall(uninstaller)
    assert not (TARGET / CONFIG["executableName"]).exists()
    assert not key_exists(UNINSTALL_KEY) and not key_exists(INSTALL_KEY)
    assert (TARGET / "user-project.tfv").read_text(encoding="utf-8") == '{"userNote": "Keep this project"}\n'
    if test_registration:
        assert not any(key_exists(key) for key in owned_keys)
        report["uninstallRemovesOwnedRegistrations"] = True
    assert read_registry(classes + ".mp4") == before_default
    assert read_registry(r"Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.mp4\UserChoice", "ProgId") == before_choice
    report.update({"uninstalled": True, "userProjectPreserved": True, "defaultPlayerPreserved": True})
    (TEST_ROOT / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
