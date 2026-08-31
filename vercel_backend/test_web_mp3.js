const axios = require('axios');

async function testFreeMp3Sites(q) {
  console.log('Testing Vietnamese full MP3 sites for:', q);

  // 1. Test nhac.vn
  try {
    const nUrl = 'https://nhac.vn/tim-kiem?q=' + encodeURIComponent(q);
    const nRes = await axios.get(nUrl, { timeout: 4000, headers: { 'User-Agent': 'Mozilla/5.0' } });
    console.log('nhac.vn status:', nRes.status, 'HTML length:', nRes.data.length);
    const links = [...nRes.data.matchAll(/href="([^"]+bai-hat[^"]+)"/g)].map(m => m[1]);
    console.log('nhac.vn links:', links.slice(0, 3));
  } catch (e) {
    console.log('nhac.vn error:', e.message);
  }

  // 2. Test y2mate / ytmp3 / youtube audio converter APIs
  try {
    const ytConv = 'https://api.v2.emilyx.in/api/yt';
    // or ytmp3 APIs
  } catch (e) {}

  // 3. Test Audiomack free search without key
  try {
    const amUrl = 'https://audiomack.com/search?q=' + encodeURIComponent(q);
    const amRes = await axios.get(amUrl, { timeout: 4000, headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64)' } });
    console.log('Audiomack web HTML:', amRes.status, 'length:', amRes.data.length);
    const rawMatch = amRes.data.match(/"stream_url":"([^"]+)"/);
    if (rawMatch) console.log('Audiomack stream:', rawMatch[1]);
  } catch (e) {
    console.log('Audiomack error:', e.message);
  }
}

testFreeMp3Sites('Không thể say HIEUTHUHAI');
testFreeMp3Sites('Cắt đôi nỗi sầu');
