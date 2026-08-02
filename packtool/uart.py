# Scan 01 — packtool: the k5prog UART protocol
"""The base's PC-UART protocol (app/uart.c + driver/crc.c + driver/aes.c):
0x0514 identify (obfuscated) -> challenge; 0x052D authenticate (AES-128-ECB
of the challenge with the default key); 0x051B/0x051D EEPROM read/write.
Frames: AB CD [size LE] [body XOR-obfuscated] [CRC16 LE] DC BA. After the
0x0514 handshake the link runs PLAINTEXT (the firmware flips its flag), so
only the identify frame is obfuscated. Pure framing here — the Radio class
adds the pyserial transport (dump/flash only).
"""

from __future__ import annotations

import struct

OBFUSCATION = bytes([
    0x16, 0x6C, 0x14, 0xE6, 0x2E, 0x91, 0x0D, 0x40,
    0x21, 0x35, 0xD5, 0x40, 0x13, 0x03, 0xE9, 0x80,
])

# gDefaultAesKey, driver/aes.c: {0x4AA5CC60, 0x0312CC5F, 0xFFD2DABB, 0x6BBA7F92}
DEFAULT_AES_KEY = bytes.fromhex("60cc a54a 5fcc 1203 bbda d2ff 927f ba6b".replace(" ", ""))

ID_IDENTIFY = 0x0514
ID_VERSION_REPLY = 0x0515
ID_AUTH = 0x052D
ID_AUTH_REPLY = 0x052E
ID_READ = 0x051B
ID_READ_REPLY = 0x051C
ID_WRITE = 0x051D
ID_WRITE_REPLY = 0x051E


def crc16(data: bytes) -> int:
    """The base's CRC_Calculate: CRC-16/CCITT, init 0, no reflection
    (dp32g030 CRC peripheral, CRC_SEL_CRC_16_CCITT, IV = 0)."""
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _xor(data: bytes, start: int = 0) -> bytes:
    return bytes(b ^ OBFUSCATION[(start + i) % 16] for i, b in enumerate(data))


def build_frame(cmd_id: int, payload: bytes, obfuscate: bool) -> bytes:
    """AB CD [size] [obfuscated body] [crc] DC BA. size counts the body
    (Header + payload); the CRC sits inside the obfuscated span (the
    firmware deobfuscates Size+2 bytes)."""
    body = struct.pack("<HH", cmd_id, len(payload) + 4) + payload
    frame_body = body + struct.pack("<H", crc16(body))
    if obfuscate:
        # the deobfuscation covers Size+2 bytes: the struct AND the CRC
        frame_body = _xor(frame_body, 0)
    return b"\xAB\xCD" + struct.pack("<H", len(body)) + frame_body + b"\xDC\xBA"


def parse_reply(data: bytes) -> tuple[int, bytes]:
    """Parse a radio reply: [CD AB][Size LE][obfuscated struct][pad 2][DC BA].
    The session is obfuscated throughout (0x0514 arrives obfuscated, so the
    firmware's raw-ID check never flips its flag — the CHIRP mode)."""
    if len(data) < 8:
        raise ValueError("short reply")
    if data[:2] != b"\xCD\xAB":
        raise ValueError(f"bad reply header {data[:2].hex()}")
    size = struct.unpack_from("<H", data, 2)[0]
    if data[-2:] != b"\xDC\xBA":
        raise ValueError(f"bad footer {data[-2:].hex()}")
    struct_bytes = _xor(data[4:4 + size], 0)
    r_id, r_size = struct.unpack_from("<HH", struct_bytes)
    if len(struct_bytes) < r_size:
        raise ValueError(f"short reply body: {len(struct_bytes)} < {r_size}")
    return r_id, struct_bytes[4:r_size]


# ---- AES-128 (ECB, one block) — mirrors driver/aes.c's transform ----

_SBOX = [
    0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
    0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
    0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
    0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
    0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
    0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
    0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
    0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
    0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
    0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
    0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
    0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
    0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
    0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
    0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
    0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16,
]

_RCON = [0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36]


def _key_expand(key: bytes) -> list[bytes]:
    words = [key[4 * i:4 * i + 4] for i in range(4)]
    for i in range(4, 44):
        t = words[i - 1]
        if i % 4 == 0:
            t = bytes(_SBOX[b] for b in t[1:] + t[:1])
            t = bytes([t[0] ^ _RCON[i // 4 - 1]]) + t[1:]
        words.append(bytes(a ^ b for a, b in zip(words[i - 4], t)))
    return words


def aes_ecb_block(key: bytes, block: bytes) -> bytes:
    state = list(block)
    words = _key_expand(key)
    rk = b"".join(words)                    # 176 bytes: 11 round keys of 16

    def add_round(r: int):
        nonlocal state
        k = rk[r * 16:(r + 1) * 16]
        state = [s ^ k[i] for i, s in enumerate(state)]

    def sub_shift():
        # SubBytes, then ShiftRows: row r moves to column (c - r) mod 4
        nonlocal state
        state = [_SBOX[b] for b in state]
        shifted = [0] * 16
        for c in range(4):
            for r in range(4):
                shifted[c * 4 + r] = state[((c + r) % 4) * 4 + r]
        state = shifted

    def mix():
        nonlocal state
        for c in range(4):
            a = state[c * 4:(c + 1) * 4]
            state[c * 4:(c + 1) * 4] = [
                _gmul(a[0], 2) ^ _gmul(a[1], 3) ^ a[2] ^ a[3],
                a[0] ^ _gmul(a[1], 2) ^ _gmul(a[2], 3) ^ a[3],
                a[0] ^ a[1] ^ _gmul(a[2], 2) ^ _gmul(a[3], 3),
                _gmul(a[0], 3) ^ a[1] ^ a[2] ^ _gmul(a[3], 2),
            ]

    add_round(0)
    for r in range(1, 10):
        sub_shift()
        mix()
        add_round(r)
    sub_shift()
    add_round(10)
    return bytes(state)


def _gmul(a: int, b: int) -> int:
    p = 0
    for _ in range(8):
        if b & 1:
            p ^= a
        hi = a & 0x80
        a = (a << 1) & 0xFF
        if hi:
            a ^= 0x1B
        b >>= 1
    return p


def aes_response(challenge: bytes) -> bytes:
    return aes_ecb_block(DEFAULT_AES_KEY, challenge)


# ---- the Radio transport (pyserial; dump/flash only) ----

class Radio:
    def __init__(self, port: str, baud: int = 9600, timeout: float = 2.0):
        import serial  # lazy: validate/build/diff never need the hardware
        self.ser = serial.Serial(port, baud, timeout=timeout)
        self.timestamp = 0x12345678

    def close(self):
        self.ser.close()

    def _command(self, cmd_id: int, payload: bytes,
                 reply_id: int, reply_size: int) -> bytes:
        self.ser.write(build_frame(cmd_id, payload, obfuscate=True))
        head = self.ser.read(4)                  # the CD AB reply header (plaintext)
        if len(head) != 4:
            raise OSError("no reply header")
        if head[:2] != b"\xCD\xAB":
            raise OSError(f"bad reply header {head[:2].hex()}")
        size = struct.unpack_from("<H", head, 2)[0]
        data = self.ser.read(size + 4)           # struct + footer
        r_id, body = parse_reply(head + data)
        if r_id != reply_id:
            raise OSError(f"reply id {r_id:#x}, wanted {reply_id:#x}")
        if len(body) < reply_size:
            raise OSError("short reply data")
        return body

    def identify(self) -> bytes:
        """0x0514 (obfuscated) -> version reply with the challenge."""
        body = self._command(ID_IDENTIFY, struct.pack("<I", self.timestamp),
                             reply_id=ID_VERSION_REPLY, reply_size=36)
        return body[20:36]                       # Challenge[4], after Version[16]+flags[4]

    def authenticate(self, challenge: bytes) -> bool:
        body = self._command(ID_AUTH, aes_response(challenge),
                             reply_id=ID_AUTH_REPLY, reply_size=4)
        return body[0] == 0

    def read_eeprom(self, addr: int, size: int) -> bytes:
        if size > 128:
            raise ValueError("read_eeprom max 128 bytes")
        payload = struct.pack("<HBB", addr, size, 0) + struct.pack("<I", self.timestamp)
        body = self._command(ID_READ, payload,
                             reply_id=ID_READ_REPLY, reply_size=size + 4)
        return body[4:4 + size]          # skip the reply's Offset/Size header

    def write_eeprom(self, addr: int, data: bytes) -> None:
        if len(data) > 128:
            raise ValueError("write_eeprom max 128 bytes")
        payload = struct.pack("<HBB", addr, len(data), 0) + struct.pack("<I", self.timestamp) + data
        self._command(ID_WRITE, payload,
                      reply_id=ID_WRITE_REPLY, reply_size=2)

    def read_full(self) -> bytes:
        out = bytearray()
        for addr in range(0, 0x2000, 128):
            out += self.read_eeprom(addr, 128)
        return bytes(out)

    def write_regions(self, regions) -> None:
        for region in regions:
            for off in range(0, len(region.data), 8):
                chunk = region.data[off:off + 8].ljust(8, b"\xFF")
                self.write_eeprom(region.addr + off, chunk)
