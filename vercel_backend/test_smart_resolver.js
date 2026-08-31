const axios = require('axios');

async function testSmartQuery(rawQuery) {
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  console.log(`\n==============================================`);
  console.log(`Original: "${rawQuery}"`);

  // 1. Làm sạch triệt để dấu câu và từ đệm
  let clean = rawQuery.replace(/[.,\/#!$%\^&\*;:{}=\-_`~()]/g, ' ')
                      .replace(/^(hãy mở|hãy bật|hãy phát|mở giúp tôi|bật giúp tôi|phát giúp tôi|mở cho tôi|bật cho tôi|phát cho tôi|mở hộ tôi|bật hộ tôi|phát hộ tôi|cho tôi nghe|cho mình nghe|cho nghe|mở bài hát|bật bài hát|phát bài hát|mở bài nhạc|bật bài nhạc|phát bài nhạc|mở bài|bật bài|phát bài|hát bài|nghe bài|mở ca khúc|bật ca khúc|phát ca khúc|mở nhạc|bật nhạc|phát nhạc|nghe nhạc|tìm bài|tìm nhạc|hát nhạc|play)\s+/gi, '')
                      .replace(/^(mo giup toi|bat giup toi|phat giup toi|mo ho toi|bat ho toi|phat ho toi|mo cho toi|bat cho toi|phat cho toi|cho toi nghe|cho minh nghe|cho nghe|mo bai hat|bat bai hat|phat bai hat|mo bai nhac|bat bai nhac|phat bai nhac|mo bai|bat bai|phat bai|hat bai|nghe bai|mo ca khuc|bat ca khuc|phat ca khuc|mo nhac|bat nhac|phat nhac|nghe nhac|tim bai|tim nhac|hat nhac|play)\s+/gi, '')
                      .replace(/^(cho toi|cho minh|cho nghe|cho em|cho anh|toi muon|minh muon)\s+/gi, '')
                      .replace(/^(bài hát|bai hat|bài nhạc|bai nhac|bài|bai|ca khúc|ca khuc|nhạc|nhac)\s+/gi, '')
                      .replace(/\s+(của|cua|bởi|boi|do|ca sĩ|ca si|trình bày|trinh bay)\s+/gi, ' ')
                      .replace(/\s+(đi|nào|nha|nhé|với|ạ|nhé bạn|nha bạn)$/gi, '')
                      .trim();

  // Tạo danh sách các truy vấn phụ (thử tìm cả câu, hoặc bỏ tên ca sĩ ở đuôi nếu quá dài)
  const candidateQueries = [clean];
  const words = clean.split(/\s+/);
  if (words.length > 4) {
    // Thử bỏ 2-3 từ cuối (thường là tên ca sĩ: "hieu thu hai", "son tung", "justin bieber")
    candidateQueries.push(words.slice(0, -3).join(' '));
    candidateQueries.push(words.slice(0, -2).join(' '));
  }

  console.log('Candidate Queries:', candidateQueries);

  for (const q of candidateQueries) {
    if (!q || q.length < 3) continue;
    const url = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent(q)}&client_id=${clientId}&limit=10`;
    const res = await axios.get(url);
    const collection = res.data?.collection || [];

    const validTracks = collection.filter(t => {
      const durSec = Math.round(t.duration / 1000);
      const hasProgressive = t.media && t.media.transcodings && t.media.transcodings.some(tc => tc.format && tc.format.protocol === 'progressive');
      return hasProgressive && durSec >= 75 && durSec <= 600;
    });

    if (validTracks.length > 0) {
      console.log(`✅ MATCHED with query: "${q}"`);
      const chosen = validTracks[0];
      const tc = chosen.media.transcodings.find(t => t.format && t.format.protocol === 'progressive');
      const streamInfo = await axios.get(`${tc.url}?client_id=${clientId}`);
      console.log(`  Title: ${chosen.title} (${Math.round(chosen.duration / 1000)}s)`);
      console.log(`  Stream: ${streamInfo.data.url.substring(0, 75)}...`);
      return;
    }
  }
  console.log('❌ No valid track found');
}

async function run() {
  await testSmartQuery('cho toi nguoi im lang gap nguoi hay noi hieu thu hai.');
  await testSmartQuery('mở giúp tôi bài người im lặng gặp người hay nói của hiếu thứ hai');
  await testSmartQuery('cho tôi bài sorry của justin bieber');
}

run();
