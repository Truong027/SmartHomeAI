const axios = require('axios');

async function findClientId() {
  const res = await axios.get('https://soundcloud.com');
  const urls = [...res.data.matchAll(/src="([^"]+)"/g)].map(m => m[1]).filter(u => u.includes('sndcdn.com'));
  console.log('Script URLs:', urls);
  for (const u of urls) {
    try {
      const js = (await axios.get(u)).data;
      const matches = [...js.matchAll(/client_id[:=]"([a-zA-Z0-9]{32})"/g)];
      if (matches.length > 0) {
        console.log('FOUND:', matches.map(m => m[1]));
        return matches[0][1];
      }
      // Also look for client_id=...
      const m2 = js.match(/client_id=([a-zA-Z0-9]{32})/);
      if (m2) {
        console.log('FOUND M2:', m2[1]);
        return m2[1];
      }
    } catch (e) {
      console.log('Error fetching script:', e.message);
    }
  }
}
findClientId();
