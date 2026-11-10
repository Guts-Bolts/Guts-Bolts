export default {
  id: 'checkout-plain-errors', name: 'Checkout Says Why', time: 1794322800, tag: 'Website',
  summary: 'The Boltworks Club payment page now says exactly what went wrong instead of a vague error.',
  items: [
    'If a payment setting is missing, the page names it, so it\'s quick to fix',
    'If Stripe turns the payment down (like a test price used with a live key), its own reason is shown',
  ],
};
