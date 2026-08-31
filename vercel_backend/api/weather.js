const axios = require('axios');
const cors = require('cors');

// Cấu hình CORS để cho phép App Flutter gọi API
const corsMiddleware = cors({ origin: true });

// Hàm chạy middleware CORS
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

// API Key của Groq do bạn cung cấp
const GROQ_API_KEY = "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";

module.exports = async function handler(req, res) {
  // Bật CORS
  await runMiddleware(req, res, corsMiddleware);

  if (req.method !== 'POST') {
    return res.status(405).json({ error: 'Method Not Allowed' });
  }

  const data = req.body;
  if (!data) {
    return res.status(400).json({ error: 'Thiếu dữ liệu.' });
  }

  try {
    const prompt = `Bạn là trợ lý AI thông minh cho nhà thông minh (Smart Home).
Người dùng đang cung cấp dữ liệu cảm biến và thời tiết hiện tại như sau:
- Nhiệt độ trong nhà: ${data.indoorTemp || 'Không rõ'} °C
- Độ ẩm trong nhà: ${data.indoorHum || 'Không rõ'} %
- Nhiệt độ ngoài trời (OWM): ${data.outTemp || 'Không rõ'} °C
- Độ ẩm ngoài trời (OWM): ${data.outHum || 'Không rõ'} %
- Tốc độ gió: ${data.outWindSpd || 'Không rõ'} m/s
- Tình trạng ngoài trời: ${data.owmDesc || 'Không rõ'}

Hãy đưa ra một câu khuyên DỰ BÁO NGẮN GỌN (Dưới 30 từ) bằng tiếng Việt để hiển thị trên màn hình điều khiển.
Ví dụ: "Trời đang có mưa nhỏ. Bạn nên bật đèn và đóng cửa sổ phòng khách."
Trả về chỉ nội dung lời khuyên, không có dấu ngoặc kép, không có lời chào hỏi.`;

    const url = `https://api.groq.com/openai/v1/chat/completions`;
    
    const requestData = {
        model: "llama-3.3-70b-versatile",
        messages: [{ role: "user", content: prompt }],
        temperature: 0.7,
        max_tokens: 150
    };

    const response = await axios.post(url, requestData, {
        headers: { 
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${GROQ_API_KEY}`
        }
    });

    let aiAdvice = response.data.choices[0].message.content.trim();
    // Loại bỏ dấu ngoặc kép nếu AI vô tình trả về
    aiAdvice = aiAdvice.replace(/^"|"$/g, '');
    
    return res.status(200).json({ success: true, advice: aiAdvice });

  } catch (error) {
    console.error("Lỗi khi gọi Groq API (weather):", error.response ? error.response.data : error.message);
    return res.status(500).json({ error: 'Lỗi phân tích AI: ' + (error.response ? JSON.stringify(error.response.data) : error.message) });
  }
};
