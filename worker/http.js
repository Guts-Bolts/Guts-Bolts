// Bounded request-body readers shared by the Worker, Durable Object and protocol endpoints.
const DEFAULT_MAX_BODY = 40 * 1024 * 1024;

export async function readLimitedBytes(request, maxBytes = DEFAULT_MAX_BODY) {
  if (!Number.isSafeInteger(maxBytes) || maxBytes < 0) throw new TypeError('Invalid body limit.');
  if (!request.body) return new Uint8Array(0);
  const reader = request.body.getReader();
  let bytes = new Uint8Array(Math.min(maxBytes, 64 * 1024));
  let total = 0;
  try {
    for (;;) {
      const { value, done } = await reader.read();
      if (done) break;
      const nextTotal = total + value.byteLength;
      if (nextTotal > maxBytes) {
        await reader.cancel('Request body exceeds the configured limit.');
        return null;
      }
      if (nextTotal > bytes.length) {
        const capacity = Math.min(maxBytes, Math.max(nextTotal, Math.max(1024, bytes.length * 2)));
        const grown = new Uint8Array(capacity);
        grown.set(bytes.subarray(0, total));
        bytes = grown;
      }
      bytes.set(value, total);
      total = nextTotal;
    }
  } finally {
    reader.releaseLock();
  }
  return bytes.subarray(0, total);
}

export async function readLimitedText(request, maxBytes = DEFAULT_MAX_BODY) {
  const bytes = await readLimitedBytes(request, maxBytes);
  return bytes === null ? null : new TextDecoder().decode(bytes);
}
