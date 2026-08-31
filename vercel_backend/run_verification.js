const search = require('./api/search.js');
const music = require('./api/music.js');

async function testSearch(q) {
  return new Promise(res => {
    const req = { query: { q }, headers: {} };
    const resObj = {
      setHeader: () => {},
      getHeader: () => {},
      status: (c) => ({
        json: (d) => {
          console.log('\n[SEARCH TEST] Query: "' + q + '"');
          console.log('Status:', c);
          console.log('Answer:', d.answer);
          console.log('Source:', d.source);
          res(d);
        }
      })
    };
    search(req, resObj);
  });
}

async function testMusic(q) {
  return new Promise(res => {
    const req = { query: { q }, headers: {} };
    const resObj = {
      setHeader: () => {},
      getHeader: () => {},
      status: (c) => ({
        json: (d) => {
          console.log('\n[MUSIC TEST] Query: "' + q + '"');
          console.log('Status:', c);
          console.log('Title:', d.full_title);
          console.log('Stream URL Valid:', (d.stream_url && d.stream_url.startsWith('http')) ? 'YES' : 'NO');
          res(d);
        }
      })
    };
    music(req, resObj);
  });
}

async function runAll() {
  console.log('====== BẮT ĐẦU KIỂM TRA TOÀN DIỆN ======');
  await testSearch('con rắn');
  await testSearch('con voi');
  await testSearch('thủ đô nước Pháp');
  await testSearch('hôm nay ngày mấy âm lịch');
  await testSearch('Natri Clorid 0.9%');
  
  await testMusic('Lạc trôi');
  await testMusic('Em của ngày hôm qua');
  await testMusic('Cắt đôi nỗi sầu');
  await testMusic('bật bài hát Baby Justin Bieber');
  console.log('\n====== HOÀN TẤT TOÀN BỘ KIỂM TRA ======');
}

runAll();
