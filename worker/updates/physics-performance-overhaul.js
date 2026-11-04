export default {
  id: 'physics-performance-overhaul',
  name: 'The Laws of Physics Are Getting a Tune-Up', version: '0.7.9',
  time: 1793508000,
  tag: 'Engine',
  summary: 'Improve rigid-body stability during slow frames and make graphics and real-water quality settings much more useful on lower-powered devices.',
  items: [
    'Rigid-body simulation can now use up to twelve bounded substeps to keep fast impacts and stacked objects more stable when frame times spike.',
    'Low graphics quality now uses a smaller render scale, fewer lights, shorter render distance, and reduced post-processing.',
    'Medium graphics quality is tuned to reduce GPU load while keeping common visual effects enabled.',
    'Real-water particle budgets are reduced to 8,000 (Low), 20,000 (Medium), 60,000 (High), and 150,000 (Ultra).',
    'Lower water quality uses fewer smoothing passes as well as fewer particles, reducing both simulation and rendering work.',
    'New mobile installs start with the Low performance preset instead of Medium.',
    'Existing custom graphics settings are preserved; players can still choose higher quality when their device can handle it.'
  ],
};
