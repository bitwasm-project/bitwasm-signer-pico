# qrboot

The whole signer, UI included, as one wasm app that a QR boot loader runs. `src/main.c` and `src/ui`
are built as they are for the device; `src/qrboot` swaps the board for the loader's imports
(`src/qrboot/qrboot.h`: screen lines, keys, clock, camera frames, QR decoding, log) and links the parser
in. The loader holds no Bitcoin code.

```sh
make build/qrboot/app.wasm build/qrboot/host/host
python3 tools/qrboot/codes.py pack build/qrboot/app.wasm out --version 40   # printable codes + sheet.html
build/qrboot/host/host script.txt shot.png --qr "<sha256>" out/qr-*.png      # boot from the codes
```

`host` is the loader on a Mac, without a window: WAMR's classic interpreter, the firmware's QR reader
built for every version, keys and camera frames from a script, the last screen written to a PNG. It
refuses codes whose SHA-256 differs from the one printed with them.

| | |
|---|---|
| app.wasm | 138,712 bytes, 70,584 compressed |
| codes at version 40 / 25 / 15 | 27 / 62 / 152 |

The app links wasi-libc's `vsnprintf`, which names `fd_write` and `fd_seek`; the loader provides them as
stubs that are never called.
