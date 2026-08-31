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

  const arduinoCode = req.body.code;
  if (!arduinoCode) {
    return res.status(400).json({ error: 'Thiếu mã nguồn Arduino.' });
  }

  try {
    const prompt = `Tôi có mã nguồn Arduino/C++ cho ESP8266/ESP32 giao tiếp với Firebase.
Mã nguồn:
\`\`\`cpp
${arduinoCode}
\`\`\`
Hãy phân tích mã này và xác định danh sách các widget cần có trên giao diện điều khiển.
Trả về dữ liệu dưới dạng JSON array, mỗi widget là một object gồm:
- type: loại widget (switch, gauge, slider, text, chart, ai_advice, rtc)
- title: Tên widget (VD: Đèn phòng khách, Nhiệt độ)
- pin: biến trên Firebase (VD: relay1, indoorTemp)
- icon: tên icon chuẩn của Material Icons (VD: lightbulb, thermostat, opacity, flash_on)
- color: mã màu hex (VD: 0xFFFFAB00)
Nếu type là gauge, thêm min, max, unit.
CHÚ Ý QUAN TRỌNG: CHỈ in ra đúng JSON array. KHÔNG in thêm bất kỳ văn bản giải thích, lời chào hay định dạng markdown nào khác.`;

    const url = `https://api.groq.com/openai/v1/chat/completions`;
    
    const requestData = {
        model: "llama-3.3-70b-versatile",
        messages: [{ role: "user", content: prompt }],
        temperature: 0.1
    };

    const response = await axios.post(url, requestData, {
        headers: { 
            'Content-Type': 'application/json',
            'Authorization': `Bearer ${GROQ_API_KEY}`
        }
    });

    let textResponse = response.data.choices[0].message.content;
    
    // Tìm mảng JSON đầu tiên trong chuỗi trả về để phòng trường hợp Llama trả về text thừa
    const jsonMatch = textResponse.match(/\[[\s\S]*\]/);
    if (!jsonMatch) {
         throw new Error("Không tìm thấy JSON hợp lệ trong câu trả lời của AI: " + textResponse);
    }
    
    const widgets = JSON.parse(jsonMatch[0]);
    return res.status(200).json({ success: true, widgets: widgets });

  } catch (error) {
    console.error("Lỗi khi gọi Groq API:", error.response ? error.response.data : error.message);
    return res.status(500).json({ error: 'Lỗi phân tích AI: ' + (error.response ? JSON.stringify(error.response.data) : error.message) });
  }
};
