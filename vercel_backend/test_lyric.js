const axios = require('axios');

async function testLyricSearch() {
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  const queries = [
    'cho toi nguoi im lang gap nguoi hay noi hieu thu hai.',
    'nguoi im lang gap nguoi hay noi hieu thu hai',
    'nguoi im lang gap nguoi hay noi',
    'ngu mot minh hieuthuhai'
  ];

  for (const q of queries) {
    const clean = q.replace(/[.,\/#!$%\^&\*;:{}=\-_`~()]/g, '')
                   .replace(/^(cho toi|cho minh|cho nghe|mo cho toi|bat cho toi|phat cho toi)\s+/gi, '')
                   .trim();
    console.log(`\n=== Raw: "${q}" -> Clean: "${clean}" ===`);
    const url = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent(clean)}&client_id=${clientId}&limit=6`;
    const res = await axios.get(url);
    for (const t of res.data.collection) {
      const prog = t.media && t.media.transcodings ? t.media.transcodings.find(tc => tc.format.protocol === 'progressive') : null;
      console.log(`- "${t.title}" (${Math.round(t.duration / 1000)}s) | Has Prog: ${!!prog}`);
    }
  }
}

testLyricSearch();
