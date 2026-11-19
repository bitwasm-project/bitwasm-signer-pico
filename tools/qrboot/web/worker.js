// Runs the app on the browser's WebAssembly, lending it src/qrboot/qrboot.h. The app's loops block, so it
// lives in a worker, waiting on shared memory for keys and camera frames
import { qrReader } from "./qr.mjs";

onmessage = async ({ data: { wasm, ctrlSab, frameSab, screenSab } }) => {
  const ctrl = new Int32Array(ctrlSab), frame = new Uint8Array(frameSab), screen = new Uint8Array(screenSab);
  const qr = await qrReader("qr_reader.wasm");
  let mem, lastSeq = Atomics.load(ctrl, 2);
  const bytes = (p, n) => {
    if (p < 0 || n < 0 || p + n > mem.buffer.byteLength) throw new Error("qrboot: address out of bounds");
    return new Uint8Array(mem.buffer, p, n);
  };
  const qrboot = {
    draw_line(y, p) {
      if (y < 0 || y >= 240) return;
      screen.set(bytes(p, 480), y * 480);
      if (y === 239) postMessage({ present: true });
    },
    key() {
      const head = Atomics.load(ctrl, 0), tail = Atomics.load(ctrl, 1);
      if (head === tail) return -1;
      Atomics.store(ctrl, 1, tail + 1);
      return Atomics.load(ctrl, 8 + (tail % 16));
    },
    time_us: () => BigInt(Math.round(performance.now() * 1000)),
    // Woken early by a key, which is what a short sleep in a polling loop is waiting for anyway
    sleep_ms: (ms) => void Atomics.wait(ctrl, 3, Atomics.load(ctrl, 3), ms),
    capture(p, w, h) {
      if (w !== 320 || h !== 240) return 0;
      Atomics.add(ctrl, 4, 1);
      if (Atomics.load(ctrl, 2) === lastSeq) Atomics.wait(ctrl, 2, lastSeq, 500);
      const seq = Atomics.load(ctrl, 2);
      if (seq === lastSeq) return 0;
      lastSeq = seq;
      bytes(p, w * h).set(frame);
      return 1;
    },
    qr_decode(px, w, h, kind, out, cap) {
      const r = qr.decode(bytes(px, w * h), w, h, kind);
      if (typeof r === "number") return r;
      if (r.length > cap) return -1;
      bytes(out, r.length).set(r);
      return r.length;
    },
    random: (p, n) => void crypto.getRandomValues(bytes(p, n)),
    log: (p, n) => postMessage({ log: new TextDecoder().decode(bytes(p, n)) }),
  };
  try {
    const { instance } = await WebAssembly.instantiate(wasm, { qrboot });
    mem = instance.exports.memory;
    instance.exports.main();
    postMessage({ stopped: "the app returned" });
  } catch (err) {
    postMessage({ stopped: String(err) });
  }
};
