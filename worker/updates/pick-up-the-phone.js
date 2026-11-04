export default {
  id: 'pick-up-the-phone', name: 'Pick Up the Phone', version: '0.7.9', time: 1793663516, tag: 'Website',
  summary: 'Chats live in the corner of every page now, you can voice call your friends, and games can talk to Lyreto.',
  items: [
    'A Chat panel sits in the bottom corner on a computer: search your friends, see every chat and party with its last message, and start a new one with the pencil',
    'On a phone, Chat has its own tab at the top',
    'Voice calls: press the phone button in any chat, and your friends get a ring with Answer and Decline (up to 6 people in a call)',
    'Do Not Disturb: calls stop ringing for you, and friends see a red dot instead of a green one',
    'The Lyreto gateway: a party\'s owner can link it to a Lyreto channel, so Guts&Bolts players and Lyreto users talk in the same chat',
    'Scripts: HttpService:GetAsync, PostAsync and RequestAsync now work on the server, for sites on Guts&Bolts\' list (lyreto.com for now), once the game\'s creator turns on Web requests',
    'Secrets for games: save an API key on the game\'s Configure page and use it with HttpService:GetSecret. The key never reaches whoever hosts the server',
    'Scripts can hear chat with Players.PlayerChatted and player.Chatted, post a grey line with TextChatService:DisplaySystemMessage, and read game.GameId',
  ],
};
