const axios = require('axios');

const LIVE_KEYWORDS = [
  'live', 'liveshow', 'live performance', 'live at', 'live in', 'fancam', 
  'concert', 'sân khấu', 'san khau', 'khán giả', 'khan gia', 'hát live', 
  'hat live', 'song 24', 'genfest', 'rap viet live', 'crowd', 'audience', 
  'camcorder', 'record live', 'suy live', 'live version', 'show', 'audioset live'
];

function isLiveTrack(title) {
  const lower = title.toLowerCase();
  return LIVE_KEYWORDS.some(kw => lower.includes(kw));
}

function scoreTrack(track, cleanQuery) {
  const title = track.title.toLowerCase();
  const queryWords = cleanQuery.toLowerCase().split(/\s+/).filter(w => w.length > 1);
  let score = 0;

  // Trừ điểm nặng hoặc loại nếu dính live
  if (isLiveTrack(title)) return -9999;

  // Điểm khớp từ khóa tìm kiếm
  for (const word of queryWords) {
    if (title.includes(word)) score += 30;
  }

  // Điểm ưu tiên bản Official / Studio / Original
  if (title.includes('official') || title.includes('audio') || title.includes('original') || title.includes('studio') || title.includes('master')) {
    score += 40;
  }

  // Trừ điểm nhẹ nếu là cover/karaoke/beat/remix (nếu người dùng không tìm kiếm remix)
  if (!cleanQuery.toLowerCase().includes('cover') && title.includes('cover')) score -= 25;
  if (!cleanQuery.toLowerCase().includes('karaoke') && title.includes('karaoke')) score -= 60;
  if (!cleanQuery.toLowerCase().includes('beat') && (title.includes('beat') || title.includes('instrumental'))) score -= 50;
  if (!cleanQuery.toLowerCase().includes('remix') && (title.includes('remix') || title.includes('rmx') || title.includes('mixset'))) score -= 15;

  // Thời lượng chuẩn 2.5 đến 5.5 phút
  const durSec = Math.round(track.duration / 1000);
  if (durSec >= 150 && durSec <= 330) score += 20;

  return score;
}

async function testQuery(cleanQuery) {
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  console.log(`\n========================================`);
  console.log(`🔍 Searching Studio Audio for: "${cleanQuery}"`);

  // Tìm kiếm rộng 15 bài
  const sUrl = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent(cleanQuery)}&client_id=${clientId}&limit=15`;
  const res = await axios.get(sUrl);

  const collection = res.data.collection || [];
  const candidates = collection.filter(t => {
    const durSec = Math.round(t.duration / 1000);
    const hasMedia = t.media && t.media.transcodings && t.media.transcodings.length > 0;
    return hasMedia && durSec >= 80 && durSec <= 600 && !isLiveTrack(t.title);
  });

  candidates.sort((a, b) => scoreTrack(b, cleanQuery) - scoreTrack(a, cleanQuery));

  console.log(`Top ${Math.min(4, candidates.length)} Studio Candidates:`);
  for (let i = 0; i < Math.min(4, candidates.length); i++) {
    const c = candidates[i];
    console.log(`  #${i + 1} [Score: ${scoreTrack(c, cleanQuery)}] ${c.title} (${Math.round(c.duration / 1000)}s)`);
  }

  if (candidates.length > 0) {
    console.log(`🎯 SELECTED: ${candidates[0].title}`);
  }
}

async function main() {
  await testQuery('khong the say hieuthuhai');
  await testQuery('cat doi noi sau tang duy tan');
  await testQuery('am tham ben em son tung');
  await testQuery('lac troi son tung');
  await testQuery('noi nay co anh');
}

main();
