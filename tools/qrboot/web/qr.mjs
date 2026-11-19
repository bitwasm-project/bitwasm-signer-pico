// The firmware's QR reader (src/runtime/qr_reader) built for the browser without its small limits.
// decode() takes greyscale pixels, at most 1280 x 720 either way up
export async function qrReader(url) {
  const { instance } = await WebAssembly.instantiateStreaming(fetch(url), {});
  const e = instance.exports, mem = e.memory;
  const cap = e.qr_reader_output_cap(), stateSize = e.qr_reader_state_size(), frameCap = 1280 * 720;
  // Pages of our own past what the module uses; its allocator grows past them if it ever needs to
  const base = mem.buffer.byteLength;
  mem.grow(Math.ceil((stateSize + 16 + cap + frameCap) / 65536));
  const state = (base + 15) & ~15, out = state + stateSize, frame = out + cap;
  e.qr_reader_state_init(state);
  return {
    // kind 0 any, 1 SeedQR, 2 animated UR. Returns the decoded bytes, or a number: 0 none, -1 unreadable, -3 wrong kind
    decode(grey, w, h, kind = 0, budget = 1000) {
      if (w * h > frameCap) throw new Error("frame too large");
      new Uint8Array(mem.buffer, frame, w * h).set(grey);
      const n = e.qr_reader_decode(state, frame, w, h, kind === 1 ? 1 : 0, kind === 2 ? 1 : 0, out, budget);
      return n > 0 ? new Uint8Array(mem.buffer, out, n).slice() : n;
    },
  };
}
