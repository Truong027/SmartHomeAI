const axios = require('axios');

async function testNewspaperRSS() {
  const feeds = [
    { name: 'VnExpress', url: 'https://vnexpress.net/rss/tin-moi-nhat.rss' },
    { name: 'Tuổi Trẻ', url: 'https://tuoitre.vn/rss/thoi-su.rss' },
    { name: 'CafeF Chứng Khoán', url: 'https://cafef.vn/thi-truong-chung-khoan.rss' },
    { name: 'Thanh Niên', url: 'https://thanhnien.vn/rss/home.rss' }
  ];

  for (const f of feeds) {
    try {
      const res = await axios.get(f.url, {
        headers: { 'User-Agent': 'Mozilla/5.0' },
        timeout: 4000
      });
      const items = [...res.data.matchAll(/<item>[\s\S]*?<\/item>/g)];
      console.log(`[${f.name}] Status: ${res.status}, Articles: ${items.length}`);
      if (items.length > 0) {
        const itemXml = items[0][0];
        const title = (itemXml.match(/<title><!\[CDATA\[([\s\S]*?)\]\]><\/title>/) || itemXml.match(/<title>([\s\S]*?)<\/title>/) || [])[1] || '';
        const desc = (itemXml.match(/<description><!\[CDATA\[([\s\S]*?)\]\]><\/description>/) || itemXml.match(/<description>([\s\S]*?)<\/description>/) || [])[1] || '';
        const cleanDesc = desc.replace(/<[^>]*>/g, '').replace(/&nbsp;/g, ' ').trim();
        console.log(`   Sample: "${title}" - ${cleanDesc.substring(0, 100)}...`);
      }
    } catch(e) {
      console.log(`[${f.name}] Error:`, e.message);
    }
  }
}

testNewspaperRSS();
