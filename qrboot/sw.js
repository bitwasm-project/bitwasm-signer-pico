// Keeps the loader working offline and gives it the cross-origin isolation its shared memory needs, which
// static hosts such as GitHub Pages cannot set as headers: every response passes through here with them added
const CACHE = "qrboot-v2";
const FILES = ["./", "index.html", "loader.js", "worker.js", "qr.mjs", "qr_reader.wasm", "manifest.json"];

// A new CACHE name replaces the files: fetched past the HTTP cache, and older caches dropped so none shadows them
self.addEventListener("install", (ev) => ev.waitUntil(caches.open(CACHE).then((c) => c.addAll(FILES.map((f) => new Request(f, { cache: "reload" })))).then(() => self.skipWaiting())));
self.addEventListener("activate", (ev) => ev.waitUntil(caches.keys().then((ks) => Promise.all(ks.filter((k) => k !== CACHE).map((k) => caches.delete(k)))).then(() => self.clients.claim())));

function isolated(res) {
  if (res.status === 0) return res;
  const headers = new Headers(res.headers);
  headers.set("Cross-Origin-Opener-Policy", "same-origin");
  headers.set("Cross-Origin-Embedder-Policy", "require-corp");
  headers.set("Cross-Origin-Resource-Policy", "same-origin");
  return new Response(res.body, { status: res.status, statusText: res.statusText, headers });
}

// The cache first, so nothing is fetched once installed; the network only for what was never cached
self.addEventListener("fetch", (ev) => {
  if (ev.request.method !== "GET" || new URL(ev.request.url).origin !== location.origin) return;
  ev.respondWith(caches.match(ev.request, { ignoreSearch: true }).then((hit) => hit || fetch(ev.request)).then(isolated));
});
