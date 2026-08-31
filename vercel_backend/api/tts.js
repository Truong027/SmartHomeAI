const axios = require('axios');
const cors = require('cors');

const corsMiddleware = cors({ origin: true });

function runMiddleware(req, res, fn) {
  return new Promise((resolve, reject) => {
    fn(req, res, (result) => {
      if (result instanceof Error) {
        return reject(result);
      }
      return resolve(result);
    });
  });
}

function splitTextIntoChunks(text, maxLen = 160) {
  const chunks = [];
  let remaining = String(text).replace(/[\r\n]+/g, ' ').replace(/\s+/g, ' ').trim();
  
  while (remaining.length > 0) {
    if (remaining.length <= maxLen) {
      chunks.push(remaining);
      break;
    }
    
    // Tìm dấu ngắt câu tự nhiên (. , ? ! ;)
    let splitIdx = -1;
    for (let i = maxLen; i >= Math.floor(maxLen * 0.4); i--) {
      const char = remaining[i];
      if (['.', '?', '!', ',', ';', ':'].includes(char)) {
        splitIdx = i + 1;
        break;
      }
    }
    
    // Nếu không có dấu câu, ngắt tại khoảng trắng
    if (splitIdx === -1) {
      for (let i = maxLen; i >= Math.floor(maxLen * 0.4); i--) {
        if (remaining[i] === ' ') {
          splitIdx = i + 1;
          break;
        }
      }
    }
    
    // Fallback: cắt tại maxLen
    if (splitIdx === -1) {
      splitIdx = maxLen;
    }
    
    const chunk = remaining.substring(0, splitIdx).trim();
    if (chunk.length > 0) {
      chunks.push(chunk);
    }
    remaining = remaining.substring(splitIdx).trim();
  }
  
  return chunks;
}

module.exports = async function handler(req, res) {
  if (!req.headers) req.headers = {};
  await runMiddleware(req, res, corsMiddleware);

  let text = req.query.q || req.query.text || (req.body && (req.body.q || req.body.text));

  if (req.query.b64) {
    try {
      text = Buffer.from(req.query.b64, 'base64url').toString('utf-8');
    } catch (e) {
      try {
        text = Buffer.from(req.query.b64, 'base64').toString('utf-8');
      } catch (e2) {}
    }
  }

  if (!text || String(text).trim() === '') {
    return res.status(400).send("Thiếu văn bản TTS");
  }

  const lang = req.query.lang || 'vi';
  const chunks = splitTextIntoChunks(text, 160);

  console.log(`🔊 Generating Vercel Cloud TTS (${chunks.length} chunks) for: "${text.substring(0, 100)}..."`);

  try {
    if (chunks.length === 1) {
      const googleTtsUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=${encodeURIComponent(lang)}&client=tw-ob&q=${encodeURIComponent(chunks[0])}`;
      const resp = await axios.get(googleTtsUrl, {
        responseType: 'arraybuffer',
        headers: {
          'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
        },
        timeout: 10000
      });
      const audioBuffer = Buffer.from(resp.data);
      res.setHeader('Content-Type', 'audio/mpeg');
      res.setHeader('Accept-Ranges', 'bytes');
      res.setHeader('Content-Length', audioBuffer.length);
      res.setHeader('Access-Control-Allow-Origin', '*');
      res.setHeader('Cache-Control', 'public, max-age=86400');
      return res.status(200).end(audioBuffer);
    }

    const audioBuffers = await Promise.all(
      chunks.map(async (chunk) => {
        const googleTtsUrl = `https://translate.google.com/translate_tts?ie=UTF-8&tl=${encodeURIComponent(lang)}&client=tw-ob&q=${encodeURIComponent(chunk)}`;
        const resp = await axios.get(googleTtsUrl, {
          responseType: 'arraybuffer',
          headers: {
            'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36'
          },
          timeout: 10000
        });
        return Buffer.from(resp.data);
      })
    );

    const combinedAudio = Buffer.concat(audioBuffers);

    res.setHeader('Content-Type', 'audio/mpeg');
    res.setHeader('Accept-Ranges', 'bytes');
    res.setHeader('Content-Length', combinedAudio.length);
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Cache-Control', 'public, max-age=86400');

    res.status(200).end(combinedAudio);
  } catch (error) {
    console.error("Lỗi Google TTS Multi-chunk Proxy:", error.message);
    res.status(500).send("Lỗi TTS Proxy: " + error.message);
  }
};
