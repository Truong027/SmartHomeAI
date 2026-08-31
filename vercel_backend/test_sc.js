const axios = require('axios');

async function getSoundCloudClientId() {
  try {
    const res = await axios.get('https://soundcloud.com', {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36' },
      timeout: 5000
    });
    const scriptUrls = [...res.data.matchAll(/src="(https:\/\/a-v2\.sndcdn\.com\/assets\/[^"]+\.js)"/g)].map(m => m[1]);
    console.log('Soundcloud scripts found:', scriptUrls.length);

    for (const sUrl of scriptUrls.slice(0, 5)) {
      try {
        const sRes = await axios.get(sUrl, { timeout: 4000 });
        const cMatch = sRes.data.match(/client_id:"([a-zA-Z0-9]{32})"/);
        if (cMatch) {
          console.log('Found SoundCloud client_id:', cMatch[1]);
          return cMatch[1];
        }
      } catch (e) {}
    }
  } catch (e) {
    console.error('SC error:', e.message);
  }
  return null;
}

async function testSC() {
  const clientId = await getSoundCloudClientId();
  if (clientId) {
    const searchUrl = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent('HIEUTHUHAI Khong The Say')}&client_id=${clientId}&limit=3`;
    const res = await axios.get(searchUrl);
    console.log('Search collection:', res.data.collection.map(t => ({
      title: t.title,
      duration: Math.round(t.duration / 1000) + 's',
      transcodings: t.media.transcodings.map(tc => ({ format: tc.format, url: tc.url }))
    })));

    // Fetch progressive or HLS stream
    if (res.data.collection[0]) {
      const track = res.data.collection[0];
      const prog = track.media.transcodings.find(tc => tc.format.protocol === 'progressive') || track.media.transcodings[0];
      if (prog) {
        const streamInfoRes = await axios.get(`${prog.url}?client_id=${clientId}`);
        console.log('Direct Stream URL:', streamInfoRes.data.url);
        const headRes = await axios.head(streamInfoRes.data.url);
        console.log('Stream Status:', headRes.status, 'Type:', headRes.headers['content-type'], 'Length:', headRes.headers['content-length']);
      }
    }
  }
}

testSC();
