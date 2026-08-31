const axios = require('axios');

async function testZingAndOthers(query) {
  console.log('Testing full song resolvers for:', query);

  // 1. Zing MP3 public search HTML / Web API
  try {
    const zUrl = 'https://mp3.zing.vn/tim-kiem/bai-hat.html?q=' + encodeURIComponent(query);
    const zRes = await axios.get(zUrl, {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' },
      timeout: 5000
    });
    console.log('Zing HTML length:', zRes.data.length);
  } catch (e) {
    console.log('Zing error:', e.message);
  }

  // 2. Chiasenhac direct web search
  try {
    const csnUrl = 'https://chiasenhac.vn/tim-kiem?q=' + encodeURIComponent(query);
    const csnRes = await axios.get(csnUrl, {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' },
      timeout: 5000
    });
    console.log('CSN HTML length:', csnRes.data.length);
    // Find song link
    const songMatch = csnRes.data.match(/href="(https:\/\/chiasenhac\.vn\/mp3\/[^"]+)"/);
    if (songMatch) {
      console.log('Found CSN song page:', songMatch[1]);
    }
  } catch (e) {
    console.log('CSN error:', e.message);
  }

  // 3. YouTube Music Web Search
  try {
    const ytUrl = 'https://www.youtube.com/results?search_query=' + encodeURIComponent(query + ' audio');
    const ytRes = await axios.get(ytUrl, {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' },
      timeout: 5000
    });
    const videoMatches = [...ytRes.data.matchAll(/"videoId":"([a-zA-Z0-9_-]{11})"/g)];
    if (videoMatches.length > 0) {
      console.log('Found YouTube Video IDs:', videoMatches.slice(0, 3).map(m => m[1]));
    }
  } catch (e) {
    console.log('YT error:', e.message);
  }
}

testZingAndOthers('Không thể say HIEUTHUHAI');
