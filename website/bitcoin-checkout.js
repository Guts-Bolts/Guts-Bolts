// Initializes the Blockonomics payment widget on the standalone Bolts checkout page.
(() => {
  const id = new URLSearchParams(location.search).get('intent') || '';
  const area = document.getElementById('payment_area');
  const status = document.getElementById('status');
  if (!/^pi_[A-Za-z0-9]{8,80}$/.test(id)) {
    area.textContent = 'This checkout link is invalid. Please start again from the Bolts page.';
    return;
  }
  if (!window.Blockonomics || typeof window.Blockonomics.checkout !== 'function') {
    area.textContent = 'The payment checkout did not load. Please refresh or return to your Bolts page.';
    return;
  }
  window.Blockonomics.checkout({
    msg_area: 'payment_area',
    intent_id: id,
    onPaymentDetected: () => { status.textContent = 'Payment detected. Waiting for Blockonomics confirmation before Bolts are added.'; }
  });
})();
