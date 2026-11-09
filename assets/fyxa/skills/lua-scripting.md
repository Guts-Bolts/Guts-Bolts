# Guts & Bolts Lua Scripting

Use this skill for Lua scripts and debugging in Guts & Bolts.

- Guts & Bolts uses Lua 5.4. Check the engine reference and available APIs instead of assuming Roblox-specific APIs exist.
- Determine whether code belongs in a Script, LocalScript, or ModuleScript and place it in the correct service.
- Keep server-authoritative gameplay, currency, inventory, and permissions on the server. Treat client input as untrusted.
- Use the engine's actual object model, events, and service APIs. Ask the engine documentation tool when uncertain.
- Make the smallest useful change, inspect the script after writing, then run a playtest and inspect Output/errors when available.
- Do not invent API names, claim code compiled, or claim a bug is fixed without a confirming result.
