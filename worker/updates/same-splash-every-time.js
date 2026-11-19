export default {
  id: 'same-splash-every-time', name: 'Same Splash Every Time', version: '0.8.1', time: 1795015496, tag: 'Fix',
  summary: 'Flowing water now behaves the same way every time, so water slides stop randomly throwing you off.',
  items: [
    'Water drops are worked out on all your CPU cores at once. In the last step, one core could move a drop while another core was still reading where it was',
    'So the same water came out a little different every time, and now and then a water slide ride went wrong and dropped the rider off the side',
    'Each drop now waits until every core is done before it moves, so the water is the same on every run and every computer core count',
    'This also fixes the Linux build check, which failed now and then on the Mega Water Slide test',
  ],
};
