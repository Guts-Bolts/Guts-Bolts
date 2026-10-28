export default {
  id: 'updater-no-loop', title: 'Updater that can\'t get stuck in a restart loop', status: 'done', version: '0.7.8', pr: 205, time: 1793222356,
  text: 'When an update failed, Studio and the Player could close and reopen over and over without ever updating. Now a failed update is remembered, the app says what went wrong, and you can download the new version yourself.',
};
