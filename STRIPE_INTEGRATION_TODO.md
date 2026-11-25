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
| `STRIPE_PRICE_...` | Plain variables | One per thing on sale (table below). Stripe Dashboard > Product catalog > click the product > click the price > copy its `price_...` ID. |
| `STRIPE_WEBHOOK_SECRET` | **Secret** | Stripe Dashboard > Developers > Webhooks > Add endpoint: URL `https://gutsandbolts.net/api/stripe/webhook`, events `checkout.session.completed`, `checkout.session.async_payment_succeeded`, `customer.subscription.created`, `customer.subscription.updated` and `customer.subscription.deleted`. Copy its signing secret (`whsec_...`). |

### The price settings

Only the ones you fill in go on sale; the rest stay hidden. The website reads the real price from
Stripe, so changing a price in Stripe changes it on the site (within 10 minutes). The number of
Bolts in each pack is set in `worker/stripe.js` (`ITEMS`), not by the product's name in Stripe.

| Setting | Stripe product | Kind |
|---------|----------------|------|
| `STRIPE_PRICE_BWC_MONTH` | Boltworks Club, monthly price | subscription (the old `STRIPE_PRICE_ID` still works for this one) |
| `STRIPE_PRICE_BWC_YEAR` | Boltworks Club, yearly price | subscription |
| `STRIPE_PRICE_TBC_MONTH` | Turbo Boltworks Club, monthly price | subscription |
| `STRIPE_PRICE_TBC_YEAR` | Turbo Boltworks Club, yearly price | subscription |
| `STRIPE_PRICE_OBC_MONTH` | Overclocked Boltworks Club, monthly price | subscription |
| `STRIPE_PRICE_OBC_YEAR` | Overclocked Boltworks Club, yearly price | subscription |
| `STRIPE_PRICE_BOLTS_200` | 200 Bolts | one-time |
| `STRIPE_PRICE_BOLTS_500` | 500 Bolts | one-time |
| `STRIPE_PRICE_BOLTS_1100` | 1100 Bolts | one-time |
| `STRIPE_PRICE_BOLTS_2400` | 2400 Bolts | one-time |
| `STRIPE_PRICE_BOLTS_6500` | 6500 Bolts | one-time |

### The early-list discount

People who pressed "Let me know" on the Bolts page before memberships launched (2026-11-24 23:53 UTC)
get a discount for good: on every membership they start, renewals included. The list is closed now,
so nobody new can join it. To turn it on:

1. Stripe Dashboard > Product catalog > **Coupons** > Create coupon, with duration **Forever**
   (it stays on every renewal). Leave "Customer-facing code" off: nobody types it in.
2. Copy the coupon's ID and put it in Cloudflare as a plain variable named `STRIPE_COUPON_EARLY`.

The server checks who is on the early list and adds the coupon at checkout. Remove the setting to
stop adding it to new memberships (ones that already have it keep it). Bolts packs never get it.

### Bans and deleted accounts

A ban for good, or someone deleting their account, cancels their Stripe membership by itself (no
more charges, no refund for the rest of the month). A ban for some days doesn't. If
`STRIPE_SECRET_KEY` isn't set, the server logs which subscription needs cancelling by hand.

### The "Manage membership" page

Stripe Dashboard > Settings > Billing > **Customer portal**: turn it on, allow cancelling and
updating the card, and (to let people switch tier) allow "switch plans" with the three club products.
The Manage membership button on the Bolts page opens this page.

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
worker/club-pay.js                  the game server's side: gives tiers and Bolts, Manage membership
worker/test/club-payments.test.mjs  checks for tiers and packs
STRIPE_INTEGRATION_TODO.md          this file
```

Also changed: `worker/index.js` (the three `/api/stripe/...` addresses), `worker/security.js`
(lets Stripe.js and its frames load), `wrangler.jsonc` (the payment page gets the security headers).

## How it works

1. The Bolts page asks `/api/stripe/prices` what's on sale. Until a price is set, it shows the old
   "coming later" teaser.
2. A tier or pack button opens `/checkout.html?item=<item>&user=<their user number>`.
3. The page asks `/api/stripe/config` for the publishable key, then `POST /api/stripe/checkout`.
4. The server asks Stripe for a Checkout Session (a subscription for tiers, a one-time payment for
   Bolts) and sends back only its `client_secret`. The user number and item are saved on the payment
   (`metadata.gb_user`, `metadata.gb_item`).
5. Stripe draws the card form in its own frame. Card numbers never touch Guts&Bolts.
6. Stripe calls `/api/stripe/webhook`. The server checks Stripe's signature, then hands the job to the
   game server (`worker/club-pay.js`): it adds the Bolts once (the payment id is kept, so a repeated
   message does nothing), or turns the club tier on, changes it, or takes it away. Staff-given tiers
   keep working too.

## Testing

Use your **test** keys (`sk_test_`, `pk_test_`). No real money moves in test mode.

| Card number | What happens |
|-------------|--------------|
| 4242 4242 4242 4242 | Payment works |
| 4000 0025 0000 3155 | Asks for 3D Secure (bank check) |
| 4000 0000 0000 9995 | Declined (not enough money) |

Use any future date, any 3-digit CVC and any postcode. More: https://docs.stripe.com/testing

## Next steps (before charging real people)

- **Early-list coupon.** Make it and set `STRIPE_COUPON_EARLY` (see above).
- **Taxes.** `automatic_tax` is off. Turn on Stripe Tax once there are real sales.
- **Switch to live keys** (`sk_live_`, `pk_live_`, a live webhook) once everything works in test mode.

## Resources

- https://support.stripe.com
- https://docs.stripe.com/mcp
