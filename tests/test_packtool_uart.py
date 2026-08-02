# Scan 01 — packtool: the k5prog UART protocol (framing + a fake radio)
import struct

from packtool import uart
from packtool.uart import (ID_AUTH, ID_AUTH_REPLY, ID_IDENTIFY, ID_READ,
                           ID_READ_REPLY, ID_WRITE, ID_WRITE_REPLY, Radio,
                           aes_ecb_block, build_frame, crc16, parse_reply)


def test_k5prog_crc():
    assert crc16(b"123456789") == 0x31C3          # CCITT-FALSE, init 0


def test_aes_fips197_vector():
    key = bytes.fromhex("000102030405060708090a0b0c0d0e0f")
    block = bytes.fromhex("00112233445566778899aabbccddeeff")
    assert aes_ecb_block(key, block).hex() == "69c4e0d86a7b0430d8cdb78070b4c55a"


def test_frame_roundtrip():
    frame = build_frame(ID_READ, struct.pack("<HBBI", 0x1BD0, 0x40, 0, 0x12345678),
                        obfuscate=True)
    assert frame[:2] == b"\xAB\xCD"
    assert frame[-2:] == b"\xDC\xBA"
    size = struct.unpack_from("<H", frame, 2)[0]
    body = frame[4:4 + size]
    crc = struct.unpack_from("<H", frame, 4 + size)[0]
    plain = bytes(b ^ uart.OBFUSCATION[i % 16]
                  for i, b in enumerate(body + crc.to_bytes(2, "little")))
    assert crc16(plain[:size]) == struct.unpack("<H", plain[size:])[0]
    cmd_id = struct.unpack_from("<H", plain, 0)[0]
    assert cmd_id == ID_READ
    payload = struct.pack("<I", 0x12345678)
    obf = build_frame(ID_IDENTIFY, payload, obfuscate=True)
    obf_body = obf[4:4 + struct.unpack_from("<H", obf, 2)[0]]
    plain = bytes(b ^ uart.OBFUSCATION[i % 16] for i, b in enumerate(obf_body[:8]))
    assert struct.unpack_from("<H", plain, 0)[0] == ID_IDENTIFY
    assert plain[4:8] == payload


def test_parse_reply():
    body = struct.pack("<HH", ID_READ_REPLY, 8) + b"\x01\x02\x03\x04"
    body = bytes(b ^ uart.OBFUSCATION[i % 16] for i, b in enumerate(body))
    frame = b"\xCD\xAB" + struct.pack("<H", len(body)) + body + b"\x00\x00\xDC\xBA"
    r_id, data = parse_reply(frame)
    assert r_id == ID_READ_REPLY and data == b"\x01\x02\x03\x04"


# ---- a fake radio implementing the firmware's server side ----

class FakeRadio:
    """Speaks the firmware's protocol: the session is obfuscated throughout
    (0x0514 arrives obfuscated, so the firmware's raw-ID check never flips
    its flag); replies carry the CD AB + size host header; AES-ECB auth;
    EEPROM read/write into a bytearray."""

    def __init__(self, image: bytearray):
        self.image = image
        self.challenge = bytes.fromhex("00112233445566778899aabbccddeeff")
        self.buf = b""

    def feed(self, data: bytes) -> bytes:
        self.buf += data
        out = b""
        while len(self.buf) >= 4 and self.buf[:2] == b"\xAB\xCD":
            size = struct.unpack_from("<H", self.buf, 2)[0]
            need = 4 + size + 2 + 2
            if len(self.buf) < need:
                break
            frame, self.buf = self.buf[:need], self.buf[need:]
            assert frame[-2:] == b"\xDC\xBA"
            body = frame[4:4 + size]
            crc = struct.unpack_from("<H", frame, 4 + size)[0]
            # the deobfuscation covers the struct AND the CRC (Size+2)
            plain = bytes(b ^ uart.OBFUSCATION[i % 16]
                          for i, b in enumerate(body + crc.to_bytes(2, "little")))
            body, crc = plain[:size], struct.unpack("<H", plain[size:])[0]
            assert crc16(body) == crc
            cmd_id = struct.unpack_from("<H", body, 0)[0]
            payload = body[4:]
            out += self._handle(cmd_id, payload)
        return out

    def _reply(self, r_id: int, data: bytes) -> bytes:
        body = struct.pack("<HH", r_id, len(data) + 4) + data
        body = bytes(b ^ uart.OBFUSCATION[i % 16] for i, b in enumerate(body))
        # the firmware's footer is 4 bytes: 2 obfuscation-derived pad + DC BA
        size = len(body)
        pad = bytes([uart.OBFUSCATION[size % 16] ^ 0xFF,
                     uart.OBFUSCATION[(size + 1) % 16] ^ 0xFF])
        return b"\xCD\xAB" + struct.pack("<H", size) + body + pad + b"\xDC\xBA"

    def _handle(self, cmd_id: int, payload: bytes) -> bytes:
        if cmd_id == ID_IDENTIFY:
            ver = b"UV-K6".ljust(16, b"\x00")          # Version[16]
            flags = b"\x00\x00\x00\x00"
            return self._reply(0x0515, ver + flags + self.challenge)
        if cmd_id == ID_AUTH:
            ok = 0 if payload[:16] == uart.aes_response(self.challenge) else 1
            return self._reply(ID_AUTH_REPLY, bytes([ok]) + b"\x00\x00\x00")
        if cmd_id == ID_READ:
            off, size = struct.unpack_from("<HB", payload, 0)
            return self._reply(ID_READ_REPLY, struct.pack("<HBB", off, size, 0)
                               + self.image[off:off + size])
        if cmd_id == ID_WRITE:
            off, size = struct.unpack_from("<HB", payload, 0)
            data = payload[8:8 + size]
            self.image[off:off + size] = data
            return self._reply(ID_WRITE_REPLY, struct.pack("<H", off))
        return b""


class FakeSerial:
    """A serial shim feeding the Radio client through the FakeRadio."""

    def __init__(self, radio: FakeRadio):
        self.radio = radio
        self.pending = b""

    def write(self, data: bytes):
        self.pending += self.radio.feed(data)

    def read(self, n: int) -> bytes:
        if not self.pending:
            raise OSError("nothing to read")
        out, self.pending = self.pending[:n], self.pending[n:]
        return out


def test_radio_flow():
    image = bytearray(0x2000)
    image[0x1BD0:0x1BD0 + 4] = b"SC01"
    radio = FakeRadio(image)
    client = Radio.__new__(Radio)               # bypass __init__ (no real port)
    client.ser = FakeSerial(radio)
    client.timestamp = 0x12345678

    challenge = client.identify()
    assert challenge == radio.challenge
    assert client.authenticate(challenge)

    client.write_eeprom(0x1BD0 + 0x30, bytes(range(8)))
    assert image[0x1BD0 + 0x30:0x1BD0 + 0x38] == bytes(range(8))
    data = client.read_eeprom(0x1BD0, 0x40)
    assert data[0:4] == b"SC01" and data[0x30:0x38] == bytes(range(8))


def test_build_frame_is_obfuscated():
    f = build_frame(ID_IDENTIFY, b"\x00" * 4, obfuscate=True)
    body = f[4:4 + struct.unpack_from("<H", f, 2)[0]]
    assert body[:2] != b"\x05\x14"               # obfuscated
    p = build_frame(ID_READ, b"\x00" * 8, obfuscate=True)
    pbody = p[4:4 + struct.unpack_from("<H", p, 2)[0]]
    assert struct.unpack_from("<H", pbody, 0)[0] != ID_READ  # obfuscated too
