const axios = require('axios');

const GROQ_KEYS = [
  "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",
  "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"
];

function removeAccents(str) {
  return String(str || '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/đ/g, 'd').replace(/Đ/g, 'D')
    .toLowerCase().trim();
}

async function fetchGoogleNewsRSS(query) {
  try {
    const url = `https://news.google.com/rss/search?q=${encodeURIComponent(query)}&hl=vi&gl=VN&ceid=VN:vi`;
    const res = await axios.get(url, {
      headers: {
        'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36'
      },
      timeout: 3500
    });

    const newsItems = [];
    const itemMatches = [...res.data.matchAll(/<item>[\s\S]*?<\/item>/g)];
    for (const match of itemMatches.slice(0, 4)) {
      const itemXml = match[0];
      const title = (itemXml.match(/<title>([\s\S]*?)<\/title>/) || [])[1] || '';
      const pubDate = (itemXml.match(/<pubDate>([\s\S]*?)<\/pubDate>/) || [])[1] || '';
      const source = (itemXml.match(/<source[^>]*>([\s\S]*?)<\/source>/) || [])[1] || '';
      const desc = (itemXml.match(/<description>([\s\S]*?)<\/description>/) || [])[1] || '';
      const cleanDesc = desc.replace(/<[^>]*>/g, '').replace(/&nbsp;/g, ' ').replace(/&amp;/g, '&').trim();

      const cleanTitle = title.replace(/\s*-\s*[^-]+$/, '').trim();
      const publisher = source || (title.match(/\s*-\s*([^-]+)$/) || [])[1] || 'Báo chí VN';

      if (cleanTitle.length > 5) {
        newsItems.push(`- [${publisher} | ${pubDate}]: ${cleanTitle}. ${cleanDesc.substring(0, 150)}`);
      }
    }
    return newsItems;
  } catch (err) {
    console.warn("Lỗi fetch Google News RSS:", err.message);
    return [];
  }
}

async function fetchMajorNewspaperRSS(category) {
  const feedMap = {
    'general': 'https://vnexpress.net/rss/tin-moi-nhat.rss',
    'thoi_su': 'https://tuoitre.vn/rss/thoi-su.rss',
    'kinh_doanh': 'https://vnexpress.net/rss/kinh-doanh.rss',
    'chung_khoan': 'https://cafef.vn/thi-truong-chung-khoan.rss',
    'the_gioi': 'https://vnexpress.net/rss/the-gioi.rss',
    'so_hoa': 'https://vnexpress.net/rss/so-hoa.rss'
  };

  const url = feedMap[category] || feedMap['general'];
  try {
    const res = await axios.get(url, {
      headers: { 'User-Agent': 'Mozilla/5.0' },
      timeout: 3000
    });
    const items = [];
    const itemMatches = [...res.data.matchAll(/<item>[\s\S]*?<\/item>/g)];
    for (const m of itemMatches.slice(0, 3)) {
      const itemXml = m[0];
      const title = (itemXml.match(/<title><!\[CDATA\[([\s\S]*?)\]\]><\/title>/) || itemXml.match(/<title>([\s\S]*?)<\/title>/) || [])[1] || '';
      const desc = (itemXml.match(/<description><!\[CDATA\[([\s\S]*?)\]\]><\/description>/) || itemXml.match(/<description>([\s\S]*?)<\/description>/) || [])[1] || '';
      const cleanDesc = desc.replace(/<[^>]*>/g, '').replace(/&nbsp;/g, ' ').trim();
      if (title.length > 5) {
        items.push(`- ${title}: ${cleanDesc.substring(0, 150)}`);
      }
    }
    return items;
  } catch (e) {
    return [];
  }
}

async function callGroqLLM(prompt) {
  const models = ["llama-3.1-8b-instant", "llama-3.3-70b-versatile", "gemma2-9b-it"];
  for (const key of GROQ_KEYS) {
    for (const model of models) {
      try {
        const res = await axios.post(
          'https://api.groq.com/openai/v1/chat/completions',
          {
            model: model,
            messages: [{ role: "user", content: prompt }],
            temperature: 0.25,
            max_tokens: 300
          },
          {
            headers: {
              'Content-Type': 'application/json',
              'Authorization': `Bearer ${key}`
            },
            timeout: 5000
          }
        );
        if (res.data?.choices?.[0]?.message?.content) {
          return res.data.choices[0].message.content.trim();
        }
      } catch (err) {
        console.warn(`Groq (${model} / key ending in ...${key.slice(-6)}) error:`, err.response?.data?.error?.message || err.message);
      }
    }
  }
  return "Hiện tại tôi chưa cập nhật được dữ liệu chi tiết, bạn thử lại sau ít phút nhé.";
}

async function testRealtimeSearch(query) {
  console.log('\n======================================================');
  console.log('Query:', query);

  const norm = removeAccents(query);
  const contextParts = [];

  // 1. Google News RSS theo từ khóa
  const googleNews = await fetchGoogleNewsRSS(query);
  if (googleNews.length > 0) {
    contextParts.push(`[TIN TỨC THỜI GIAN THỰC TỪ GOOGLE NEWS VIỆT NAM]:\n` + googleNews.join('\n'));
  }

  // 2. Chuyên mục liên quan nếu có
  if (norm.includes('chung khoan') || norm.includes('co phieu') || norm.includes('gia vang')) {
    const cafeF = await fetchMajorNewspaperRSS('chung_khoan');
    if (cafeF.length > 0) {
      contextParts.push(`[BẢN TIN KINH TẾ & CHỨNG KHOÁN TỪ CAFEF]:\n` + cafeF.join('\n'));
    }
  } else if (norm.includes('thoi su') || norm.includes('tin moi') || norm.includes('hom nay co gi')) {
    const vnExpress = await fetchMajorNewspaperRSS('thoi_su');
    if (vnExpress.length > 0) {
      contextParts.push(`[BẢN TIN THỜI SỰ TỪ BÁO TUỔI TRẺ & VNEXPRESS]:\n` + vnExpress.join('\n'));
    }
  }

  const contextText = contextParts.join('\n\n');

  const prompt = `Bạn là trợ lý AI thông minh NORI của ngôi nhà thông minh.
Người dùng đang hỏi: "${query}".

DỮ LIỆU BÁO CHÍ VÀ THỜI SỰ VIỆT NAM THỜI GIAN THỰC MỚI NHẤT VỪA CẬP NHẬT:
${contextText || "Không có bài báo trực tiếp, hãy dùng tri thức thời sự chính xác."}

QUY TẮC PHẢN HỒI BẮT BUỘC:
1. TUYỆT ĐỐI BẮT ĐẦU TRẢ LỜI NGAY, không chào hỏi, không rườm rà (CẤM nói "Xin chào", "Tôi là Nori").
2. Dựa sát vào dữ liệu báo chí thời gian thực ở trên để tổng hợp thông tin, nêu rõ số liệu, diễn biến hoặc nhận định mới nhất.
3. Trình bày từ 2 đến 4 câu súc tích, đầy đủ ý nghĩa trên CÙNG 1 ĐOẠN VĂN DUY NHẤT, TUYỆT ĐỐI KHÔNG dùng ký tự xuống dòng (Enter), không dùng ký tự đặc biệt (*, #, :).`;

  const rawAnswer = await callGroqLLM(prompt);
  const answer = rawAnswer.replace(/[\r\n]+/g, ' ').replace(/\s+/g, ' ').trim();
  console.log('🤖 NORI AI Answer:\n', answer);
}

async function run() {
  await testRealtimeSearch('giá vàng hôm nay thế nào');
  await testRealtimeSearch('thị trường chứng khoán hôm nay');
  await testRealtimeSearch('Chủ tịch nước Việt Nam hiện nay là ai');
  await testRealtimeSearch('Tổng bí thư nước ta hiện nay là ai');
}

run();
