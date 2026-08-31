const axios = require('axios');

async function testArchiveSearch(q) {
  try {
    const url = `https://archive.org/advancedsearch.php?q=${encodeURIComponent(q)}&fl[]=identifier,title,description,mediatype&rows=5&page=1&output=json`;
    console.log('Querying Archive:', url);
    const res = await axios.get(url, { timeout: 8000 });
    const docs = res.data.response.docs;
    console.log('Found docs:', docs.length);
    for (const doc of docs) {
      console.log(`- Identifier: ${doc.identifier}, Title: ${doc.title}`);
      // Get files for identifier
      const metaUrl = `https://archive.org/metadata/${doc.identifier}`;
      const metaRes = await axios.get(metaUrl, { timeout: 6000 });
      const mp3Files = metaRes.data.files.filter(f => f.name.endsWith('.mp3'));
      if (mp3Files.length > 0) {
        const fullMp3Url = `https://archive.org/download/${doc.identifier}/${encodeURIComponent(mp3Files[0].name)}`;
        console.log(`  Full MP3 URL: ${fullMp3Url}`);
      }
    }
  } catch (e) {
    console.error('Archive error:', e.message);
  }
}

testArchiveSearch('HIEUTHUHAI');
testArchiveSearch('Son Tung M-TP');
testArchiveSearch('Cat Doi Noi Sau');
