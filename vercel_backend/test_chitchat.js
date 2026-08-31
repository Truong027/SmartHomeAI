const axios = require('axios');

const GROQ_API_KEY = process.env.GROQ_API_KEY || "YOUR_GROQ_API_KEY";

async function testChitChat(q) {
  const prompt = `Bạn là trợ lý AI thông minh NORI của ngôi nhà thông minh.
Người dùng đang trò chuyện: "${q}".
Hãy trả lời trực tiếp, thông minh, thân thiện, tự nhiên và hóm hỉnh phù hợp với vai trò trợ lý AI NORI:
- Nếu người dùng hỏi bạn có biết họ là ai không (hoặc họ là ai): Hãy khẳng định bạn luôn nhận ra họ là chủ nhân của ngôi nhà thông minh này, và bạn luôn sẵn lòng hỗ trợ điều khiển thiết bị, phát nhạc hay giải đáp mọi kiến thức.
- Nếu người dùng hỏi bạn là ai/tên gì: Giới thiệu ngắn gọn bạn là Nori, trợ lý nhà thông minh đa năng.
- Tuyệt đối KHÔNG bịa đặt thông tin về nhân vật lịch sử, chương trình truyền hình không liên quan.
- Trình bày ngắn gọn từ 2 đến 3 câu trên cùng 1 dòng duy nhất, không xuống dòng, không dùng ký tự đặc biệt (*, #, :).`;

  const res = await axios.post(
    'https://api.groq.com/openai/v1/chat/completions',
    {
      model: 'llama-3.1-8b-instant',
      messages: [{ role: 'user', content: prompt }],
      temperature: 0.3,
      max_tokens: 250
    },
    {
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${GROQ_API_KEY}`
      }
    }
  );
  console.log(`\nQ: "${q}"`);
  console.log(`A: ${res.data.choices[0].message.content.trim()}`);
}

async function run() {
  await testChitChat('Bà biết tôi là ai không?');
  await testChitChat('Bạn biết tôi là ai không?');
  await testChitChat('Tôi là ai?');
  await testChitChat('Bạn tên là gì?');
}

run();
