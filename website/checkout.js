// The Boltworks Club payment page (checkout.html): Stripe's embedded payment form.
// The server makes the Checkout Session (worker/stripe.js); Stripe draws the card form in its
// own frame, so card numbers never touch Guts&Bolts.
// Open it as checkout.html?user=<user number>&item=<what> (items: worker/stripe.js ITEMS, like
// tbc_month or bolts_500). With no item it's the plain monthly Boltworks Club.

const appearance = {
  theme: 'stripe',
  labels: 'auto',
  inputs: 'spaced',
  variables: {
    borderRadius: '4px',
    colorBackground: '#ffffff',
    colorDanger: '#df1b41',
    colorPrimary: '#0570de',
    colorSuccess: '#00c853',
    colorText: '#30313d',
    fontFamily: 'default',
    fontSizeBase: '16px',
    spacingUnit: '4px',
  },
};

const status = document.getElementById('checkout-status');
const say = (text, bad) => { status.textContent = text; status.className = bad ? 'error' : 'muted'; };

// The heading says what's being bought.
const TIERS = { bwc: 'Boltworks Club', tbc: 'Turbo Boltworks Club', obc: 'Overclocked Boltworks Club' };
function describe(item) {
  const bolts = /^bolts_(\d+)$/.exec(item), club = /^(bwc|tbc|obc)_(month|year)$/.exec(item);
  const title = document.getElementById('checkout-title'), what = document.getElementById('checkout-what');
  if (bolts) {
    document.title = 'Buy Bolts - Guts&Bolts';
    title.textContent = 'Buy ' + Number(bolts[1]).toLocaleString('en-US') + ' Bolts';
    what.textContent = 'Paid once. The Bolts go straight into your wallet.';
  } else if (club) {
    document.title = 'Join ' + TIERS[club[1]] + ' - Guts&Bolts';
    title.textContent = 'Join ' + TIERS[club[1]];
    what.textContent = (club[2] === 'year' ? 'Paid once a year' : 'Paid once a month') + ': no ads, a club badge next to your name, and more perks. Cancel any time.';
  }
}

async function start() {
  if (typeof window.Stripe !== 'function') { say('Couldn\'t load the payment form. Is something blocking stripe.com?', true); return; }
  const config = await fetch('/api/stripe/config').then((r) => r.json()).catch(() => ({}));
  if (!config.publishableKey) { say('Payments aren\'t set up yet. Check back soon!', true); return; }
  const stripe = window.Stripe(config.publishableKey);

  const q = new URLSearchParams(location.search);
  const userId = q.get('user') || '';
  const item = q.get('item') || 'bwc_month';
  describe(item);
  const clientSecret = fetch('/api/stripe/checkout', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ userId, item }),
  }).then(async (r) => {
    const j = await r.json().catch(() => ({}));
    if (!r.ok || !j.client_secret) throw new Error(j.error || 'Couldn\'t start the payment.');
    return j.client_secret;
  });
  let serverError = '';
  clientSecret.catch((e) => { serverError = e.message; say(serverError, true); });

  const checkout = stripe.initCheckoutFormSdk({ clientSecret, appearance });
  const form = checkout.createForm({ layout: 'expanded' });
  form.mount('#checkout-form');

  const loadActionsResult = await checkout.loadActions();
  if (loadActionsResult.type === 'success') {
    say('');
    form.on('confirm', async (event) => {
      try {
        await loadActionsResult.actions.confirm({ formConfirmEvent: event });
      } catch (error) {
        console.error('Payment confirmation error:', error);
        say('The payment didn\'t go through. Check your card details and try again.', true);
      }
    });
  } else {
    // Show what the server said (like which setting is missing), not just "it failed".
    document.getElementById('checkout-form').hidden = true;
    say(serverError || 'Couldn\'t load the payment form. Try again in a bit.', true);
  }
}

start().catch((e) => say(String(e && e.message || e), true));
