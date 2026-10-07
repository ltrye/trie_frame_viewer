"""Install a checksum-verified FFmpeg SDK inside this checkout, never globally."""
import hashlib
import json
from pathlib import Path
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / ".tools" / "ffmpeg"
BASE = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/"
ASSET = "ffmpeg-n8.1-latest-win64-lgpl-shared-8.1.zip"

def fetch(url):
    request = urllib.request.Request(url, headers={"User-Agent": "TrieFrameViewer-dev-bootstrap"})
    return urllib.request.urlopen(request, timeout=90)

def main():
    if (DEST / "sdk-lock.json").exists():
        print("Using existing FFmpeg SDK:", DEST)
        return
    checksums = fetch(BASE + "checksums.sha256").read().decode()
    expected = next(line.split()[0] for line in checksums.splitlines() if line.split()[-1].lstrip("*") == ASSET)
    archive = ROOT / ".tools" / ASSET
    print("Downloading", ASSET, flush=True)
    digest = hashlib.sha256()
    with fetch(BASE + ASSET) as response, archive.open("wb") as output:
        while chunk := response.read(1024 * 1024):
            output.write(chunk)
            digest.update(chunk)
    if digest.hexdigest() != expected:
        raise RuntimeError("FFmpeg archive checksum does not match release checksums")
    DEST.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as sdk:
        for entry in sdk.infolist():
            relative = Path(*Path(entry.filename).parts[1:])
            if not relative.parts:
                continue
            target = (DEST / relative).resolve()
            if not target.is_relative_to(DEST.resolve()):
                raise RuntimeError("Unsafe archive member")
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(sdk.read(entry))
    (DEST / "sdk-lock.json").write_text(json.dumps({"asset": ASSET, "sha256": expected, "url": BASE + ASSET}, indent=2))
    print("Installed SDK:", DEST, flush=True)

if __name__ == "__main__":
    main()
