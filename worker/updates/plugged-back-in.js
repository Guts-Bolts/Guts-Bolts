export default {
  id: 'plugged-back-in', name: 'Plugged Back In', time: 1793648338, tag: 'Website',
  summary: 'The website can finally talk to its Supabase database.',
  items: [
    'Every message the server sent to Supabase was being thrown away before it left, because of one setting Cloudflare does not support',
    'Auction news, forum activity and the notification history now actually get saved and shown',
  ],
};
