"""Small Windows development entry point. All output stays in this checkout."""
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
DEPLOYMENT = ROOT / "out" / "TrieFrameViewer"

def run(arguments):
    # Some desktop executors provide both Path and PATH. MSBuild rejects them.
    environment = {key.upper(): value for key, value in os.environ.items()}
    subprocess.run(arguments, cwd=ROOT, env=environment, check=True)

def main():
    operation = sys.argv[1] if len(sys.argv) > 1 else "build"
    if operation == "configure":
        run(["cmake", "--preset", "windows"])
    elif operation == "build":
        run(["cmake", "--build", "--preset", "windows", "--parallel", "4", "--", "/nodeReuse:false"])
    elif operation == "test":
        run(["ctest", "--preset", "windows"])
    elif operation == "deploy":
        if len(sys.argv) > 2:
            raise SystemExit("Deployment uses out/TrieFrameViewer. Run: python scripts/dev.py deploy")
        executable = DEPLOYMENT / "TrieFrameViewer.exe"
        if executable.exists():
            try:
                # Check the Windows executable lock before replacing any bundle files.
                with executable.open("r+b"):
                    pass
            except PermissionError:
                raise SystemExit("Cannot update the deployed executable. Close Trie FrameViewer, then retry deploy.")
        run(["cmake", "--install", "build", "--config", "Release", "--prefix", str(DEPLOYMENT)])
    elif operation == "installer":
        run([sys.executable, "scripts/build_installer.py"])
    else:
        raise SystemExit("Use configure, build, test, deploy, or installer")

if __name__ == "__main__":
    main()
