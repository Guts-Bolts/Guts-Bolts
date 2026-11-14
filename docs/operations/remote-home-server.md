# Secure home server remote control

This feature lets the official Guts & Bolts account monitor and control one Windows game-server PC from the mobile dashboard at `https://gutsandbolts.net/server/`.

## Security model

- The home PC opens an outbound `wss://gutsandbolts.net/ws/remote` connection. It does not listen on a public port.
- Do not configure router port forwarding, Windows Remote Desktop exposure, SSH exposure, or PowerShell Remoting for this feature.
- The Worker accepts dashboard requests only when they contain a fresh, valid Guts & Bolts signed request for `staff.database.stats`, and the signing account matches the configured `OFFICIAL` account ID.
- The relay stores only a SHA-256 hash of the PC's 256-bit device credential. The raw credential is sent once to the connected agent over TLS and stored locally using Windows DPAPI for the current Windows user.
- Pairing uses a 256-bit one-time code, expires after five minutes, is stored as a hash, and is rate-limited to ten phone submissions per hour per account and client IP. If it expires while the agent is waiting, the PC displays a fresh code automatically.
- The agent runs as a normal, non-elevated Windows user. It does not expose a shell, PowerShell, file browser, arbitrary executable launch, or arbitrary command interface.
- The remote command allowlist is limited to start, stop, restart, slots (1–50), process priority (Normal/AboveNormal/High), and automatic crash recovery.
- The agent validates commands again, checks their expiry, and only manages the exact configured `GutsAndBoltsGameServer.exe`. It will not stop an unrelated process or start a duplicate executable.
- Device revocation invalidates the server-side credential immediately. Re-pairing requires a new code from the PC.
- The Durable Object records administrative actions and security events. Monitoring alerts trigger after CPU or memory remains at 90% for two minutes, or GPU temperature remains at 88°C for one minute. Agent disconnection and agent-reported crashes also generate alerts.
- Push subscriptions are encrypted with a separate Worker secret before being stored. Push endpoints are restricted to supported browser push providers.

This reduces the exposed attack surface; it cannot guarantee zero risk. Keep Windows, the browser, the Guts & Bolts agent, and the server executable updated. A same-origin website compromise or a compromise of the official account/browser remains a security risk.

## Cloudflare setup

After the Worker code and Durable Object migration are deployed:

1. Generate three independent keys on a trusted machine:

   ```powershell
   node tools/generate-remote-push-keys.mjs
   ```

2. Store each printed value as a Cloudflare Worker secret. Run each command and paste the matching value when Wrangler prompts:

   ```powershell
   npx wrangler secret put REMOTE_VAPID_PRIVATE_KEY
   npx wrangler secret put REMOTE_VAPID_PUBLIC_KEY
   npx wrangler secret put REMOTE_PUSH_ENCRYPTION_KEY
   ```

3. Do not commit the generated values, paste them into chat, or put them in `wrangler.jsonc`. Keep a secure offline backup of the keys. Losing the encryption key makes saved push subscriptions unreadable; rotating it requires users to subscribe again. Rotating the VAPID key also requires users to subscribe again.
4. Deploy the Worker after the secrets are configured. The Durable Object migration creates the isolated remote-control database.

The public VAPID key is not a password, but this project stores all three values as Worker secrets for simpler handling. The private VAPID key and subscription-encryption key must never be exposed to the browser.

## USB dedicated-server bootstrapper (Windows first release)

The repository includes `tools/Install-GutsAndBoltsServer.ps1`. Copy this single script to a USB drive. On the target Windows server, sign in as the ordinary Windows account that should own the game server, open PowerShell, change to the USB drive, and deliberately run:

```powershell
Set-ExecutionPolicy -Scope Process Bypass -Force
.\Install-GutsAndBoltsServer.ps1
```

Windows intentionally does not auto-execute scripts when a USB drive is inserted. The installer therefore requires one explicit launch; it does not bypass AutoRun or other Windows protections. Do not run it as Administrator.

The bootstrapper:
1. Queries official Guts & Bolts GitHub releases for the newest release containing the Windows dedicated-server package.
2. Downloads the versioned server archive and its SHA-256 checksum over HTTPS, verifies the checksum, and rejects unsafe archive paths.
3. Installs the server executable, required DLLs, game/catalog data, and remote-management agent under `%LOCALAPPDATA%\GutsAndBolts\DedicatedServer`.
4. Registers a least-privilege Scheduled Task for this Windows user's next sign-in.
5. Starts the agent in the current console, starts the dedicated server, and displays the one-time pairing code.

Pair the device at `https://gutsandbolts.net/server/` while signed into the official account. Keep the console open during first-time pairing. You can choose the initial slot count with `-Slots` (1–50), for example ` .\Install-GutsAndBoltsServer.ps1 -Slots 12`.

**Important operational limit:** this first bootstrapper is Windows-only and installs a per-user logon task, not a Windows service. The Windows account must sign in after a reboot for automatic startup; the machine must remain awake and connected. A future unattended service mode requires a dedicated service-host design and explicit elevated installation, which this version does not attempt.

The release archive is checked against a SHA-256 file published alongside it. This detects corruption and transfer errors; because the checksum is distributed through the same GitHub release, it is not an independent publisher signature. Do not treat it as protection against compromise of the repository or release account.

For pre-release testing, the installer also accepts a local package and its matching checksum without changing the normal release-feed behavior:

```powershell
.\Install-GutsAndBoltsServer.ps1 -PackagePath "$env:USERPROFILE\Downloads\GutsAndBolts-DedicatedServer-Windows.zip" -ChecksumPath "$env:USERPROFILE\Downloads\GutsAndBolts-DedicatedServer-Windows.zip.sha256" -Slots 8
```

Use the ZIP and sidecar from the successful dedicated-server Windows build artifact. The local mode performs the same SHA-256 verification, archive-path checks, package-layout checks, installation, and scheduled-task setup. It does not establish publisher authenticity by itself: obtain both files from the trusted Guts & Bolts GitHub Actions artifact for the intended commit. Run the installer from the normal interactive Windows account, not from an elevated shell or a LocalSystem service.

**When the USB installer works:** it downloads the server kit from a GitHub release. The kit is only added to releases made after this change is merged, so the first release that includes it (0.8.0) must be published before the normal (online) mode works. Until then, use the local `-PackagePath` mode above with the `dedicated-server-windows` artifact from a successful `main` build.

## The game server's account must be staff

`GutsAndBoltsGameServer.exe` signs in with the Guts & Bolts account saved on that Windows user (`%APPDATA%\GutsAndBolts\account.key`, the same one the Player uses). Only a staff account may run games. If the account is not staff, the server prints its account ID, says to give it the Staff badge, and exits; the agent then reports it as a crash.

- Easiest: sign in to the official account in the Player on that PC and Windows user before running the installer.
- Or: run `GutsAndBoltsGameServer.exe --id` in the install folder (`%LOCALAPPDATA%\GutsAndBolts\DedicatedServer`), then in the Player open **Staff**, search the first 12 characters of that ID and press **Make Staff**.

## Windows PC setup without the USB installer (manual)

Skip this section if you used the USB installer: it already installs the agent and a startup task.

1. Keep the PC powered on and connected to the internet. Disable sleep while it is expected to host the game server.
2. Download `tools/Remote Server Agent.ps1` from the repository after the change is merged.
3. Open a normal PowerShell window. Do not run it as Administrator.
4. Start the agent and select the installed server executable:

   ```powershell
   & ".\Remote Server Agent.ps1" -ServerExe "C:\path\to\GutsAndBoltsGameServer.exe"
   ```

   The script creates its per-user configuration under `%LOCALAPPDATA%\GutsAndBolts\RemoteAgent`. The device token is DPAPI-protected for the Windows user who runs the agent.
5. The console prints a 64-character one-time pairing code. Keep it private.
6. On the phone, sign in to the official Guts & Bolts account and open `https://gutsandbolts.net/server/`. Paste the code into Device pairing.
7. The agent should report that pairing succeeded. It will then reconnect automatically if the network drops.

The first release manages only the process launched and recorded by this agent. If the game server was started manually or by another manager, stop it yourself first; the agent deliberately refuses to take ownership of it. When started by hand like this, the agent does not add a startup task, so keep its PowerShell window open while remote access is needed. (The USB installer adds a startup task for you.)

## Phone setup

1. Open `https://gutsandbolts.net/server/` in a supported browser on the phone.
2. Sign in to the official Guts & Bolts account in the same browser profile.
3. Pair the PC once using the code displayed in PowerShell.
4. Select **Enable phone alerts** and grant notification permission. This registers the browser's push subscription with the Worker. The subscription's endpoint and encryption keys are encrypted before database storage.
5. Use **Disable phone alerts** to remove that browser's subscription. Use **Revoke device access** to invalidate the PC agent credential.

Android Chrome supports background Web Push. If the VAPID secrets have not been configured, the dashboard still works, but background phone alerts will remain unavailable.

## Operational notes

- Changing slots restarts the dedicated server because `--slots` is a startup option. Active sessions may disconnect.
- The dashboard polls metrics while it is open. Background push is used for sustained overload, agent disconnect, and agent-reported crash alerts.
- Automatic crash recovery is off by default. Enable it from the dashboard only after testing. It disables itself after three crashes in ten minutes.
- The current agent reports CPU, memory, NVIDIA GPU, and aggregate physical-network telemetry. GPU metrics are collected only from an Authenticode-valid NVIDIA `nvidia-smi.exe`, discovered in the standard NVSMI directory, `%WINDIR%\\System32`, or the executable search path. Otherwise GPU values remain unavailable.
- The PC must stay awake, online, and running the agent for remote commands to work. The Worker and dashboard do not wake a sleeping PC.
- This release is limited to the single configured home PC. Multi-device management, remote file access, desktop streaming, and arbitrary terminal access are intentionally out of scope.


## Isolated staging test

The repository includes a separate Worker harness under `tools/remote-staging/`. It uses its own SQLite-backed Durable Object namespace and a staging-only bearer token. It does not connect to the production Worker, production dashboard, or production device namespace.

Deploy it with the staging Wrangler configuration from that directory, then set the secret:

```powershell
npx wrangler secret put STAGING_CONTROL_TOKEN --config .\wrangler.jsonc
```

Use the staging dashboard URL as `-DashboardUri` and its `wss://...workers.dev/ws/remote` endpoint as `-RelayUri`:

```powershell
& ".\Remote Server Agent.ps1" -ServerExe "C:\path\to\GutsAndBoltsGameServer.exe" -RelayUri "wss://guts-and-bolts-remote-staging.<account-subdomain>.workers.dev/ws/remote" -DashboardUri "https://guts-and-bolts-remote-staging.<account-subdomain>.workers.dev/"
```

The staging dashboard asks for the staging control token and the one-time code displayed by the agent. The token is not stored by the page. Start/stop/restart buttons operate on the configured test server executable, so only use a maintenance window and a test server instance. The production defaults remain unchanged when the two URI parameters are omitted. The agent also keeps separate local configuration and DPAPI credentials for each relay hostname, so staging credentials cannot be reused against production.

The staging harness intentionally has no production account authentication, push notifications, or production data bindings. Its purpose is to exercise the Windows agent's TLS WebSocket connection, pairing, heartbeat/telemetry, command delivery, acknowledgements, and revocation without touching production. Treat the staging token as a credential and rotate it after testing.
