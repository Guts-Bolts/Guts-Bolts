# Security hardening and production verification

(How to report a security problem is in [SECURITY.md](../SECURITY.md). This page is the checklist of what the code does and what to check in the dashboards.)

This document describes the controls implemented in the repository and the checks that still require access to the live GitHub and Cloudflare configuration. A passing source-code test is not a production penetration test.

## Controls in this change

### Browser and HTTP

- The Worker applies baseline security headers to HTTP responses and static assets: HSTS on the Guts&Bolts domain, X-Content-Type-Options, X-Frame-Options, Referrer-Policy, Permissions-Policy, and Origin-Agent-Cluster.
- HTML responses receive an enforcing Content Security Policy. The homepage's inline scripts were moved to the same-origin website/home.js module so the policy does not need unsafe-inline in script-src.
- The policy permits the Cloudflare Turnstile script/frame, same-origin app assets, WebAssembly compilation, and the site's required styles, images, media and WebSocket connection. style-src still permits inline styles because the current UI uses style attributes. The image/media source policy currently permits HTTPS sources; narrow these after confirming all legitimate content hosts.
- The browser account signing key is now encrypted in localStorage with AES-GCM. Its encryption key is a non-extractable WebCrypto key stored in IndexedDB. Existing plaintext gb.key values are migrated automatically without changing the account key or requiring a password reset.
- This browser-key design protects a copied localStorage value on its own; it does not protect against active same-origin XSS or malicious code already running in the page. Such code can still invoke signed actions while the user is logged in.
- Existing request signing, account password derivation, OAuth and authorization protocols remain compatible.

### API, uploads and release publishing

- Request bodies for the API, OAuth token/revocation endpoints and MCP endpoint are read as streams and rejected when they exceed their byte limits. The Durable Object enforces the API limit itself, not only the outer Worker.
- Release uploads are streamed into R2 with a hard byte ceiling; the release manifest is size-limited and validates version, platform names and filenames before publication.
- Release publishing requires a valid GitHub OIDC token from the designated repository, workflow, event and allowed ref.
- The rate limiter uses its existing isolate-local fallback if Cloudflare's counter binding throws. This is best-effort only: isolate-local counters are not globally coordinated during a Cloudflare rate-limit outage.

### Database and authorization

- Player accounts, sign-in, Bolts, inventory and core game state live in Cloudflare Durable Object SQLite, and the server checks who may do what.
- Some extra features (leaderboards, creator stats, the friend feed) also use Supabase, reached only from the Worker with a server-only secret key. See docs/SUPABASE.md. Its tables must stay server-only (row-level security on, no public access for the browser keys).
- Runtime data access is through the Durable Object's SQLite API and application authorization checks. Review any future external database or storage integration separately; do not assume the Durable Object authorization model protects a new data service.
- Keep all staff, ownership, private asset, group-rank, OAuth-scope and connected-app checks in the server/relay, not only in frontend visibility rules.

### Build and supply chain

- Git dependencies previously tracking moving branches are pinned to immutable commit IDs in CMakeLists.txt.
- GitHub Actions references in the build workflow are pinned to immutable commit IDs. Dependabot checks GitHub Actions dependencies weekly.
- A scheduled Gitleaks workflow scans the working tree and reachable Git history. It downloads a pinned Gitleaks release and verifies its SHA-256 before running.
- The Cloudflare deployment workflow continues to use a GitHub Actions secret for its Cloudflare API token; no token value is committed to the repository.

## Required live production checks

These cannot be proven from repository source alone. Complete them in the GitHub and Cloudflare dashboards before treating the deployment as fully audited.

### GitHub repository settings

- [ ] Enable secret scanning and push protection if available for this repository/organization. The Gitleaks workflow is an additional control, not a replacement for GitHub's native controls.
- [ ] Require pull requests for main; disallow force pushes and branch deletion.
- [ ] Require the build tests and Security scans checks before merge.
- [ ] Restrict who can modify repository secrets, deployment environments and workflow files.
- [ ] Review GitHub Actions allowed actions and pin third-party actions to full commit SHAs. The build workflow pins its current actions; review any newly introduced workflow before merge.
- [ ] Review all Gitleaks findings. If a credential was ever committed, revoke/rotate it first. Removing it from the latest tree does not invalidate the historical credential.

### Cloudflare

- [ ] Confirm BREVO_API_KEY and TURNSTILE_SECRET are configured as Worker secrets, not plaintext variables. Keep OFFICIAL as a public variable; it is an account identifier, not an API credential.
- [ ] Confirm CLOUDFLARE_API_TOKEN is stored only as a GitHub Actions secret and is restricted to the intended Cloudflare account and the minimum permissions required to deploy this Worker, its assets, Durable Object migrations and R2 integration.
- [ ] Confirm the gutsandbolts-downloads R2 bucket is not publicly exposed through an R2.dev URL or public custom domain. Downloads should be served through the Worker route.
- [ ] Review R2 bucket CORS rules. The website should not need direct browser access to the bucket; avoid wildcard origins and unnecessary methods.
- [ ] Confirm the Worker has only the required Durable Object, R2, AI and rate-limit bindings. Review any dashboard-only bindings and environment overrides.
- [ ] Confirm HTTPS redirection and the active TLS policy for the zone. After deployment, verify HSTS is returned on https://gutsandbolts.net and its intended subdomains.
- [ ] Confirm Turnstile is configured for the production hostname(s), and that the production site key and secret are a matching pair.
- [ ] If GB_SERVER is configured to use an external server, verify its TCP listener is not generally exposed and that ingress is restricted to the intended trusted path. This repository cannot enforce an external host firewall.
- [ ] Confirm backups/exports and a tested restore procedure exist for Durable Object account, inventory, currency and game data. Do not test restore against production data.

### Post-deployment verification

- [ ] Fetch / and /app/ and confirm the CSP is present and the homepage, login, catalog, avatar rendering, OAuth approval and Turnstile flows still work.
- [ ] Confirm no CSP violations block required functionality. Tighten broad HTTPS image/media allowances after identifying the legitimate origins.
- [ ] Confirm API, OAuth, MCP and release upload endpoints return 413 for oversized bodies.
- [ ] Verify an ordinary release upload from the designated workflow succeeds, while a token from another workflow or event is rejected.
- [ ] Exercise staff-only actions with a normal account and confirm the server rejects them.
- [ ] Exercise private game/model access, asset ownership and connected-app scopes using accounts with different permissions.
- [ ] Confirm no logs, analytics events or error responses expose passwords, API keys, OAuth client secrets, bearer tokens, signing keys or private upload contents.

## Scope and limitations

This repository change does not rotate production secrets, inspect the values of GitHub or Cloudflare secrets, change Cloudflare dashboard settings, alter R2 public-access configuration, run a live penetration test, or restore a database backup. Those require owner-authorized access to the relevant services.

A full-history secret scan may identify credentials that were removed from the current source but remain in Git history. Treat any real finding as exposed: revoke or rotate it, investigate its use, then decide whether history rewriting is appropriate. Do not rely on history rewriting instead of rotation.

For a suspected vulnerability, use GitHub's private vulnerability reporting for this repository if it is enabled. Avoid posting unredacted secrets or exploit details in public issues.
