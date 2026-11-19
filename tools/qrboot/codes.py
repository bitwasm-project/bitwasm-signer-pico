"""QR codes for the QR boot loader, as PNG files.

  python3 tools/qrboot/codes.py pack app.wasm outdir [--version 40]   the app as printable BBQr codes
  python3 tools/qrboot/codes.py text lines.txt outdir                 one code per line (test frames)

pack prints the SHA-256 the loader shows before it runs the app, and writes sheet.html: an A4 sheet for
an heir, with the steps, the hash and the codes inline. It holds no secret; the key stays on its SeedQR plate.
It also writes anim.html, the same app as an animated QR to show on a screen (--anim-version).
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


def png(qr):
    size = (qr.get_size() + 2 * BORDER) * SCALE
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        rows += bytes(0 if qr.get_module(x // SCALE - BORDER, y // SCALE - BORDER) else 255 for x in range(size))
    chunk = lambda t, d: struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d))
    ihdr = struct.pack(">IIBBBBB", size, size, 8, 0, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) + chunk(b"IDAT", zlib.compress(bytes(rows), 9)) + chunk(b"IEND", b"")


def capacity(version):
    """Base32 characters per BBQr part at ECC L, after its 8-character header, a multiple of 8"""
    bits = QrCode._get_num_data_codewords(version, QrCode.Ecc.LOW) * 8 - 4 - (9 if version < 10 else 11 if version < 27 else 13)
    return (bits // 11 * 2 + (1 if bits % 11 >= 6 else 0) - 8) // 8 * 8


def codes(text, version):
    per = capacity(version)
    bodies = [text[i : i + per] for i in range(0, len(text), per)]
    if len(bodies) > 1295:
        sys.exit("more than 1295 codes")
    b36 = lambda n: B36[n // 36] + B36[n % 36]
    lines = [f"B$ZB{b36(len(bodies))}{b36(i)}{b}" for i, b in enumerate(bodies)]  # B: binary, the spec's letter for trials
    return [QrCode.encode_segments([QrSegment.make_alphanumeric(l)], QrCode.Ecc.LOW, version, version, boostecl=False) for l in lines]


def pack(a):
    wasm = Path(a.input).read_bytes()
    if wasm[:4] != b"\0asm":
        sys.exit("not a wasm module")
    digest = hashlib.sha256(wasm).hexdigest()
    # BBQr's Z encoding: zlib with wbits 10 and no header, so a receiver needs only a 1 KiB window
    c = zlib.compressobj(9, zlib.DEFLATED, -10)
    text = base64.b32encode(c.compress(wasm) + c.flush()).decode().rstrip("=")
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    printed = [png(qr) for qr in codes(text, a.version)]
    if a.pngs:
        for i, p in enumerate(printed):
            (out / f"qr-{i:02d}.png").write_bytes(p)
    groups = " ".join(digest[i : i + 4] for i in range(0, 64, 4))
    (out / "sheet.html").write_text(sheet(a, len(wasm), printed, groups))
    shown = codes(text, a.anim_version)
    (out / "anim.html").write_text(anim(a, shown, groups))
    print(f"app      {len(wasm)} bytes, {len(text) * 5 // 8} compressed")
    print(f"codes    {len(printed)} at version {a.version}, {len(shown)} animated at version {a.anim_version}")
    print(f"sha256   {groups}")


TEXT = {
    "ja": {
        "title": "ビットコイン相続用の署名器",
        "nosecret": "この紙に秘密は含まれていません。鍵は別に保管した金属板の SeedQR です。この紙は何部複製しても構いません。",
        "need": "用意するもの：スマートフォン（ブラウザ）、金属板の SeedQR、送金を作るウォレットアプリ（Sparrow など）",
        "loader": "ローダー",
        "hash": "署名器の SHA-256（手順 3 で画面の表示と照合）",
        "steps": [
            "ネットに接続した状態で上のローダーを一度開き、その後スマートフォンを機内モードにします。一度開けば以後はオフラインで動きます。",
            "「Start camera」を押し、この紙の QR をすべて読みます。順番は問いません。",
            "表示された SHA-256 が上の値と一致することを確かめてから「run the app」を押します。一致しなければ使わないでください。",
            "「Scan SeedQR」で金属板を読みます。表示された Fingerprint が下の欄と一致することを確かめます。",
            "初回だけ「Set up watch-only wallet」で表示された QR をウォレットアプリに読ませます。",
            "ウォレットアプリで送金を作り、その QR を「Scan PSBT」で読みます。",
            "送金先と金額を確かめて署名し、表示された QR をウォレットアプリに読ませて送信します。",
            "終わったらブラウザのタブを閉じます。鍵はその時点で消えます。",
        ],
        "fields": ["Fingerprint", "作成日", "メモ"],
        "codes": "署名器の QR",
        "codeinfo": "{n} 枚、各 Version {v}。順不同で読み取れます。",
        "anim": "署名器のアニメーション QR",
        "animinfo": "ローダーの「Start camera」で読みます。{n} コマを繰り返し表示します。読み落としは次の周で拾えます。",
    },
    "en": {
        "title": "Bitcoin signer for inheritance",
        "nosecret": "This sheet holds no secret. The key is the SeedQR on a metal plate kept elsewhere. Copy this sheet as often as you like.",
        "need": "You need: a phone with a browser, the SeedQR plate, and a wallet app that makes the transaction (Sparrow or similar)",
        "loader": "Loader",
        "hash": "SHA-256 of the signer (compare with the screen in step 3)",
        "steps": [
            "Open the loader above once while online, then put the phone in airplane mode. Once opened it works offline.",
            "Press Start camera and read every code on this sheet, in any order.",
            "Check the SHA-256 on screen matches the one above, then press run the app. If it does not match, do not use it.",
            "Choose Scan SeedQR and read the metal plate. Check the fingerprint shown matches the one below.",
            "The first time, show the QR from Set up watch-only wallet to the wallet app.",
            "Make the transaction in the wallet app and read its QR with Scan PSBT.",
            "Check the recipient and amount, sign, and show the QR on screen to the wallet app to send it.",
            "Close the browser tab when done. The key is gone from that moment.",
        ],
        "fields": ["Fingerprint", "Made on", "Notes"],
        "codes": "Signer codes",
        "codeinfo": "{n} codes at version {v}. Read them in any order.",
        "anim": "Signer as an animated QR",
        "animinfo": "Read it with Start camera in the loader. The {n} frames repeat; a missed one comes round again.",
    },
}


def anim(a, qrs, groups):
    t, side = TEXT[a.lang], qrs[0].get_size()
    # Each frame's modules row by row, one bit each, so the page carries a few KiB a frame and no image
    frames = []
    for qr in qrs:
        bits = [qr.get_module(i % side, i // side) for i in range(side * side)]
        frames.append(base64.b64encode(bytes(sum(b << (7 - j) for j, b in enumerate(bits[k : k + 8])) for k in range(0, len(bits), 8))).decode())
    return f"""<!doctype html><html lang="{a.lang}"><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1"><title>{t["anim"]}</title>
<style>
body {{ margin: 0; background: #fff; color: #000; font: 14px/1.5 "Hiragino Sans", system-ui, sans-serif; display: flex; flex-direction: column; align-items: center; gap: 10px; padding: 12px; min-height: 100vh; box-sizing: border-box }}
h1 {{ font-size: 16px; margin: 0 }} p {{ margin: 0; text-align: center; max-width: 640px }}
.mono {{ font-family: ui-monospace, Menlo, monospace }}
canvas {{ width: min(92vw, calc(100vh - 190px)); aspect-ratio: 1; image-rendering: pixelated }}
.row {{ display: flex; gap: 8px; align-items: center }}
button {{ font: inherit; padding: 6px 12px }} button.on {{ font-weight: 700; outline: 2px solid #000 }}
</style>
<h1>{t["anim"]}</h1>
<p>{t["animinfo"].format(n=len(qrs))}</p>
<p class=mono>SHA-256 {groups}</p>
<canvas id=c width={side + 8} height={side + 8}></canvas>
<div class=row><span id=n class=mono></span><button id=pause>||</button><span>fps</span><button>2</button><button class=on>4</button><button>8</button></div>
<script>
const F = {frames!r}, S = {side}, c = document.getElementById("c"), x = c.getContext("2d");
let i = 0, fps = 4, timer = 0;
function draw() {{
  const b = Uint8Array.from(atob(F[i]), (ch) => ch.charCodeAt(0));
  x.fillStyle = "#fff", x.fillRect(0, 0, S + 8, S + 8), x.fillStyle = "#000";
  for (let k = 0; k < S * S; k++) if (b[k >> 3] & (128 >> (k & 7))) x.fillRect(4 + (k % S), 4 + ((k / S) | 0), 1, 1);
  document.getElementById("n").textContent = `${{i + 1}} / ${{F.length}}`;
  i = (i + 1) % F.length;
}}
function play() {{ clearInterval(timer), (timer = setInterval(draw, 1000 / fps)); }}
document.querySelectorAll("button:not(#pause)").forEach((btn) => (btn.onclick = () => {{
  document.querySelector("button.on").classList.remove("on"), btn.classList.add("on"), (fps = +btn.textContent), play();
}}));
document.getElementById("pause").onclick = () => (timer ? (clearInterval(timer), (timer = 0)) : play());
draw(), play();
</script>
</html>
"""


def sheet(a, size, pngs, groups):
    t, n = TEXT[a.lang], len(pngs)
    mm = (a.version * 4 + 17 + 2 * BORDER) * a.module  # one code's printed width
    steps = "".join(f"<li>{s}</li>" for s in t["steps"])
    fields = "".join(f"<div class=field><span>{f}</span></div>" for f in t["fields"])
    figs = "".join(f'<figure><img src="data:image/png;base64,{base64.b64encode(p).decode()}"><figcaption>{i + 1} / {n}</figcaption></figure>' for i, p in enumerate(pngs))
    return f"""<!doctype html><html lang="{a.lang}"><meta charset="utf-8"><title>{t["title"]}</title>
<style>
@page {{ size: A4; margin: 8mm }}
body {{ font: 10.5pt/1.6 "Hiragino Sans", "Noto Sans JP", system-ui, sans-serif; color: #000; margin: 0 }}
h1 {{ font-size: 17pt; margin: 0 0 3mm }} h2 {{ font-size: 12pt; margin: 5mm 0 2mm }}
.note {{ border: 1.5pt solid #000; padding: 2.5mm 3mm; font-weight: 600 }}
.mono {{ font-family: ui-monospace, Menlo, monospace }}
.hash {{ font: 600 15pt/1.5 ui-monospace, Menlo, monospace; letter-spacing: .03em; word-spacing: .2em }}
ol {{ padding-left: 6mm; margin: 0 }} li {{ margin: 1mm 0 }}
.fields {{ display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 3mm; margin-top: 5mm }}
.field {{ border-bottom: 1pt solid #000; height: 11mm; font-size: 8.5pt; color: #444 }}
.codes {{ break-before: page }}
.grid {{ display: flex; flex-wrap: wrap; gap: 2mm }}
figure {{ margin: 0; text-align: center; break-inside: avoid }}
img {{ width: {mm:.1f}mm; image-rendering: pixelated; display: block }}
figcaption {{ font-size: 8pt }}
.foot {{ font-size: 8pt; margin-top: 2mm }}
</style>
<h1>{t["title"]}</h1>
<p class=note>{t["nosecret"]}</p>
<p>{t["need"]}</p>
<p>{t["loader"]}: <span class=mono>{a.loader_url}</span></p>
<p>{t["hash"]}</p>
<p class=hash>{groups}</p>
<ol>{steps}</ol>
<div class=fields>{fields}</div>
<section class=codes>
<h2>{t["codes"]}</h2>
<p class=foot>{t["codeinfo"].format(n=n, v=a.version)} {size} bytes. SHA-256 <span class=mono>{groups}</span></p>
<div class=grid>{figs}</div>
</section>
</html>
"""


def text(a):
    out = Path(a.outdir)
    out.mkdir(parents=True, exist_ok=True)
    for i, line in enumerate(Path(a.input).read_text().split()):
        (out / f"{i:02d}.png").write_bytes(png(QrCode.encode_segments(QrSegment.make_segments(line), QrCode.Ecc.LOW, boostecl=False)))


ap = argparse.ArgumentParser()
ap.add_argument("mode", choices=["pack", "text"])
ap.add_argument("input")
ap.add_argument("outdir")
ap.add_argument("--version", type=int, default=40)
ap.add_argument("--anim-version", type=int, default=20, help="version of anim.html's frames")
ap.add_argument("--lang", choices=sorted(TEXT), default="en")
ap.add_argument("--pngs", action="store_true", help="also write each code as qr-NN.png")
ap.add_argument("--module", type=float, default=0.32, help="printed module size in mm")
ap.add_argument("--loader-url", default="https://bitwasm-project.github.io/bitwasm-signer-pico/qrboot/")
args = ap.parse_args()
pack(args) if args.mode == "pack" else text(args)
