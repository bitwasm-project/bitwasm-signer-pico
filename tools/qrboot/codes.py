"""QR codes for the QR boot loader, as PNG files.

  python3 tools/qrboot/codes.py pack app.wasm outdir [--version 40]   the app as printable BBQr codes
  python3 tools/qrboot/codes.py text lines.txt outdir                 one code per line (test frames)

pack prints the SHA-256 the loader shows before it runs the app, and writes sheet.html for printing.
"""
import argparse
import base64
import hashlib
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "third_party/QR-Code-generator/python"))
from qrcodegen import QrCode, QrSegment  # noqa: E402

B36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"
SCALE, BORDER = 3, 4


def png(qr, path):
    size = (qr.get_size() + 2 * BORDER) * SCALE
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        rows += bytes(0 if qr.get_module(x // SCALE - BORDER, y // SCALE - BORDER) else 255 for x in range(size))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b""))


def capacity(version):
    """Base32 characters per BBQr part at ECC L, after its 8-character header, a multiple of 8"""
    bits = QrCode._get_num_data_codewords(version, QrCode.Ecc.LOW) * 8 - 4 - (9 if version < 10 else 11 if version < 27 else 13)
    return (bits // 11 * 2 + (1 if bits % 11 >= 6 else 0) - 8) // 8 * 8


def pack(a):
    wasm = Path(a.input).read_bytes()
    if wasm[:4] != b"\0asm":
        sys.exit("not a wasm module")
    digest = hashlib.sha256(wasm).hexdigest()
    # BBQr's Z encoding: zlib with wbits 10 and no header, so a receiver needs only a 1 KiB window
    c = zlib.compressobj(9, zlib.DEFLATED, -10)
    text = base64.b32encode(c.compress(wasm) + c.flush()).decode().rstrip("=")
    per = capacity(a.version)
    bodies = [text[i : i + per] for i in range(0, len(text), per)]
    if len(bodies) > 1295:
        sys.exit("more than 1295 codes")
    b36 = lambda n: B36[n // 36] + B36[n % 36]
    lines = [f"B$ZB{b36(len(bodies))}{b36(i)}{b}" for i, b in enumerate(bodies)]  # B: binary, the spec's letter for trials
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    for i, line in enumerate(lines):
        png(QrCode.encode_segments([QrSegment.make_alphanumeric(line)], QrCode.Ecc.LOW, a.version, a.version, boostecl=False), out / f"qr-{i:02d}.png")
    groups = " ".join(digest[i : i + 4] for i in range(0, 64, 4))
    mm = (a.version * 4 + 17 + 2 * BORDER) * 0.4  # 0.4 mm modules
    figs = "".join(f'<figure><img src="qr-{i:02d}.png"><figcaption>{i + 1} / {len(lines)}</figcaption></figure>' for i in range(len(lines)))
    (out / "sheet.html").write_text(
        f'<!doctype html><meta charset="utf-8"><title>qrboot app</title><style>body{{font:12px ui-monospace,monospace;margin:10mm}}'
        f"main{{display:flex;flex-wrap:wrap;gap:6mm}}figure{{margin:0;text-align:center}}img{{width:{mm:.1f}mm;image-rendering:pixelated}}</style>"
        f"<p>qrboot app, {len(wasm)} bytes, {len(lines)} codes at version {a.version}. SHA-256:</p><p><b>{groups}</b></p><main>{figs}</main>\n"
    )
    print(f"app      {len(wasm)} bytes, {len(text) * 5 // 8} compressed")
    print(f"codes    {len(lines)} at version {a.version}")
    print(f"sha256   {groups}")


def text(a):
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    for i, line in enumerate(Path(a.input).read_text().split()):
        png(QrCode.encode_segments(QrSegment.make_segments(line), QrCode.Ecc.LOW, boostecl=False), out / f"{i:02d}.png")


ap = argparse.ArgumentParser()
ap.add_argument("mode", choices=["pack", "text"])
ap.add_argument("input")
ap.add_argument("outdir")
ap.add_argument("--version", type=int, default=40)
args = ap.parse_args()
pack(args) if args.mode == "pack" else text(args)
