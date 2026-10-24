"""Provision pinned vcpkg and Slang under .tools without changing system settings."""

import hashlib
from pathlib import Path
import platform
import subprocess
import tarfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / ".tools"
VCPKG = "0c2a9ace1964b818b15f7ae50ba1c42404918a7b"
SLANG = "2026.17"
ARCHIVES = {
    "Windows": ("windows-x86_64.zip", "bc8cf08b24aaf44d98f06b7d578d0557a21bfed1ce6bfb9765a0f0d21dec8f31"),
    "Linux": ("linux-x86_64-glibc-2.28.tar.gz", "a5a48530e7218d79e10b633c216ef04cbe778450b8c0a7579125e630c088ca75"),
}


def run(*command):
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    system = platform.system()
    if system not in ARCHIVES or platform.machine().lower() not in ("amd64", "x86_64"):
        raise SystemExit("bootstrap supports Windows/Linux x86-64")
    TOOLS.mkdir(exist_ok=True)
    checkout = TOOLS / "vcpkg"
    if not checkout.exists():
        run("git", "clone", "--filter=blob:none", "https://github.com/microsoft/vcpkg.git", str(checkout))
    run("git", "-C", str(checkout), "fetch", "origin", VCPKG)
    run("git", "-C", str(checkout), "checkout", "--detach", VCPKG)
    if system == "Windows":
        run("cmd", "/c", str(checkout / "bootstrap-vcpkg.bat"), "-disableMetrics")
    else:
        run("sh", str(checkout / "bootstrap-vcpkg.sh"), "-disableMetrics")
    suffix, expected_hash = ARCHIVES[system]
    archive = TOOLS / f"slang-{SLANG}-{suffix}"
    if not archive.exists():
        url = f"https://github.com/shader-slang/slang/releases/download/v{SLANG}/{archive.name}"
        with urllib.request.urlopen(url, timeout=60) as response, archive.open("wb") as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected_hash:
        raise SystemExit("Slang archive checksum mismatch; remove the archive under .tools and retry")
    destination = TOOLS / f"slang-{system}"
    destination.mkdir(exist_ok=True)
    if system == "Windows":
        with zipfile.ZipFile(archive) as source:
            source.extractall(destination)
    else:
        with tarfile.open(archive) as source:
            source.extractall(destination, filter="data")
    print(f"Provisioned vcpkg {VCPKG}, Slang {SLANG}")


if __name__ == "__main__":
    main()
