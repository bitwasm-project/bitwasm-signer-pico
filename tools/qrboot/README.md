# qrboot

The whole signer, UI included, as one wasm app that a QR boot loader runs. `src/main.c` and `src/ui`
are built as they are for the device; `src/qrboot` swaps the board for the loader's imports
(`src/qrboot/qrboot.h`: screen lines, keys, clock, camera frames, QR decoding, log) and links the parser
in. The loader holds no Bitcoin code.

```sh
make build/qrboot/app.wasm build/qrboot/host/host
python3 tools/qrboot/codes.py pack build/qrboot/app.wasm out --pngs        # sheet.html, anim.html, qr-NN.png
build/qrboot/host/host script.txt shot.png --qr "<sha256>" out/qr-*.png      # boot from the codes
```

`host` is the loader on a Mac, without a window: WAMR's classic interpreter, the firmware's QR reader
built for every version, keys and camera frames from a script, the last screen written to a PNG. It
refuses codes whose SHA-256 differs from the one printed with them.

| | |
|---|---|
| app.wasm | 116,237 bytes, 60,368 compressed |
| codes at version 40 / animated frames at version 20 | 23 / 78 |

`make qrboot-release` writes what is committed in `release/qrboot`: `app.wasm` without the test seed,
its `sheet.html` (the codes inline, so one file) and `anim.html`, and the hash in `SHA256.txt`.

## In a browser

`make qrboot-web` puts the loader in `build/qrboot/web`: it reads the codes from the camera or from
images, shows the SHA-256, and runs the app in a worker on the browser's WebAssembly. `sw.js` caches it
for offline use and adds the cross-origin isolation its shared memory needs, so any static HTTPS host
will do; `tools/qrboot/serve.py` serves it locally.

## The sheet for an heir

`codes.py pack` also writes `sheet.html`, four A4 pages: the steps, the hash to compare and the codes
(`--lang ja|en`, `--loader-url`, `--module` for the printed module size). It holds no secret. The key
stays on its SeedQR plate, which the app asks for.

`anim.html` is the same app as an animated QR for a screen, its frames at `--anim-version` (20 by
default) so a phone reads each at a glance.

The app imports only `qrboot`: `src/qrboot/libc_min.c` supplies the `snprintf` and `malloc` it needs in
place of wasi-libc's, whose stdio would name WASI's `fd_write` and `fd_seek`.
