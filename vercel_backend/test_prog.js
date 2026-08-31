const axios = require('axios');

async function testProgressiveOnly(q) {
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  const url = 'https://api-v2.soundcloud.com/search/tracks?q=' + encodeURIComponent(q) + '&client_id=' + clientId + '&limit=10';
  const res = await axios.get(url);
  for (const t of res.data.collection) {
    const prog = t.media && t.media.transcodings ? t.media.transcodings.find(tc => tc.format.protocol === 'progressive') : null;
    console.log(`Track: "${t.title}" (${Math.round(t.duration / 1000)}s) | Has Progressive MP3: ${!!prog}`);
    if (prog) {
      const sInfo = await axios.get(`${prog.url}?client_id=${clientId}`);
      console.log('  Direct MP3 Stream:', sInfo.data.url.substring(0, 80) + '...');
    }
  }
}

testProgressiveOnly('Khong the say');
testProgressiveOnly('Cat doi noi sau');
testProgressiveOnly('Am tham ben em');
