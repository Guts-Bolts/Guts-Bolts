# Security policy

Thanks for helping keep Guts&Bolts and its players safe. If you found a way to
break into accounts, steal Bolts or items, run code on someone else's computer,
read private data, or anything like that, please tell us **privately** first so
we can fix it before anyone misuses it.

## How to report a problem

1. Go to the **Security** tab of this repository and press
   **Report a vulnerability**
   ([direct link](https://github.com/Guts-Bolts/Guts-Bolts/security/advisories/new)).
   Only the maintainers can see what you write there.
2. If that button isn't there, open a ticket on the website's **Support** page
   (https://gutsandbolts.net) and say it's a security problem. Leave out the
   details; we'll reach out and give you a private place to share them.

**Please don't** put security problems in public issues, pull requests,
Discord or in-game chat, and never post real passwords, keys or other people's
data, even your own test ones.

A good report says:

- what part it's in (website, online server, Player, Studio, Web Player,
  Studio MCP / connected apps, the updater, a script API...),
- the version you tested (shown in the app or on the Updates page),
- the steps to make it happen, and what an attacker could do with it.

## What happens next

- We try to answer within **3 days** to say we got it.
- Within **14 days** we try to tell you if we can reproduce it and how bad it is.
- We fix the worst problems first. When it's fixed we'll let you know, and
  we're happy to thank you by name in the update notes if you want.

Guts&Bolts is made by a small team, so sometimes it takes a bit longer, but
every report gets read.

## Which versions get fixes

| Part | Gets security fixes |
| --- | --- |
| The website and online server (gutsandbolts.net) | Yes, always the live version |
| Newest release of Player and Studio (0.7.x, latest) | Yes |
| Older releases | No, please update (the apps update themselves) |
| Your own forks or self-hosted servers built from this code | Fixes land here; you need to pull them in |

## What counts

In scope: this repository's code, gutsandbolts.net, the online server, the
Player and Studio apps, the Web Player, and the Studio MCP / connected-apps
sign-in.

Not in scope (please don't do these):

- attacks that just flood the servers (denial of service) or spam,
- tricking or phishing other players or staff,
- testing on other people's accounts, games or items. Use your own accounts,
- bugs in a game someone made with Guts&Bolts that only affect that game
  (tell its creator),
- problems in services we use (Cloudflare, Supabase, GitHub, Brevo): report
  those to them.

If you follow these rules and act in good faith, we won't take action against
you for your research.

## For maintainers

What the code already does to stay safe, and the checklist of settings to
check in GitHub and Cloudflare, is in
[docs/security-hardening.md](docs/security-hardening.md).
