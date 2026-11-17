// The loader in a browser: reads the app's BBQr codes from the camera or from images, shows its SHA-256,
// and on approval runs it in worker.js. The screen, keys and camera frames cross over in shared memory
import { qrReader } from "./qr.mjs";

const $ = (id) => document.getElementById(id);
const VERSION = "v5";
// A failure while loading would otherwise leave buttons that do nothing
const fail = (e) => (($("iso").className = "bad"), ($("iso").textContent = `${VERSION} error: ${e?.message ?? e}`));
addEventListener("error", (ev) => fail(ev.error ?? ev.message));
addEventListener("unhandledrejection", (ev) => fail(ev.reason));

// Isolation comes from sw.js on hosts that cannot send headers. The first visit reloads once it is in control
if ("serviceWorker" in navigator) {
  await navigator.serviceWorker.register("sw.js");
  if (!crossOriginIsolated && !sessionStorage.getItem("qrboot-reloaded")) {
    sessionStorage.setItem("qrboot-reloaded", "1");
    await navigator.serviceWorker.ready;
    location.reload();
    await new Promise(() => {});
  }
}
const B36 = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ", B32 = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
const qr = await qrReader("qr_reader.wasm");
const canvas = document.createElement("canvas"), ctx2d = canvas.getContext("2d", { willReadFrequently: true });
let parts = new Map(), total = 0, app = null, stream = null, running = null, last = -1;

$("cam").disabled = false, ($("cam").textContent = "Start camera");
$("iso").textContent = VERSION + " · " + (crossOriginIsolated
  ? "Offline once loaded: nothing here talks to the network."
  : "This browser could not isolate the page, which the app's worker needs. Reload, or serve it with tools/qrboot/serve.py.");

// Greyscale pixels of a drawable, scaled to fit w x h (keeping its shape) and centred on white
function grey(src, sw, sh, w, h, fit = 1, sx = 0, sy = 0) {
  canvas.width = w, canvas.height = h;
  ctx2d.fillStyle = "#fff", ctx2d.fillRect(0, 0, w, h);
  const s = Math.min(w / sw, h / sh) * fit, dw = sw * s, dh = sh * s;
  ctx2d.drawImage(src, sx, sy, sw, sh, (w - dw) / 2, (h - dh) / 2, dw, dh);
  const rgba = ctx2d.getImageData(0, 0, w, h).data, out = new Uint8Array(w * h);
  for (let i = 0; i < out.length; i++) out[i] = (rgba[i * 4] * 77 + rgba[i * 4 + 1] * 150 + rgba[i * 4 + 2] * 29) >> 8;
  return out;
}

function largest(sw, sh) {
  const s = Math.min(1, 1280 / Math.max(sw, sh), 720 / Math.min(sw, sh));
  return [Math.round(sw * s), Math.round(sh * s)];
}

// --- boot: collect the codes ---
function drawGrid() {
  $("grid").replaceChildren(...Array.from({ length: total }, (_, i) => {
    const s = document.createElement("span");
    s.textContent = i + 1;
    if (parts.has(i)) s.className = i === last ? "got new" : "got";
    return s;
  }));
  $("count").textContent = total ? `${parts.size} / ${total}` : "0";
  $("bar").style.width = total ? `${(100 * parts.size) / total}%` : "0";
}

function takePart(text) {
  if (!text.startsWith("B$ZB")) return "not an app code";
  const t = B36.indexOf(text[4]) * 36 + B36.indexOf(text[5]), i = B36.indexOf(text[6]) * 36 + B36.indexOf(text[7]);
  if (total && t !== total) return "a code from another set";
  if (!total) total = t;
  const fresh = !parts.has(i);
  if (fresh) parts.set(i, text.slice(8)), last = i, drawGrid(), navigator.vibrate?.(60);
  scanState(fresh ? "got" : "seen", fresh ? `Read code ${i + 1}` : `Code ${i + 1} was already read`);
  if (parts.size === total && !app) assemble();
  return null;
}

async function assemble() {
  const text = Array.from({ length: total }, (_, i) => parts.get(i)).join("");
  const packed = new Uint8Array(Math.floor((text.length * 5) / 8));
  for (let i = 0, acc = 0, bits = 0, o = 0; i < text.length; i++) {
    acc = ((acc << 5) | B32.indexOf(text[i])) & 0xffff;
    if ((bits += 5) >= 8) packed[o++] = (acc >> (bits -= 8)) & 255;
  }
  try {
    // BBQr's zlib is raw deflate; any raw inflater reads its 1 KiB window
    const wasm = new Uint8Array(await new Response(new Blob([packed]).stream().pipeThrough(new DecompressionStream("deflate-raw"))).arrayBuffer());
    const hex = [...new Uint8Array(await crypto.subtle.digest("SHA-256", wasm))].map((b) => b.toString(16).padStart(2, "0")).join("");
    app = wasm;
    $("hash").replaceChildren(...hex.match(/.{4}/g).map((g) => Object.assign(document.createElement("span"), { textContent: g })));
    $("verify").hidden = $("verifyActions").hidden = false;
    $("scanActions").hidden = $("grid").hidden = true;
    $("bootStatus").textContent = `codes read: ${wasm.length} bytes`;
    stopCamera(), ($("idle").hidden = true);
  } catch (err) {
    $("bootStatus").innerHTML = `<span class="bad">The codes did not decompress: ${err.message}</span>`;
  }
}

$("bootFiles").onchange = async (ev) => {
  for (const f of ev.target.files) {
    const bmp = await createImageBitmap(f), [w, h] = largest(bmp.width, bmp.height);
    const r = qr.decode(grey(bmp, bmp.width, bmp.height, w, h), w, h);
    const why = typeof r === "number" ? `no code in ${f.name}` : takePart(new TextDecoder("latin1").decode(r));
    if (why) $("bootStatus").textContent = why;
    else $("bootStatus").textContent = "codes read";
  }
  ev.target.value = "";
};

// --- camera ---
// One element at a time: iOS may stall play() on a second element, or on a hidden one
async function startCamera(v) {
  if (!navigator.mediaDevices?.getUserMedia) throw new Error("this browser gives pages no camera");
  stream = await navigator.mediaDevices.getUserMedia({ video: { facingMode: "environment", width: { ideal: 3840 }, height: { ideal: 2160 } } });
  v.srcObject = stream;
  v.play().catch((e) => fail(e));
}

function stopCamera() {
  stream?.getTracks().forEach((t) => t.stop());
  stream = null, $("scanBox").hidden = true, $("idle").hidden = false;
}

$("cam").onclick = async () => {
  const v = $("bootVideo");
  $("scanBox").hidden = false, $("idle").hidden = true, $("cam").hidden = true;
  $("scanState").textContent = "Starting the camera";
  try {
    await startCamera(v);
  } catch (err) {
    $("scanBox").hidden = true, $("cam").hidden = false, $("idle").hidden = false;
    $("idle").innerHTML = `<span class="bad">Camera unavailable: ${err.name} ${err.message}</span>`;
    return;
  }
  (function scan() {
    if (!stream || app) return;
    if (v.videoWidth) {
      // Version 40 needs about 3.5 pixels a module, more than a whole frame scaled to 720 gives, so the
      // centre square is read at up to the camera's own resolution: all of it, and the guide's middle part
      const vw = v.videoWidth, vh = v.videoHeight, t = performance.now();
      let r = 0;
      for (const f of [1, 0.55]) {
        const side = Math.round(Math.min(vw, vh) * f), n = Math.min(side, 960);
        const got = qr.decode(grey(v, side, side, n, n, 1, (vw - side) / 2, (vh - side) / 2), n, n);
        if (typeof got !== "number") { r = got; break; }
        r = Math.min(r, got);
      }
      const ms = Math.round(performance.now() - t);
      if (typeof r !== "number") {
        const why = takePart(new TextDecoder("latin1").decode(r));
        if (why) scanState("seen", why);
      } else if (r === -1) scanState("seen", `A code is in view but did not read (${ms} ms): fill the inner square, hold still, check focus`);
      else scanState("", `No code in view (camera ${vw}x${vh}, ${ms} ms)`);
    }
    setTimeout(scan, 100);
  })();
};

// The border flashes for a read; a plain state does not clear a flash before it has been seen
let flashUntil = 0;
function scanState(cls, text) {
  const now = performance.now();
  if (!cls && now < flashUntil) return;
  if (cls) flashUntil = now + 600;
  $("bootVideo").className = cls, $("scanState").textContent = text;
}

$("discard").onclick = () => {
  parts = new Map(), total = 0, app = null, last = -1;
  $("verify").hidden = $("verifyActions").hidden = true;
  $("scanActions").hidden = $("grid").hidden = $("cam").hidden = $("idle").hidden = false;
  $("bootStatus").textContent = "codes read", drawGrid();
};

// --- run ---
$("run").onclick = async () => {
  if (!crossOriginIsolated) return alertLine("needs cross-origin isolation (serve.py)");
  const ctrlSab = new SharedArrayBuffer(24 * 4), frameSab = new SharedArrayBuffer(320 * 240), screenSab = new SharedArrayBuffer(240 * 480);
  const ctrl = new Int32Array(ctrlSab), frame = new Uint8Array(frameSab), screen = new Uint16Array(screenSab);
  const sctx = $("screen").getContext("2d"), img = sctx.createImageData(240, 240);
  const worker = new Worker("worker.js", { type: "module" });
  running = { ctrl, frame, queue: [], calls: 0, lastCall: 0, starting: false };
  $("boot").hidden = true, $("device").hidden = false;

  worker.onmessage = ({ data }) => {
    if (data.present) {
      for (let i = 0; i < 240 * 240; i++) {
        const c = screen[i];
        img.data.set([((c >> 11) * 255) / 31, (((c >> 5) & 63) * 255) / 63, ((c & 31) * 255) / 31, 255], i * 4);
      }
      sctx.putImageData(img, 0, 0);
    } else if (data.log) {
      const log = $("log");
      log.textContent += data.log;
      log.scrollTop = log.scrollHeight;
    } else if (data.stopped) {
      alertLine(`app stopped: ${data.stopped}`);
    }
  };
  worker.postMessage({ wasm: app, ctrlSab, frameSab, screenSab });

  setInterval(pushFrame, 100);
};

function alertLine(text) {
  $("log").textContent += text + "\n";
}

function press(key) {
  if (!running) return;
  const { ctrl } = running, head = Atomics.load(ctrl, 0);
  Atomics.store(ctrl, 8 + (head % 16), key);
  Atomics.store(ctrl, 0, head + 1);
  Atomics.add(ctrl, 3, 1), Atomics.notify(ctrl, 3);
}

// Queued images play as camera frames, each held for a few frames; otherwise the live camera
// The camera runs only while the app is capturing (it counts its calls in ctrl[4]); the app draws the preview
function pushFrame() {
  const { ctrl, frame, queue } = running, v = $("video"), now = performance.now(), calls = Atomics.load(ctrl, 4);
  if (calls !== running.calls) (running.calls = calls), (running.lastCall = now);
  const wanted = now - running.lastCall < 1500;
  if (wanted && !stream && !running.starting && !running.noCamera) {
    running.starting = true;
    startCamera(v)
      .catch(() => ((running.noCamera = true), alertLine("no camera: use 'Show images to the camera'")))
      .finally(() => (running.starting = false));
  } else if (!wanted && stream) stopCamera();
  let px = null;
  if (queue.length) {
    const q = queue[0];
    px = q.px;
    if (--q.left <= 0) queue.shift();
  } else if (stream && v.videoWidth) {
    px = grey(v, v.videoWidth, v.videoHeight, 320, 240);
  }
  if (!px) return;
  frame.set(px);
  Atomics.add(ctrl, 2, 1), Atomics.notify(ctrl, 2);
}

$("feedFiles").onchange = async (ev) => {
  for (const f of ev.target.files) {
    const bmp = await createImageBitmap(f);
    running?.queue.push({ px: grey(bmp, bmp.width, bmp.height, 320, 240, 0.9), left: 3 });
  }
  ev.target.value = "";
};

document.querySelectorAll("[data-key]").forEach((b) => (b.onclick = () => press(+b.dataset.key)));
const KEYS = { ArrowUp: 0, ArrowDown: 1, ArrowLeft: 2, ArrowRight: 3, Enter: 4, Escape: 5 };
addEventListener("keydown", (ev) => {
  if (running && ev.key in KEYS) press(KEYS[ev.key]), ev.preventDefault();
});
drawGrid();
