const axios = require('axios');

async function testJamendo(q) {
  try {
    const url = `https://api.jamendo.com/v3.0/tracks/?client_id=56d30c4d&format=json&limit=5&namesearch=${encodeURIComponent(q)}&audioformat=mp32`;
    console.log('Fetching Jamendo:', url);
    const res = await axios.get(url, { timeout: 6000 });
    console.log('Jamendo headers:', res.data.headers);
    if (res.data && res.data.results) {
      console.log('Jamendo tracks:', res.data.results.map(t => ({
        id: t.id,
        name: t.name,
        artist: t.artist_name,
        duration: t.duration,
        audio: t.audio
      })));

      if (res.data.results[0]) {
        const audioUrl = res.data.results[0].audio;
        console.log('Testing audio stream head for:', audioUrl);
        const testHead = await axios.head(audioUrl, { timeout: 5000 });
        console.log('Jamendo Audio Status:', testHead.status, 'Type:', testHead.headers['content-type'], 'Length:', testHead.headers['content-length']);
      }
    }
  } catch (e) {
    console.error('Jamendo error:', e.message);
  }
}

testJamendo('chill');
testJamendo('piano');
