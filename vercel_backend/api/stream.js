const axios = require('axios');

module.exports = async function handler(req, res) {
  let targetUrl = req.query.id || req.query.b64 || req.query.url;
  
  if (!targetUrl) {
    return res.status(400).send("Thiếu URL âm thanh.");
  }

  // Tự động giải mã Base64URL hoặc Double-URL-Encoding từ ESP32
  try {
    if (req.query.id || req.query.b64) {
      targetUrl = Buffer.from(targetUrl, 'base64url').toString('utf8');
    }
    let maxDecode = 5;
    while ((targetUrl.includes('%3A') || targetUrl.includes('%2F') || targetUrl.includes('%25')) && maxDecode-- > 0) {
      targetUrl = decodeURIComponent(targetUrl);
    }
  } catch (decodeErr) {
    console.warn("Lỗi decode URL:", decodeErr.message);
  }

  if (!targetUrl.startsWith('http://') && !targetUrl.startsWith('https://')) {
    console.error("URL không hợp lệ sau khi decode:", targetUrl);
    return res.status(400).send("URL không hợp lệ: " + targetUrl);
  }

  // Tùy chọn 302 redirect trực tiếp sang CDN (tiết kiệm 100% băng thông serverless & không bị ngắt quãng)
  if (req.query.redirect === '1' || req.query.direct === 'true') {
    return res.redirect(302, targetUrl);
  }

  console.log("🔊 Proxying audio stream for:", targetUrl.substring(0, 100) + "...");

  try {
    const forwardHeaders = {
      'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
      'Accept': '*/*'
    };

    if (req.headers.range) {
      forwardHeaders['Range'] = req.headers.range;
    }

    const response = await axios({
      method: 'get',
      url: targetUrl,
      responseType: 'stream',
      headers: forwardHeaders,
      timeout: 15000,
      maxRedirects: 5,
      validateStatus: (status) => status >= 200 && status < 400
    });

    res.status(response.status || 200);

    let contentType = 'audio/mpeg';
    if (targetUrl.includes('.m4a') || targetUrl.includes('m4a')) {
      contentType = 'audio/mp4';
    } else if (targetUrl.includes('.aac') || targetUrl.includes('aac')) {
      contentType = 'audio/aac';
    } else if (targetUrl.includes('.wav') || targetUrl.includes('wav')) {
      contentType = 'audio/wav';
    } else {
      contentType = 'audio/mpeg';
    }

    res.setHeader('Content-Type', contentType);
    res.setHeader('Accept-Ranges', 'bytes');
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, HEAD, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Range, Content-Type, Authorization');
    res.setHeader('Access-Control-Expose-Headers', 'Content-Length, Content-Range, Accept-Ranges');
    res.setHeader('Cache-Control', 'public, max-age=86400');

    if (response.headers['content-length']) {
      res.setHeader('Content-Length', response.headers['content-length']);
    }
    if (response.headers['content-range']) {
      res.setHeader('Content-Range', response.headers['content-range']);
    }

    response.data.pipe(res);
  } catch (error) {
    console.error("Lỗi stream audio proxy:", error.message);
    // Nếu proxy gặp trục trặc, tự động 302 redirect về targetUrl
    return res.redirect(302, targetUrl);
  }
};
