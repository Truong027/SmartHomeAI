const handler = require('./api/music.js');

const globalSongs = [
  'Mở cho tôi bài Sorry của Justin Bieber',
  'Shape of You Ed Sheeran',
  'Despacito Luis Fonsi',
  'See You Again Wiz Khalifa',
  'Faded Alan Walker',
  'Believer Imagine Dragons',
  'Perfect Ed Sheeran',
  'Hotel California'
];

async function testGlobal() {
  for (const q of globalSongs) {
    const req = { headers: {}, query: { q } };
    const res = {
      setHeader: () => {},
      getHeader: () => {},
      status: (code) => ({
        json: (d) => {
          console.log(`=== "${q}" ===`);
          console.log(`Title: ${d.title} | Artist: ${d.artist} | Duration: ${d.duration}s`);
          console.log(`Stream URL: ${d.stream_url.substring(0, 75)}...\n`);
        }
      })
    };
    await handler(req, res);
  }
}

testGlobal();
