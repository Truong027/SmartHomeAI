const axios = require('axios');

const LIVE_KEYWORDS = [
  'live', 'liveshow', 'live performance', 'live at', 'live in', 'fancam', 
  'concert', 'sân khấu', 'san khau', 'khán giả', 'khan gia', 'hát live', 
  'hat live', 'song 24', 'genfest', 'rap viet live', 'crowd', 'audience', 
  'camcorder', 'record live', 'show', 'acoustic live'
];

function isLiveTrack(title) {
  const lower = title.toLowerCase();
  return LIVE_KEYWORDS.some(kw => lower.includes(kw));
}

async function testStudioSearch(query) {
  console.log('Testing studio search for:', query);
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  
  // Thử tìm với từ khóa studio / official trước
  const sUrl = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent(query + ' official audio')}&client_id=${clientId}&limit=12`;
  const res = await axios.get(sUrl);

  const collection = res.data.collection || [];
  console.log(`Found ${collection.length} tracks.`);

  // 1. Lọc bỏ 100% các bản live, fancam, có tiếng hò reo
  const studioTracks = collection.filter(t => {
    const durSec = Math.round(t.duration / 1000);
    const hasMedia = t.media && t.media.transcodings && t.media.transcodings.length > 0;
    const isDurationValid = durSec >= 80 && durSec <= 480;
    return hasMedia && isDurationValid && !isLiveTrack(t.title);
  });

  console.log('Studio tracks found:', studioTracks.map(t => ({
    title: t.title,
    duration: Math.round(t.duration / 1000) + 's',
    isLive: isLiveTrack(t.title)
  })));

  if (studioTracks.length > 0) {
    console.log('🏆 BEST STUDIO SELECTION:', studioTracks[0].title);
  } else {
    console.log('⚠️ No pure studio track found in first query, trying without suffix...');
  }
}

async function run() {
  await testStudioSearch('Không thể say HIEUTHUHAI');
  await testStudioSearch('Cắt đôi nỗi sầu');
  await testStudioSearch('Âm thầm bên em');
  await testStudioSearch('Nơi này có anh');
}

run();
