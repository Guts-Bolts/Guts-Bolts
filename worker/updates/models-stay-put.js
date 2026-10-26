export default {
  id: 'models-stay-put', name: 'Spin Without the Wander', version: '0.7.6', time: 1793041200, tag: 'Fix',
  summary: 'Turning a model with the Rotate handles no longer makes it slowly slide away from where it was.',
  items: [
    'Models now turn around one fixed middle point for the whole drag, instead of a middle that shifted a little every frame.',
    'Wiggling the Rotate handles back and forth puts the model back exactly where it started.',
  ],
};
