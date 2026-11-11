#!/usr/bin/env python3
"""Exercise both directions against the actual protocol-1 sources from Git."""
import argparse
import io
from pathlib import Path
import shutil
import socket
import subprocess
import tarfile
import tempfile


def match(server, first, second, config=None):
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = str(reservation.getsockname()[1])
    command = [str(server), "server", port]
    if config:
        command.append(str(config))
    host = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    clients = []
    try:
        if not host.stdout.readline().startswith("READY"):
            raise RuntimeError(host.stderr.read())
        for binary in (first, second):
            peer = subprocess.Popen([str(binary), "client", port], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            clients.append(peer)
            if not peer.stdout.readline().startswith("WELCOME"):
                raise RuntimeError(peer.stderr.read())
        for peer in clients:
            output, errors = peer.communicate(timeout=10)
            if peer.returncode or "COMPAT OK" not in output:
                raise RuntimeError(output + errors)
            print(output.strip(), flush=True)
    finally:
        for peer in clients:
            if peer.poll() is None:
                peer.terminate()
            peer.wait(timeout=3)
        if host.poll() is None:
            host.terminate()
        host.wait(timeout=3)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ref", default="6132487")
    parser.add_argument("--build", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    build = (args.build or project / "build").resolve()
    sfml = build / "_deps/sfml-src/include"
    libraries = build / "_deps/sfml-build/lib"
    current = build / "compatibility_peer"
    if not current.exists() or not sfml.exists():
        raise RuntimeError("Build compatibility_peer with the default FetchContent build first")
    archive = subprocess.check_output(["git", "archive", args.ref, "Tanks"], cwd=project.parent)
    with tempfile.TemporaryDirectory(prefix="tankwars-legacy-") as temporary:
        root = Path(temporary)
        with tarfile.open(fileobj=io.BytesIO(archive)) as bundle:
            bundle.extractall(root, filter="data")
        legacy = root / "Tanks"
        binary = root / "legacy_peer"
        sources = [legacy / "src" / name for name in ("Config.cpp", "Map.cpp", "Physics.cpp", "World.cpp", "GameSession.cpp", "Network.cpp")]
        subprocess.run([
            shutil.which("c++") or "c++", "-std=c++23", "-O2", "-DSFML_STATIC",
            "-I" + str(legacy / "include"), "-I" + str(sfml),
            *map(str, sources), str(project / "tests/CompatibilityPeer.cpp"),
            str(libraries / "libsfml-network-s.a"), str(libraries / "libsfml-system-s.a"),
            "-pthread", "-o", str(binary),
        ], check=True)
        print("New host + legacy client + new client", flush=True)
        match(current, binary, current, project / "config/game.cfg")
        print("Legacy host + new client + legacy client", flush=True)
        match(binary, current, binary)
        print("Both protocol-1 compatibility directions passed", flush=True)


if __name__ == "__main__":
    main()
