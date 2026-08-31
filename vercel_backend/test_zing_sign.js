const axios = require('axios');
const crypto = require('crypto');

// ZingMP3 API secret keys
const API_KEY = "38e8643fb0ced24614cbd71661773129";
const SECRET_KEY = "10a01dcf33bc62d371d34e9423ab3b5f";

function getHash256(str) {
  return crypto.createHash('sha256').update(str).digest('hex');
}

function getHmac512(str, key) {
  return crypto.createHmac('sha512', key).update(str).digest('hex');
}

function getSig(path, params) {
  const sortedKeys = Object.keys(params).sort();
  let str = '';
  for (const key of sortedKeys) {
    str += `${key}=${params[key]}`;
  }
  const hash256 = getHash256(str);
  return getHmac512(path + hash256, SECRET_KEY);
}

async function getZingSongStream(songId) {
  const ctime = String(Math.floor(Date.now() / 1000));
  const path = '/api/v2/song/get/streaming';
  const params = {
    id: songId,
    ctime: ctime,
    apiKey: API_KEY
  };
  const sig = getSig(path, { id: songId, ctime: ctime });

  const url = `https://zingmp3.vn${path}?id=${songId}&ctime=${ctime}&sig=${sig}&apiKey=${API_KEY}`;
  console.log('Fetching Zing signed streaming URL:', url);

  try {
    const res = await axios.get(url, {
      headers: {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
        'Cookie': 'zmp3_app_version=1.1.1; _zlang=vn'
      },
      timeout: 5000
    });
    console.log('Zing API Response:', res.data);
    if (res.data && res.data.data) {
      console.log('Streams found:', res.data.data);
    }
  } catch (err) {
    console.error('Zing streaming error:', err.message);
  }
}

// Test for "Không Thể Say" (Z69C9IB7) & "Cắt Đôi Nỗi Sầu" (Z6FWCOO0)
getZingSongStream('Z69C9IB7');
getZingSongStream('Z6FWCOO0');
