// G&B's video player on the plain pages: /videos/<id> (what a shared link opens) and /embed/<id>
// (just the player, for other sites' frames). Both pages come from worker/seo.js with a normal
// <video data-gbp> in them, so the video still plays if this file doesn't load.
import { gbPlayer } from './videoplayer.js';

for (const video of document.querySelectorAll('video[data-gbp]')) {
  gbPlayer(video, { id: video.dataset.id, title: video.dataset.title, by: video.dataset.by, poster: video.getAttribute('poster') || '' });
}
