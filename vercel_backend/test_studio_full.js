const axios = require('axios');

async function testFull() {
  const handler = require('./api/music.js');
  const queries = [
    'mo giup toi  khong the say cua hieu thu hai',
    'cat doi noi sau',
    'am tham ben em',
    'lac troi',
    'noi nay co anh'
  ];

  for (const q of queries) {
    const req = { headers: {}, query: { q } };
    const res = {
      setHeader: () => {},
      getHeader: () => {},
      status: (code) => ({
        json: (d) => {
          console.log(`=== "${q}" ===`);
          console.log(`Title: ${d.title} | Duration: ${d.duration}s | Source: ${d.source}`);
          console.log(`Stream URL: ${d.stream_url.substring(0, 75)}...\n`);
        }
      })
    };
    await handler(req, res);
  }
}

testFull();
