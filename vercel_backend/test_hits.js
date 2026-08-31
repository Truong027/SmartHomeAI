const axios = require('axios');

async function findStudioTracks() {
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  const hits = ['Không Thể Say HIEUTHUHAI', 'Cắt Đôi Nỗi Sầu Tăng Duy Tân', 'Âm Thầm Bên Em Sơn Tùng', 'Lạc Trôi Sơn Tùng', 'Nơi Này Có Anh'];
  for (const h of hits) {
    const url = 'https://api-v2.soundcloud.com/search/tracks?q=' + encodeURIComponent(h) + '&client_id=' + clientId + '&limit=8';
    const res = await axios.get(url);
    console.log('\n=== ' + h + ' ===');
    for (const t of res.data.collection) {
      const prog = t.media && t.media.transcodings ? t.media.transcodings.find(tc => tc.format.protocol === 'progressive') : null;
      if (prog) {
        console.log(`- Title: "${t.title}" (${Math.round(t.duration / 1000)}s) | Progressive: ${prog.url}`);
      }
    }
  }
}
findStudioTracks();
