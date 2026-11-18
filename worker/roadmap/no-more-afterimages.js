export default {
  id: 'no-more-afterimages', title: 'Fix afterimages behind moving characters', status: 'working', version: '0.8.1', pr: 387, time: 1794972000,
  text: 'The temporal upscaler left a faint afterimage where characters had just been. It now checks how far away each pixel was last frame and drops the old frames where something moved.',
};
