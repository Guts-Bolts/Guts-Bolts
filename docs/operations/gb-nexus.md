# G&B Nexus owner console

G&B Nexus is the owner-only infrastructure inventory at `https://gutsandbolts.net/server/`. It extends the existing private home-server dashboard without replacing the legacy game-server control channel.

## Access control

- The page calls the signed Guts & Bolts `staff.check` operation.
- The Worker independently verifies the signature with the authoritative G&B server and requires the account ID to match the configured `OFFICIAL` ID.
- The infrastructure API accepts only the explicit `list`, `pair`, `revoke`, and `audit` actions.
- Infrastructure nodes receive an individual random 256-bit credential. Only its SHA-256 hash is stored by the Durable Object.
- Node credentials are protected with Windows DPAPI for the Windows account running the agent.
- Pairing uses a 256-bit one-time code, expires after five minutes, and is rate-limited on both the browser and agent side.
- Nodes connect outbound to `wss://gutsandbolts.net/ws/infrastructure`. No inbound router ports, remote desktop, SSH, PowerShell Remoting, shell, or arbitrary command execution are exposed.
- Revocation invalidates a node credential immediately. The node must be paired again to reconnect.

## Current implementation boundary

This first Nexus milestone provides a secure multi-machine registry and live telemetry. It is intentionally read-only.

The Windows agent reports hostname, OS, CPU, memory, primary C: drive capacity, NVIDIA GPU telemetry when an Authenticode-valid `nvidia-smi.exe` is available, and a small set of detected capabilities. The dashboard displays online state, last heartbeat, CPU/RAM/GPU/disk readings, and audit events.

The following are **not implemented by this milestone** and must not be presented as working controls yet:

- Remote installation, updating, starting, or stopping arbitrary services.
- AI model lifecycle management or inference scheduling.
- ContentForge video-generation job execution and GPU allocation.
- Multi-instance game-server provisioning and scheduling.
- Remote file browsing, uploads, downloads, desktop streaming, or terminal access.
- Linux/macOS agents.

The existing `/api/remote` and `/ws/remote` system remains responsible for the legacy single dedicated game-server process and its narrow command allowlist. Nexus uses a separate Durable Object and separate node credentials.

## Deploy

The Worker configuration adds the `INFRA_CONTROL` Durable Object binding and migration `v3`. Deploy the Worker using the repository's normal Wrangler release process so Cloudflare creates the new SQLite-backed Durable Object class. No new secrets or external database credentials are required for this milestone.

## Pair a Windows machine

1. Sign in to the official G&B owner account in the same browser profile that will open the console.
2. On the Windows machine, download `tools/G&B Nexus Infrastructure Agent.ps1` from the deployed repository revision.
3. Open a normal PowerShell window as the intended Windows user. Do not run as Administrator.
4. Run:

   ```powershell
   Set-ExecutionPolicy -Scope Process Bypass -Force
   .\G&B Nexus Infrastructure Agent.ps1
   ```

5. The agent prints a 64-character, one-time pairing code.
6. Open `https://gutsandbolts.net/server/` in the authenticated owner browser, paste the code under **Machine fleet**, and select **Pair machine**.
7. Keep the agent running. It reconnects automatically after temporary network failures and sends telemetry approximately every ten seconds.

The agent stores its credential under `%LOCALAPPDATA%\GutsAndBolts\NexusAgent`. Its credential file is DPAPI-protected for the current Windows user. To remove the local pairing, stop the agent and delete that directory. Revoke the machine in Nexus first if the machine is being retired or may be compromised.

## Operational notes

- Online status requires a heartbeat within 45 seconds.
- A machine can be paired while offline later; the registry retains its last known status and last-seen timestamp.
- GPU metrics are optional. Unsupported or unavailable GPU telemetry is displayed as unavailable, not zero.
- Capability labels are agent observations, not proof that a workload is installed, configured, or ready for production.
- Do not expose the agent's pairing code or local DPAPI credential.
- This agent is telemetry-only. It does not accept remote commands. Workload orchestration will require a separately reviewed, narrowly scoped command protocol with per-action authorization, expiry, acknowledgements, and audit records.
