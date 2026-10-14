"""Fetch a pinned portable NSIS compiler into .tools; no system installation."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VERSION = "3.13"
SHA256 = "ba63dffc4410ee89193e1cb5a41989991bd77c61068da17e3156d136b7b0b3d8"
URL = f"https://downloads.sourceforge.net/project/nsis/NSIS%203/{VERSION}/nsis-{VERSION}.zip"


def compiler():
    tools = ROOT / ".tools"
    executable = tools / f"nsis-{VERSION}" / "makensis.exe"
    archive = tools / f"nsis-{VERSION}.zip"
    tools.mkdir(exist_ok=True)
    if not archive.exists():
        request = urllib.request.Request(URL, headers={"User-Agent": "TrieFrameViewer-build"})
        with urllib.request.urlopen(request, timeout=60) as response, archive.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
    with archive.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != SHA256:
            raise RuntimeError("NSIS archive checksum mismatch")
    if not executable.exists():
        with zipfile.ZipFile(archive) as package:
            for member in package.infolist():
                if not (tools / member.filename).resolve().is_relative_to(tools.resolve()):
                    raise RuntimeError("Unsafe NSIS archive member")
            package.extractall(tools)
    return executable


if __name__ == "__main__":
    print(compiler())
