const axios = require('axios');

async function testZingMobile(q) {
  try {
    // ZingMP3 API search
    const sUrl = 'http://ac.mp3.zing.vn/complete?type=artist,song,key,code&num=5&query=' + encodeURIComponent(q);
    const sRes = await axios.get(sUrl, {
      headers: {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
      },
      timeout: 5000
    });
    console.log('Zing autocomplete response:', sRes.data);

    if (sRes.data && sRes.data.data && sRes.data.data[0] && sRes.data.data[0].song) {
      const songs = sRes.data.data[0].song;
      console.log('Zing Songs:', songs);
      for (const s of songs) {
        console.log(`Song: ${s.name} - ${s.artist} (ID: ${s.id})`);
        // Test direct stream URL for Zing ID
        const streamUrl = `http://api.mp3.zing.vn/api/streaming/audio/${s.id}/128`;
        try {
          const testStream = await axios.head(streamUrl, { maxRedirects: 5, timeout: 5000 });
          console.log(`Zing stream for ${s.id}: Status ${testStream.status}, Type: ${testStream.headers['content-type']}`);
        } catch (stErr) {
          console.log(`Zing stream error for ${s.id}:`, stErr.message);
        }
      }
    }
  } catch (e) {
    console.log('Zing error:', e.message);
  }
}

testZingMobile('Không thể say');
testZingMobile('Cắt đôi nỗi sầu');
testZingMobile('Âm thầm bên em');
