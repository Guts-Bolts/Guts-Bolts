// Shared by the legal pages (privacy, cookies, copyright, contact). Loading app/ads.js makes
// every "Privacy choices" link (data-privacy-choices) open the choices panel, and starts
// Google's Privacy & messaging tag once it's switched on (worker/ads.js ADS_GOOGLE_CONSENT).
import { forgetCookieChoice, startGoogleMessages } from './app/ads.js';

startGoogleMessages(null);

// The Privacy page's "Change my cookie answer" button.
const button = document.getElementById('cookie-reset');
if (button) button.addEventListener('click', () => {
  forgetCookieChoice();
  document.getElementById('cookie-reset-done').hidden = false;
});
