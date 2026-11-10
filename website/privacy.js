// The Privacy page's "Change my cookie answer" button (the answer is kept by app/ads.js).
import { forgetCookieChoice } from './app/ads.js';

const button = document.getElementById('cookie-reset');
if (button) button.addEventListener('click', () => {
  forgetCookieChoice();
  document.getElementById('cookie-reset-done').hidden = false;
});
