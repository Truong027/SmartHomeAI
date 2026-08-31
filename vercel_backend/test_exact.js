const axios = require('axios');

async function testExact(q) {
  const clientId = 'pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD';
  const url = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent(q)}&client_id=${clientId}&limit=10`;
  const res = await axios.get(url);
  console.log(`Results for "${q}":`);
  for (const t of res.data.collection) {
    console.log(`- ${t.title} (${Math.round(t.duration / 1000)}s) [User: ${t.user?.username}]`);
  }
}

async function run() {
  await testExact('Không thể say');
  await testExact('Không thể say Kewtiie');
}
run();
