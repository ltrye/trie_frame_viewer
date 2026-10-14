"""Publish a verified installer to origin's GitHub release using existing Git credentials."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
API_VERSION = "2026-03-10"


def git(*arguments):
    environment = {key.upper(): value for key, value in os.environ.items()}
    environment.update(GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never")
    result = subprocess.run(["git", *arguments], cwd=ROOT, env=environment,
                            capture_output=True, text=True, check=True, timeout=60)
    return result.stdout.strip()


def repository():
    remote = git("remote", "get-url", "origin")
    match = re.fullmatch(r"(?:https://github\.com/|git@github\.com:)([\w.-]+/[\w.-]+?)(?:\.git)?/?", remote)
    if not match:
        raise RuntimeError("origin must be a GitHub HTTPS or SSH repository without embedded credentials")
    return match.group(1)


def credential(repo):
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if token:
        return token
    environment = {key.upper(): value for key, value in os.environ.items()}
    environment.update(GIT_TERMINAL_PROMPT="0", GCM_INTERACTIVE="never")
    result = subprocess.run(
        ["git", "credential", "fill"], cwd=ROOT, env=environment, capture_output=True,
        input=f"protocol=https\nhost=github.com\npath={repo}.git\n\n", text=True, timeout=30)
    values = dict(line.split("=", 1) for line in result.stdout.splitlines() if "=" in line)
    if result.returncode or not values.get("password"):
        raise RuntimeError("Authenticate origin with Git Credential Manager, or set GH_TOKEN with Contents write access")
    return values["password"]


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, request, file, code, message, headers, new_url):
        return None  # Never forward a credential through an API/upload redirect.


class GitHub:
    def __init__(self, repo, token):
        self.repo = repo
        self.token = token  # Memory only: never write credentials to disk or output.
        self.opener = urllib.request.build_opener(NoRedirect())

    def request(self, method, path, data=None, content_type="application/json", missing_ok=False):
        url = path if path.startswith("https://") else f"https://api.github.com/repos/{self.repo}{path}"
        parsed = urllib.parse.urlparse(url)
        if parsed.scheme != "https" or parsed.hostname not in {"api.github.com", "uploads.github.com"}:
            raise RuntimeError("Refusing to send credentials to an unexpected API host")
        headers = {"Authorization": "Bearer " + self.token, "Accept": "application/vnd.github+json",
                   "X-GitHub-Api-Version": API_VERSION, "User-Agent": "TrieFrameViewer-release-publisher"}
        if data is not None:
            headers["Content-Type"] = content_type
            if not isinstance(data, bytes):
                data = json.dumps(data).encode("utf-8")
        request = urllib.request.Request(url, data=data, headers=headers, method=method)
        try:
            with self.opener.open(request, timeout=180) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if missing_ok and error.code == 404:
                return None
            raise RuntimeError(f"GitHub {method} request failed (HTTP {error.code}); check repository permissions") from None

    def release(self, tag):
        release = self.request("GET", "/releases/tags/" + urllib.parse.quote(tag, safe=""), missing_ok=True)
        if release:
            return release
        # Drafts may not be returned by the tag endpoint.
        return next((item for item in self.request("GET", "/releases?per_page=100") if item["tag_name"] == tag), None)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify_asset(asset, file, expected):
    if asset.get("state") != "uploaded" or asset.get("size") != file.stat().st_size:
        raise RuntimeError(f"Uploaded asset is incomplete: {file.name}")
    if asset.get("digest") != "sha256:" + expected:
        raise RuntimeError(f"GitHub's asset checksum does not match: {file.name}. The draft was left for inspection.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    action = parser.add_mutually_exclusive_group()
    action.add_argument("--check", action="store_true", help="Validate files, pushed tag, and access without changing GitHub")
    action.add_argument("--publish", action="store_true", help="Publish after all assets pass verification (otherwise leave a draft)")
    parser.add_argument("--tag", help="Must match the CMake application version, e.g. v1.0.0")
    parser.add_argument("--notes", type=Path, help="Release notes; defaults to docs/releases/<tag>.md")
    args = parser.parse_args()
    version = re.search(r"project\(TrieFrameViewer VERSION ([0-9.]+)", (ROOT / "CMakeLists.txt").read_text()).group(1)
    tag = args.tag or "v" + version
    if tag != "v" + version:
        raise RuntimeError("The release tag must match the application version: v" + version)
    if git("status", "--porcelain"):
        raise RuntimeError("Commit source and release notes before publishing")
    head = git("rev-parse", "HEAD")
    if git("rev-parse", f"{tag}^{{commit}}") != head:
        raise RuntimeError("The release tag must point to the current source commit; never move an existing release tag")
    refs = git("ls-remote", "origin", f"refs/tags/{tag}", f"refs/tags/{tag}^{{}}")
    remote_refs = dict(reversed(line.split()) for line in refs.splitlines())
    if remote_refs.get(f"refs/tags/{tag}^{{}}", remote_refs.get(f"refs/tags/{tag}")) != head:
        raise RuntimeError("Push the matching release tag to origin before publishing")
    notes = (args.notes or ROOT / "docs" / "releases" / f"{tag}.md").read_text(encoding="utf-8").strip()
    if not notes:
        raise RuntimeError("Release notes must not be empty")
    installer = ROOT / "out" / "installer" / f"TrieFrameViewer-Setup-{version}-x64.exe"
    checksum = installer.with_suffix(".exe.sha256")
    expected = digest(installer)
    if checksum.read_text(encoding="ascii").strip() != f"{expected}  {installer.name}":
        raise RuntimeError("The installer does not match its .sha256 file; rebuild before publishing")
    repo = repository()
    api = GitHub(repo, credential(repo))
    info = api.request("GET", "")
    if not info.get("permissions", {}).get("push"):
        raise RuntimeError("GitHub credentials do not have push/release access to origin")
    assets = [(installer, expected), (checksum, digest(checksum))]
    print(f"Repository: {repo} ({'private' if info['private'] else 'public'})\nTag: {tag}\nSource: {head}\nInstaller SHA256: {expected}", flush=True)
    release = api.release(tag)
    if release:
        existing = {item["name"]: item for item in release["assets"]}
        for file, expected_hash in assets:
            if file.name in existing:
                verify_asset(existing[file.name], file, expected_hash)
            elif not release["draft"]:
                raise RuntimeError("A published release is missing assets; inspect it before changing it")
    if args.check:
        print("Release preflight passed; GitHub was not changed.")
        return
    if release and not release["draft"]:
        print("Matching release is already published: " + release["html_url"])
        return
    download_base = f"https://github.com/{repo}/releases/download/{urllib.parse.quote(tag, safe='')}"
    body = notes + f"\n\n## Downloads\n\n- [Windows x64 installer]({download_base}/{installer.name})\n- [SHA-256 checksum]({download_base}/{checksum.name})\n\nInstaller SHA-256: `{expected}`\n"
    if not release:
        release = api.request("POST", "/releases", {"tag_name": tag, "target_commitish": head,
                              "name": f"Trie FrameViewer {tag}", "body": body, "draft": True, "prerelease": False})
    for file, expected_hash in assets:
        existing = next((item for item in release["assets"] if item["name"] == file.name), None)
        if existing:
            verify_asset(existing, file, expected_hash)
            continue
        url = release["upload_url"].split("{", 1)[0] + "?" + urllib.parse.urlencode({"name": file.name})
        print("Uploading " + file.name, flush=True)
        uploaded = api.request("POST", url, file.read_bytes(), "application/octet-stream")
        verify_asset(uploaded, file, expected_hash)
    # Re-read the server state so publishing can never race ahead of asset verification.
    release = api.request("GET", f"/releases/{release['id']}")
    for file, expected_hash in assets:
        asset = next((item for item in release["assets"] if item["name"] == file.name), None)
        if not asset:
            raise RuntimeError("Missing uploaded asset: " + file.name)
        verify_asset(asset, file, expected_hash)
    release = api.request("PATCH", f"/releases/{release['id']}", {"body": body, "draft": not args.publish,
                          **({"make_latest": "true"} if args.publish else {})})
    print(("Published: " if args.publish else "Draft ready: ") + release["html_url"])


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError, ValueError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
