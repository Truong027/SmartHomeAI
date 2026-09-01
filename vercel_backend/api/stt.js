const axios = require('axios');
const FormData = require('form-data');
const cors = require('cors');

const corsMiddleware = cors({ origin: true });

function runMiddleware(req, res, fn) {
  return new Promise((resolve, reject) => {
    fn(req, res, (result) => {
      if (result instanceof Error) return reject(result);
      return resolve(result);
    });
  });
}

const GROQ_API_KEY = process.env.GROQ_API_KEY || "";

function getRawBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (chunk) => chunks.push(chunk));
    req.on('end', () => resolve(Buffer.concat(chunks)));
    req.on('error', (err) => reject(err));
  });
}

module.exports = async function handler(req, res) {
  await runMiddleware(req, res, corsMiddleware);

  if (req.method !== 'POST') {
    return res.status(405).json({ error: 'Method Not Allowed' });
  }

  try {
    let audioBuffer = null;
    if (Buffer.isBuffer(req.body)) {
      audioBuffer = req.body;
    } else {
      audioBuffer = await getRawBody(req);
    }

    if (!audioBuffer || audioBuffer.length < 100) {
      return res.status(400).json({ error: 'Dữ liệu âm thanh không hợp lệ hoặc quá ngắn.' });
    }

    const form = new FormData();
    form.append('model', 'whisper-large-v3-turbo');
    form.append('language', 'vi');
    form.append('response_format', 'json');
    form.append('temperature', '0');
    form.append('prompt', 'Hi Nori, Hey Nori, Nori oi, chao Nori, bat den, tat quat');
    form.append('file', audioBuffer, {
      filename: 'audio.wav',
      contentType: 'audio/wav',
    });

    const groqRes = await axios.post('https://api.groq.com/openai/v1/audio/transcriptions', form, {
      headers: {
        ...form.getHeaders(),
        'Authorization': `Bearer ${GROQ_API_KEY}`,
      },
      timeout: 8000,
    });

    return res.status(200).json(groqRes.data);
  } catch (error) {
    console.error('Groq STT Error:', error.response ? error.response.data : error.message);
    const errDetail = error.response ? error.response.data : error.message;
    return res.status(500).json({ error: 'Lỗi Whisper STT', detail: errDetail });
  }
};

module.exports.config = {
  api: {
    bodyParser: false,
  },
};
