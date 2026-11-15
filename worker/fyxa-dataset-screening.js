// Conservative first-pass screening for Fyxa dataset candidates.
// This module never returns extracted text and never declares a source training-ready.
// Age/authorization, human review, private storage, and a real dataset pipeline remain separate gates.

export const MAX_FYXA_SCREEN_BYTES = 1 * 1024 * 1024;
const MAX_NODES = 20000;
const MAX_TEXT_BYTES = 512 * 1024;
const GEOMETRY_KEY = /(vertices|positions|indices|triangles|faces|normals|uvs|weights|mesh|geometry|vertexdata|indexdata|vertexbuffer|indexbuffer|submesh|morphtarget|morph|skeleton|bonedata|boneweights|skinweights|skinning|texture|imagedata|image|bitmap|pixels|rgba|binary|blob|buffer|filedata|base64)/i;
const TEXT_KEY = /(name|title|text|message|description|caption|label|value|prompt|dialog|script|source|code|author|license)/i;

const SECRET_PATTERNS = [
  { code: 'private_key', pattern: /-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----/i },
  { code: 'bearer_token', pattern: /\bBearer\s+[A-Za-z0-9._~+/=-]{16,}/i },
  { code: 'github_token', pattern: /\bgh[pousr]_[A-Za-z0-9]{30,}\b/i },
  { code: 'aws_access_key', pattern: /\bAKIA[0-9A-Z]{16}\b/ },
  { code: 'generic_secret_assignment', pattern: /\b(?:api[_-]?key|client[_-]?secret|access[_-]?token|password)\s*[:=]\s*["']?[^\s"' ,;]{12,}/i },
];
const PERSONAL_PATTERNS = [
  { code: 'email_address', pattern: /\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b/i },
  { code: 'phone_number', pattern: /(?:\+?1[ .-]?)?(?:\(?\d{3}\)?[ .-]?)\d{3}[ .-]\d{4}\b/ },
  { code: 'us_ssn', pattern: /\b\d{3}-\d{2}-\d{4}\b/ },
];

function result(status, reasons, counts = {}) {
  return {
    status,
    reasons: [...new Set(reasons)].slice(0, 12),
    scannedNodes: counts.scannedNodes || 0,
    textFields: counts.textFields || 0,
    textBytes: counts.textBytes || 0,
    secretMatches: counts.secretMatches || 0,
    personalDataMatches: counts.personalDataMatches || 0,
    ageStatus: 'not_verified',
    datasetEligible: false,
  };
}

export function screenFyxaTrainingSource(input) {
  if (!(input instanceof Uint8Array) && !(input instanceof ArrayBuffer))
    return result('manual_review', ['invalid_source_bytes']);
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  if (!bytes.length) return result('manual_review', ['empty_source']);
  if (bytes.length > MAX_FYXA_SCREEN_BYTES)
    return result('manual_review', ['source_exceeds_screening_limit']);

  let parsed;
  try {
    const text = new TextDecoder('utf-8', { fatal: true }).decode(bytes);
    parsed = JSON.parse(text);
  } catch {
    return result('manual_review', ['source_not_valid_utf8_json']);
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed))
    return result('manual_review', ['unexpected_source_shape']);

  const reasons = [];
  const counts = { scannedNodes: 0, textFields: 0, textBytes: 0, secretMatches: 0, personalDataMatches: 0 };
  const stack = [{ value: parsed, key: '', depth: 0 }];
  while (stack.length && counts.scannedNodes < MAX_NODES) {
    const current = stack.pop();
    counts.scannedNodes++;
    if (current.depth > 48) { reasons.push('source_nesting_limit'); continue; }
    const value = current.value;
    if (GEOMETRY_KEY.test(current.key)) {
      const present = Array.isArray(value) ? value.length > 0
        : value && typeof value === 'object' ? Object.keys(value).length > 0
          : typeof value === 'string' && value.length > 64;
      if (present) reasons.push('opaque_geometry_or_media');
      continue;
    }
    if (typeof value === 'string') {
      if (!TEXT_KEY.test(current.key)) continue;
      const sample = value.slice(0, Math.max(0, MAX_TEXT_BYTES - counts.textBytes));
      if (!sample && value.length) { reasons.push('text_scan_budget_exceeded'); continue; }
      counts.textFields++;
      counts.textBytes += new TextEncoder().encode(sample).byteLength;
      for (const item of SECRET_PATTERNS) if (item.pattern.test(sample)) {
        counts.secretMatches++;
        reasons.push(item.code);
      }
      for (const item of PERSONAL_PATTERNS) if (item.pattern.test(sample)) {
        counts.personalDataMatches++;
        reasons.push(item.code);
      }
      continue;
    }
    if (Array.isArray(value)) {
      for (let i = value.length - 1; i >= 0; i--) stack.push({ value: value[i], key: current.key, depth: current.depth + 1 });
    } else if (value && typeof value === 'object') {
      const entries = Object.entries(value);
      for (let i = entries.length - 1; i >= 0; i--) {
        const [key, child] = entries[i];
        stack.push({ value: child, key, depth: current.depth + 1 });
      }
    }
  }
  if (stack.length) reasons.push('source_node_limit');
  if (counts.secretMatches) return result('blocked_sensitive_material', reasons, counts);
  if (counts.personalDataMatches) return result('manual_review', reasons, counts);
  if (reasons.length) return result('manual_review', reasons, counts);
  // A clean heuristic scan is not proof of safety, age eligibility, or training rights.
  return result('screened_pending_human_and_age_review', ['automated_screen_only'], counts);
}
