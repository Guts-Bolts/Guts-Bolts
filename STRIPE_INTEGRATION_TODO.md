# Stripe integration: what's left to do

Boltworks Club card payments use Stripe Checkout's embedded payment form. The code is in,
but nothing can be charged until the settings below are filled in. Until then the payment page
says "Payments aren't set up yet."

## Values to Replace

The following values are placeholders and must be updated before going live.

**Files containing placeholders:**
- [worker/stripe.js](worker/stripe.js)

| Field | Current Value | What to Set |
|-------|--------------|-------------|
| mode | subscription | Boltworks Club is a monthly membership, so it's `"subscription"`. Use `"payment"` only for a one-time charge (like a Bolts pack). |
| line_items[].price | `STRIPE_PRICE_ID` setting (empty) | Your real Stripe Price ID (`price_...`) for the Boltworks Club monthly price, from the Dashboard (https://dashboard.stripe.com/prices). Put it in the Cloudflare setting `STRIPE_PRICE_ID`, not in the code. |

## Configured Parameters

These parameters were configured in Checkout Studio and are already set correctly.

**Files containing these parameters:**
- [worker/stripe.js](worker/stripe.js)
- [website/checkout.js](website/checkout.js) (the form's look, `appearance`)

| Parameter | Value |
|-----------|-------|
| ui_mode | form |
| billing_address_collection | auto |
| phone_number_collection | { enabled: false } |
| automatic_tax | { enabled: false } |
| payment_method_collection | always (only sent because mode is subscription) |
| submit_type | auto |
| integration_identifier | custom_embedded_web_0001 |
| Stripe API version | 2026-03-25.dahlia |

## Setup

This site runs on Cloudflare Workers, so there's no `.env` file in production. Settings go in the
Cloudflare dashboard: **Workers & Pages > project-guts-and-bolts > Settings > Variables and Secrets**.

| Name | Type | Where to get it |
|------|------|-----------------|
| `STRIPE_SECRET_KEY` | **Secret** (click "Encrypt") | Stripe Dashboard > Developers > API keys > Secret key (`sk_test_...` while testing, `sk_live_...` when live). Never put it in the code or show anyone. |
| `STRIPE_PUBLISHABLE_KEY` | Plain variable | Same page, Publishable key (`pk_test_...` / `pk_live_...`). This one is safe to be public. |
| `STRIPE_PRICE_ID` | Plain variable | Stripe Dashboard > Product catalog > add product "Boltworks Club", recurring monthly price > copy its `price_...` ID. |
| `STRIPE_WEBHOOK_SECRET` | **Secret** | Stripe Dashboard > Developers > Webhooks > Add endpoint: URL `https://gutsandbolts.net/api/stripe/webhook`, event `checkout.session.completed`. Copy its signing secret (`whsec_...`). |

To try it on your own computer with `wrangler dev`, put the same four names in a `.dev.vars` file
(it's ignored by git, so it never gets pushed).

No new packages are needed: the server talks to Stripe's API directly with `fetch`, and the page
loads Stripe.js from `https://js.stripe.com/dahlia/stripe.js` (it must always come from Stripe).

## New files

```
worker/stripe.js                    the server side: makes Checkout Sessions, checks the webhook
website/checkout.html               the "Join Boltworks Club" payment page
website/checkout.js                 shows Stripe's payment form on that page
worker/test/stripe-checkout.test.mjs  checks
STRIPE_INTEGRATION_TODO.md          this file
```

Also changed: `worker/index.js` (the three `/api/stripe/...` addresses), `worker/security.js`
(lets Stripe.js and its frames load), `wrangler.jsonc` (the payment page gets the security headers).

## How it works

1. Someone opens `https://gutsandbolts.net/checkout?user=<their user number>`.
2. The page asks `/api/stripe/config` for the publishable key, then `POST /api/stripe/checkout`.
3. The server asks Stripe for a Checkout Session (subscription, Boltworks Club price) and sends back
   only its `client_secret`. The user number is saved on the payment (`client_reference_id`
   `gb-user-<number>` and `metadata.gb_user`).
4. Stripe draws the card form in its own frame. Card numbers never touch Guts&Bolts.
5. After paying, they come back to the Bolts page.
6. Stripe calls `/api/stripe/webhook`. The server checks Stripe's signature and logs the payment.

## Testing

Use your **test** keys (`sk_test_`, `pk_test_`). No real money moves in test mode.

| Card number | What happens |
|-------------|--------------|
| 4242 4242 4242 4242 | Payment works |
| 4000 0025 0000 3155 | Asks for 3D Secure (bank check) |
| 4000 0000 0000 9995 | Declined (not enough money) |

Use any future date, any 3-digit CVC and any postcode. More: https://docs.stripe.com/testing

## Next steps (before charging real people)

- **Giving the club tier automatically.** Club tiers are signed by the official staff account, which the
  server can't do on its own. For now: when a payment comes in, look at its `gb_user` in Stripe and
  give the tier by hand on the Staff page. Automating this needs a decision on how the server is
  allowed to give paid tiers (and cancelling should take the tier away, via the
  `customer.subscription.deleted` webhook event).
- **A "Join" button.** The Boltworks Club box on the Bolts page still says "coming later". Link it to
  `/checkout?user=<user number>` when you're ready.
- **Terms of Service.** They currently say there are no paid subscriptions. Update them (prices,
  renewals, cancelling, refunds) before going live.
- **Prices and perks.** Decide the tiers (BWC / TBC / OBC). Each tier would need its own Price ID.
- **Switch to live keys** (`sk_live_`, `pk_live_`, a live webhook) once everything works in test mode.

## Resources

- https://support.stripe.com
- https://docs.stripe.com/mcp
