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
| `STRIPE_WEBHOOK_SECRET` | **Secret** | Stripe Dashboard > Developers > Webhooks > Add endpoint: URL `https://gutsandbolts.net/api/stripe/webhook`, events `checkout.session.completed`, `checkout.session.async_payment_succeeded`, `customer.subscription.created`, `customer.subscription.updated`, `customer.subscription.deleted`, `charge.refunded` and `charge.dispute.created` (the last two take refunded or charged-back Bolts back). Copy its signing secret (`whsec_...`). |

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

## More ways to pay (no code needed)

The payment form shows every payment method that's switched on in Stripe, because the code never
picks a list of methods. To add more:

1. Stripe Dashboard > **Settings > Payment methods**. Turn on the ones you want. Good ones to start:
   **Apple Pay**, **Google Pay**, **Link** (Stripe's saved-card button), **Cash App Pay**, **Amazon Pay**.
   Some aren't offered in every country; the page says which ones your account can use.
2. **Apple Pay only:** Stripe Dashboard > Settings > Payment methods > **Payment method domains** >
   Add `gutsandbolts.net`. Without that, the Apple Pay button never shows up on iPhones and Macs.
3. **Crypto through Stripe:** if your account shows **Stablecoins and crypto** on that page, turning it
   on lets people pay with stablecoins (USDC and similar, which are pegged to the US dollar). Bitcoin checkout is handled separately by Blockonomics (below).
4. **Bank payments** (like ACH in the US) also work: the Bolts or the tier only arrive once the bank
   payment clears, which can take a few days. Skip "buy now, pay later" ones (Klarna, Afterpay) for
   small Bolts packs; they cost more per payment.

Each method has its own fee; Stripe shows it on that page.

## Bitcoin (through Blockonomics)

Bolts packs (not memberships) can be paid for with Bitcoin. The checkout uses Blockonomics Payment Intents and its hosted checkout widget. Prices are set in US dollars; Blockonomics quotes the exact crypto amount and address.

1. Create a Blockonomics account and a Store in the dashboard. Add a BTC wallet and enable Bitcoin for the store.
2. Set the Store callback URL to `https://gutsandbolts.net/api/bitcoin/webhook?secret=YOUR_RANDOM_SECRET`. Use a long random secret and keep it private. Set the same value in Cloudflare as the encrypted secret `BLOCKONOMICS_CALLBACK_SECRET`.
3. Create an API key in Blockonomics Stores and add it to Cloudflare as the encrypted secret `BLOCKONOMICS_API_KEY`.
4. If your account has more than one store, set `BLOCKONOMICS_MATCH_CALLBACK` in Cloudflare to the unique callback URL/domain fragment used for this store, as supported by Blockonomics' API. Leave it unset for a single store.
5. The Bolts page shows Bitcoin purchase buttons only when `BLOCKONOMICS_API_KEY` is present.

Checkout creates a USD payment intent and routes the buyer to our Blockonomics checkout widget page. The callback contains no trusted payment status: Guts & Bolts fetches the intent from Blockonomics and only credits Bolts if the intent is `paid`, in USD, for the exact pack amount. Credits use an idempotency reference, so duplicate callbacks or checks do not credit twice. A customer returning to the Bolts page also checks any recent pending intent IDs in their browser.

Blockonomics' widget can show supported crypto choices for the store. For this integration, configure the store for BTC only unless we deliberately expand the payment options later. Payment confirmations can take time; Bolts are credited only after Blockonomics confirms the intent. Bitcoin transactions are generally irreversible, so review the pack before sending.
## Refunds and chargebacks (cards and wallets)

If you refund a Bolts pack in the Stripe Dashboard, the server takes back the same share of the Bolts
(a full refund takes them all, a half refund takes half). If the buyer's bank charges it back (a
dispute), the whole pack is taken back. It never takes back more than the pack gave, and the balance
can go below zero if they already spent them. If you **win** a dispute, give the Bolts back by hand
(staff tools). Memberships are left to Stripe: refunding one doesn't end it, so cancel it in the
Dashboard too if you want the badge gone. (Bolts bought before this update aren't linked to their
payment, so refunds of those don't take anything back; do that by hand if needed.)

## Before selling with crypto: check the legal side

This isn't legal advice, just things to look at (or ask someone who knows): the Terms now mention
Bitcoin through Blockonomics and that Bitcoin payments can't be reversed, and the Privacy Policy lists
Blockonomics. Crypto sales can count differently for taxes and bookkeeping (you get Bitcoin, or dollars
from Blockonomics, instead of a card payment), and some places have their own rules about taking crypto.

## New files

```
worker/stripe.js                    the server side: makes Checkout Sessions, checks the webhook
website/checkout.html               the "Join Boltworks Club" payment page
website/checkout.js                 shows Stripe's payment form on that page
worker/test/stripe-checkout.test.mjs  checks
worker/club-pay.js                  the game server's side: gives tiers and Bolts, Manage membership
worker/test/club-payments.test.mjs  checks for tiers and packs
worker/bitcoin.js                   Bitcoin through Blockonomics: charges, webhook, check
worker/test/bitcoin-payments.test.mjs  checks for Bitcoin, refunds and chargebacks
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
