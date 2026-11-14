// The Boltworks Club payment page (checkout.html): Stripe's embedded payment form.
// The server makes the Checkout Session (worker/stripe.js); Stripe draws the card form in its
// own frame, so card numbers never touch Guts&Bolts. Open it as checkout.html?user=<user number>.

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

async function start() {
  if (typeof window.Stripe !== 'function') { say('Couldn\'t load the payment form. Is something blocking stripe.com?', true); return; }
  const config = await fetch('/api/stripe/config').then((r) => r.json()).catch(() => ({}));
  if (!config.publishableKey) { say('Payments aren\'t set up yet. Check back soon!', true); return; }
  const stripe = window.Stripe(config.publishableKey);

  const userId = new URLSearchParams(location.search).get('user') || '';
  const clientSecret = fetch('/api/stripe/checkout', {
    method: 'POST',
    headers: { 'content-type': 'application/json' },
    body: JSON.stringify({ userId }),
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
