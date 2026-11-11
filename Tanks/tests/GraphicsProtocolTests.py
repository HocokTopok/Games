"""Projectile visuals must survive a Hello with absent or partial capabilities."""
import select
import socket
import struct
import subprocess
import sys
import time
import tempfile
from pathlib import Path


class Reader:
    def __init__(self, data):
        self.data, self.offset = data, 0

    def read(self, fmt):
        value = struct.unpack_from(fmt, self.data, self.offset)
        self.offset += struct.calcsize(fmt)
        return value[0] if len(value) == 1 else value

    def skip(self, size):
        self.offset += size

    def string(self):
        self.skip(self.read(">I"))


def state(data):
    r = Reader(data)
    assert r.read("B") == 2
    tick, round_number = r.read(">II")
    phase = r.read("B")
    r.skip(5)  # winner, elapsed
    for _ in range(r.read("B")):
        r.skip(6)
        r.string()
    tank_count = r.read("B")
    r.skip(tank_count * 39)
    projectile_count = r.read(">H")
    owners = []
    for _ in range(projectile_count):
        r.skip(4)
        owners.append(r.read("B"))
        r.skip(20)
    event_count = r.read(">H")
    r.skip(event_count * 14)
    assert r.read(">I") == 0x54575831 and r.read(">H") == 2
    assert r.read("B") == tank_count
    r.skip(tank_count * 2)
    r.skip(r.read(">H") * 21)
    r.skip(tank_count * 10 + projectile_count)
    r.skip(r.read(">H") * 21 + event_count)
    detailed, projectiles, events = False, [], []
    while r.offset < len(data):
        marker, version = r.read(">IH")
        assert version == 1
        if marker == 0x48505731:
            assert r.read("B") == tank_count
            r.skip(tank_count * 5)
        elif marker == 0x41525731:
            detailed = True
            weapons = [r.read("B") for _ in range(tank_count)]
            if phase == 1:
                assert weapons[:2] == [4, 3], weapons
            projectiles = [r.read("BB") for _ in range(projectile_count)]
            events = [(r.read("B"), r.read("<f"), r.read("<f")) for _ in range(event_count)]
        elif marker == 0x42465731:
            break  # Battlefield extension is exercised by battlefield_network.
        else:
            raise AssertionError(f"Unknown graphics block {marker:x}")
    assert detailed, "Server omitted projectile graphics because Hello lacked the weapon capability"
    for owner, (kind, bounces) in zip(owners, projectiles):
        assert kind == (4 if owner == 0 else 3) and bounces <= 2, (owner, kind, bounces)
    return phase, projectiles, events


def send(peer, payload):
    peer.sendall(struct.pack(">I", len(payload)) + payload)


def main():
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    config_file = tempfile.NamedTemporaryFile(mode="w", suffix=".cfg")
    config_file.write(Path(sys.argv[2]).read_text().replace("loadout_selection = 1", "loadout_selection = 0"))
    config_file.flush()
    server = subprocess.Popen([sys.argv[1], "server", str(port), config_file.name], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    peers = []
    try:
        assert server.stdout.readline().startswith(b"READY"), "Test server failed to start"
        for capabilities in (None, 1):
            peer = socket.create_connection(("127.0.0.1", port), 2)
            peers.append(peer)
            name = b"GRAPHICS"
            hello = b"\x00" + struct.pack(">III", 0x54414E4B, 1, len(name)) + name
            if capabilities is not None:
                hello += struct.pack(">II", 0x54574331, capabilities)
            send(peer, hello)
            send(peer, b"\x04\x01")
        buffers = [bytearray(), bytearray()]
        seen = [set(), set()]
        deadline = time.monotonic() + 6.5
        while time.monotonic() < deadline:
            for i, peer in enumerate(peers):
                send(peer, b"\x03" + struct.pack("<fffBf", 0, -0.12 if i == 0 else -2.5, 0, 1, 0))
            ready, _, _ = select.select(peers, [], [], 0.01)
            for peer in ready:
                i = peers.index(peer)
                chunk = peer.recv(65536)
                assert chunk, "Client disconnected during graphics test"
                buffers[i].extend(chunk)
                while len(buffers[i]) >= 4:
                    size = struct.unpack_from(">I", buffers[i])[0]
                    if len(buffers[i]) < size + 4:
                        break
                    payload = bytes(buffers[i][4:size + 4])
                    del buffers[i][:size + 4]
                    if payload[0] != 2:
                        continue
                    phase, projectiles, events = state(payload)
                    seen[i].update(kind for kind, _ in projectiles)
                    if any(kind == 4 and radius == 68 for kind, radius, _ in events):
                        seen[i].add("blast")
                    if {3, 4, "blast"} <= seen[0] and {3, 4, "blast"} <= seen[1]:
                        print("Graphics protocol passed: full orb, mortar and explosion data with absent and partial capabilities")
                        return
        raise AssertionError(f"Missing typed graphics: {seen}")
    finally:
        for peer in peers:
            peer.close()
        server.terminate()
        server.wait(timeout=3)
        config_file.close()


if __name__ == "__main__":
    main()
