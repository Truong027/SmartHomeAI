const axios = require('axios');

async function testInvidiousAudio(q) {
  try {
    const sUrl = 'https://www.youtube.com/results?search_query=' + encodeURIComponent(q + ' official audio');
    const sRes = await axios.get(sUrl, {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' },
      timeout: 5000
    });
    const m = sRes.data.match(/"videoId":"([a-zA-Z0-9_-]{11})"/);
    if (m) {
      const vid = m[1];
      console.log('Found video ID:', vid);

      const invidiousInstances = [
        'https://inv.tux.pizza',
        'https://invidious.projectsegfau.lt',
        'https://vid.puffyan.us',
        'https://invidious.flokinet.to',
        'https://iv.melmac.space',
        'https://invidious.schenkl.is'
      ];
      for (const inst of invidiousInstances) {
        try {
          const infoUrl = inst + '/api/v1/videos/' + vid;
          const vRes = await axios.get(infoUrl, { timeout: 4000 });
          if (vRes.data && vRes.data.adaptiveFormats) {
            const audioFormats = vRes.data.adaptiveFormats.filter(f => f.type && f.type.startsWith('audio'));
            console.log('Audio formats found from ' + inst + ':', audioFormats.length);
            if (audioFormats.length > 0) {
              const bestAudio = audioFormats[0];
              console.log('Best audio:', bestAudio.type, 'Bitrate:', bestAudio.bitrate);
              console.log('Audio stream URL:', bestAudio.url.substring(0, 100) + '...');
              return bestAudio.url;
            }
          }
        } catch (err) {
          console.log(inst + ' error:', err.message);
        }
      }
    }
  } catch (e) {
    console.log('Error:', e.message);
  }
  return null;
}

testInvidiousAudio('Không thể say HIEUTHUHAI');
