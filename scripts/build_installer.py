"""Build a production payload and per-user Windows installer. Never package test tools."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from bootstrap_nsis import compiler

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build" / "production"
PAYLOAD = BUILD / "payload"
GENERATED = BUILD / "installer"
OUTPUT = ROOT / "out" / "installer"
QT = ROOT / ".tools" / "Qt" / "6.8.3" / "msvc2022_64"


def run(arguments, **kwargs):
    return subprocess.run(arguments, cwd=ROOT, env={key.upper(): value for key, value in os.environ.items()}, check=True, **kwargs)


def sha256(path):
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def quote(text):
    return str(text).replace("$", "$$").replace('"', '$\\"')


def write_payload_include(files):
    lines = ["!macro InstallPayload"]
    current = None
    directories = set()
    for file in files:
        relative = file.relative_to(PAYLOAD)
        folder = str(relative.parent)
        if folder != current:
            suffix = "" if folder == "." else "\\" + quote(folder)
            lines.append(f'SetOutPath "$INSTDIR{suffix}"')
            current = folder
        lines.append(f'File "{quote(file)}"')
        directories.update(parent for parent in relative.parents if str(parent) != ".")
    lines += ["!macroend", "!macro UninstallPayload"]
    lines.extend(f'Delete "$INSTDIR\\{quote(file.relative_to(PAYLOAD))}"' for file in files)
    # Nonrecursive removal preserves user-created files, including projects and footage.
    lines.extend(f'RMDir "$INSTDIR\\{quote(folder)}"' for folder in sorted(directories, key=lambda p: (len(p.parts), str(p)), reverse=True))
    lines.append("!macroend")
    (GENERATED / "payload.nsh").write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_shell_cleanup():
    config = json.loads((ROOT / "config/windows-integration.json").read_text())
    extensions = config["videoExtensions"]
    entries = [(f'SystemFileAssociations\\{extension}\\shell\\{config["verbId"]}', "command", []) for extension in extensions]
    entries += [
        (config["mediaProgId"], "shell\\open\\command", [(extension, config["mediaProgId"]) for extension in extensions]),
        (config["projectProgId"], "shell\\open\\command", [(config["projectExtension"], config["projectProgId"])]),
        ("Applications\\" + config["executableName"], "shell\\open\\command", []),
    ]
    lines = ["!macro RemoveOwnedShellEntries"]
    for key, command, associations in entries:
        lines.append(f'ReadRegStr $0 HKCU "Software\\Classes\\{quote(key)}\\{command}" ""')
        lines.append('${If} $0 == \'"$INSTDIR\\TrieFrameViewer.exe" "%1"\'')
        for extension, progid in associations:
            lines.append(f'DeleteRegValue HKCU "Software\\Classes\\{quote(extension)}\\OpenWithProgids" "{quote(progid)}"')
        lines.append(f'DeleteRegKey HKCU "Software\\Classes\\{quote(key)}"')
        lines.append("${EndIf}")
    lines.append("!macroend")
    (GENERATED / "shell-cleanup.nsh").write_text("\n".join(lines) + "\n", encoding="utf-8")


def dependency_notices():
    notices = PAYLOAD / "licenses"
    notices.mkdir()
    for file in sorted((ROOT / "packaging/licenses").glob("*.txt")):
        shutil.copy2(file, notices / file.name)
    shutil.copy2(ROOT / "packaging/licenses/sources.json", notices / "license-text-sources.json")
    shutil.copy2(ROOT / ".tools/ffmpeg/LICENSE.txt", notices / "FFmpeg-8-LGPL-3.0.txt")
    shutil.copy2(ROOT / ".tools/nsis-3.13/COPYING", notices / "NSIS.txt")
    sbom = notices / "qt-sbom"
    sbom.mkdir()
    texts = []
    for file in sorted((QT / "sbom").glob("*")):
        if file.is_file():
            shutil.copy2(file, sbom / file.name)
        if file.suffix != ".json":
            continue
        data = json.loads(file.read_text(encoding="utf-8"))
        texts.append("\n" + file.name + "\n" + "=" * 72)
        for package in data.get("packages", []):
            texts.append("\n".join(str(package.get(key, "")) for key in ("name", "versionInfo", "copyrightText", "licenseDeclared", "downloadLocation")))
        for license in data.get("hasExtractedLicensingInfos", []):
            texts.append(license.get("licenseId", "") + "\n" + license.get("extractedText", ""))
    (notices / "Qt-third-party-notices.txt").write_text("\n\n".join(texts), encoding="utf-8")
    shutil.copy2(ROOT / "packaging/THIRD_PARTY_NOTICES.txt", PAYLOAD / "THIRD_PARTY_NOTICES.txt")
    shutil.copy2(ROOT / ".tools/ffmpeg/sdk-lock.json", notices / "ffmpeg-sdk-lock.json")


def main():
    version = re.search(r"project\(TrieFrameViewer VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    makensis = compiler()
    run(["cmake", "--preset", "windows-production"])
    run(["cmake", "--build", "--preset", "windows-production", "--parallel", "4", "--", "/nodeReuse:false"])
    # This is generated staging data, never the user's deployed app or install directory.
    if PAYLOAD.resolve() != ROOT.resolve() / "build" / "production" / "payload":
        raise RuntimeError("Unexpected payload staging path")
    if PAYLOAD.exists():
        shutil.rmtree(PAYLOAD)
    run(["cmake", "--install", str(BUILD), "--config", "Release", "--prefix", str(PAYLOAD)])
    dependency_notices()
    for name in ("TrieFrameViewer.exe", "ffmpeg.exe", "Qt6Core.dll", "Qt6Quick.dll", "plugins/platforms/qwindows.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
        if not (PAYLOAD / name).is_file():
            raise RuntimeError(f"Installer payload is missing {name}")
    if any(PAYLOAD.glob("*test*.exe")) or (PAYLOAD / "Qt6Test.dll").exists():
        raise RuntimeError("Development test dependencies must not enter the installer")
    GENERATED.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    files = sorted(file for file in PAYLOAD.rglob("*") if file.is_file())
    manifest = {"application": "Trie FrameViewer", "version": version, "architecture": "x64", "qtVersion": "6.8.3", "files": {str(file.relative_to(PAYLOAD)).replace("\\", "/"): sha256(file) for file in files}}
    manifest_path = PAYLOAD / "installation-manifest.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    files.append(manifest_path)
    files.sort()
    write_payload_include(files)
    write_shell_cleanup()
    output = OUTPUT / f"TrieFrameViewer-Setup-{version}-x64.exe"
    run([str(makensis), "/V2", "/INPUTCHARSET", "UTF8", f"/DAPP_VERSION={version}", f"/DAPP_ICON={ROOT / 'assets/branding/fv-icon.ico'}", f"/DGENERATED_DIR={GENERATED}", f"/DOUTPUT_FILE={output}", f"/DPAYLOAD_KIB={sum(file.stat().st_size for file in files) // 1024}", str(ROOT / "packaging/windows/installer.nsi")])
    digest = sha256(output)
    (output.with_suffix(".exe.sha256")).write_text(f"{digest}  {output.name}\n", encoding="ascii")
    print(f"Installer: {output}\nSize: {output.stat().st_size / 1024 / 1024:.1f} MiB\nSHA256: {digest}")


if __name__ == "__main__":
    main()
