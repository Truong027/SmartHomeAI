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

const GROQ_KEYS = [
  "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",
  "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"
];
const OWM_API_KEY = "6e117d37cbcacbcef8db7c37ca75044e";

// Từ điển mapping các thành phố/quốc gia thông dụng tiếng Việt sang tên chuẩn quốc tế cho OpenWeatherMap
const CITY_MAPPINGS = {
  "sai gon": "Ho Chi Minh City",
  "tp hcm": "Ho Chi Minh City",
  "tphcm": "Ho Chi Minh City",
  "tp ho chi minh": "Ho Chi Minh City",
  "thanh pho ho chi minh": "Ho Chi Minh City",
  "ha noi": "Hanoi",
  "da nang": "Da Nang",
  "hai phong": "Hai Phong",
  "can tho": "Can Tho",
  "nha trang": "Nha Trang",
  "da lat": "Da Lat",
  "vung tau": "Vung Tau",
  "hue": "Hue",
  "quang ninh": "Ha Long",
  "ha long": "Ha Long",
  "quang nam": "Tam Ky",
  "nghe an": "Vinh",
  "vinh": "Vinh",
  "thanh hoa": "Thanh Hoa",
  "nam dinh": "Nam Dinh",
  "thai binh": "Thai Binh",
  "bac ninh": "Bac Ninh",
  "bac giang": "Bac Giang",
  "phu quoc": "Phu Quoc",
  "phan thiet": "Phan Thiet",
  "buon ma thuot": "Buon Ma Thuot",
  "pleiku": "Pleiku",
  "tay ninh": "Tay Ninh",
  "bac kinh": "Beijing",
  "thuong hai": "Shanghai",
  "luan don": "London",
  "tokyo": "Tokyo",
  "seoul": "Seoul",
  "paris": "Paris",
  "new york": "New York",
  "washington": "Washington",
  "bangkok": "Bangkok",
  "singapore": "Singapore",
  "sydney": "Sydney",
  "melbourne": "Melbourne",
  "berlin": "Berlin",
  "roma": "Rome",
  "rome": "Rome",
  "moscow": "Moscow",
  "matxcova": "Moscow",
  "los angeles": "Los Angeles",
  "chicago": "Chicago",
  "toronto": "Toronto",
  "dubai": "Dubai"
};

function removeAccents(str) {
  return String(str || '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/đ/g, 'd').replace(/Đ/g, 'D')
    .toLowerCase().trim();
}

// Nhận diện các câu hỏi giao tiếp, hỏi danh tính, chào hỏi, đùa vui
function isConversationalOrIdentityQuery(q) {
  const norm = removeAccents(q).replace(/[.,\/#!$%\^&\*;:{}=\-_`~()?]/g, ' ').replace(/\s+/g, ' ').trim();
  
  // 1. Kiểm tra câu hỏi về bản thân / người dùng ("Bà biết tôi là ai không?", "Tôi là ai?")
  if (norm.includes('biet toi la ai') || norm.includes('biet tao la ai') || norm.includes('biet minh la ai') ||
      norm.includes('toi la ai') || norm.includes('tao la ai') || norm.includes('co biet toi khong') || norm.includes('biet toi khong') ||
      norm.includes('biet anh la ai') || norm.includes('biet em la ai')) {
    return true;
  }

  // 2. Kiểm tra câu hỏi về AI / Nori ("Bạn là ai?", "Bạn tên là gì?", "Ai tạo ra bạn?")
  if (norm.includes('ban la ai') || norm.includes('may la ai') || norm.includes('em la ai') || norm.includes('nori la ai') ||
      norm.includes('ten la gi') || norm.includes('ten gi') || norm.includes('ai tao ra') ||
      norm.includes('ai lam ra') || norm.includes('lam duoc nhung gi') || norm.includes('lam duoc gi') ||
      norm.includes('co thong minh khong') || norm.includes('co khoe khong')) {
    return true;
  }

  // 3. Lời chào, cảm ơn, khen ngợi, chuyện cười
  if (norm === 'xin chao' || norm === 'chao ban' || norm === 'chao em' || norm === 'chao nori' || norm === 'hello' || norm === 'hi' ||
      norm.includes('cam on') || norm.includes('gioi qua') || norm.includes('hay qua') || norm.includes('ke chuyen cuoi') || norm.includes('khen')) {
    return true;
  }

  return false;
}

// Trích xuất địa điểm thời tiết từ câu hỏi
function extractWeatherLocation(query) {
  const norm = removeAccents(query);
  for (const [key, val] of Object.entries(CITY_MAPPINGS)) {
    if (norm.includes(key)) {
      return val;
    }
  }

  const patterns = [
    /thoi tiet\s+(?:o|tai|khu vuc|thanh pho|tinh|nuoc)\s+([a-zA-Z0-9\s]+?)(?:\s+hom nay|\s+ngay mai|\s+the nao|\s+nhu the nao|\s+ra sao|\s+khong|\?|$)/i,
    /nhiet do\s+(?:o|tai|khu vuc|thanh pho|tinh|nuoc)\s+([a-zA-Z0-9\s]+?)(?:\s+hom nay|\s+ngay mai|\s+la bao nhieu|\s+the nao|\s+ra sao|\?|$)/i,
    /du bao thoi tiet\s+(?:o|tai|khu vuc|thanh pho|tinh|nuoc)?\s*([a-zA-Z0-9\s]+?)(?:\s+hom nay|\s+ngay mai|\s+the nao|\s+ra sao|\?|$)/i,
    /troi\s+(?:o|tai)\s+([a-zA-Z0-9\s]+?)(?:\s+co mua|\s+nang|\s+the nao|\s+ra sao|\?|$)/i,
    /(?:o|tai)\s+([a-zA-Z0-9\s]+?)\s+thoi tiet\s+(?:the nao|nhu the nao|ra sao|\?|$)/i
  ];

  for (const regex of patterns) {
    const match = norm.match(regex);
    if (match && match[1]) {
      let loc = match[1].trim();
      loc = loc.replace(/^(thanh pho|tp|tinh|nuoc|khu vuc)\s+/i, '').trim();
      if (loc.length >= 2) {
        if (CITY_MAPPINGS[loc]) return CITY_MAPPINGS[loc];
        return loc;
      }
    }
  }

  return null;
}

// Gọi OpenWeatherMap API
async function fetchOpenWeatherData(locationName) {
  try {
    const url = `https://api.openweathermap.org/data/2.5/weather?q=${encodeURIComponent(locationName)}&units=metric&lang=vi&appid=${OWM_API_KEY}`;
    const res = await axios.get(url, { timeout: 3500 });
    const data = res.data;
    if (data && data.main) {
      return {
        city: data.name,
        country: data.sys ? data.sys.country : '',
        temp: data.main.temp,
        feels_like: data.main.feels_like,
        temp_min: data.main.temp_min,
        temp_max: data.main.temp_max,
        humidity: data.main.humidity,
        pressure: data.main.pressure,
        description: data.weather && data.weather[0] ? data.weather[0].description : '',
        wind_speed: data.wind ? data.wind.speed : 0,
        clouds: data.clouds ? data.clouds.all : 0,
        visibility: data.visibility ? (data.visibility / 1000).toFixed(1) : ''
      };
    }
  } catch (err) {
    console.warn("Lỗi fetch OpenWeatherMap:", err.message);
  }
  return null;
}

function extractCoreKeywords(query) {
  let clean = query.replace(/[.,?!]+$/g, '').trim();
  let q = clean.replace(/^(cho tôi biết|cho tôi thông tin chi tiết về|cho tôi thông tin về|cho tôi hỏi|cho hỏi|tìm hiểu về|tìm kiếm về|tìm kiếm|tra cứu|bạn có biết|kể cho tôi nghe về|nói cho tôi biết về|thông tin về|giới thiệu về)\s+/gi, '');
  
  const terms = [clean, q];
  let simplified = q.replace(/\s+(và kênh đăng ký của anh ấy được bao nhiêu người theo dõi rồi|và kênh youtube được bao nhiêu sub|được bao nhiêu người theo dõi rồi|được bao nhiêu người đăng ký|bao nhiêu người đăng ký|bao nhiêu người theo dõi|kênh đăng ký của anh ấy được bao nhiêu người|là ai|là gì|như thế nào|ở đâu|khi nào|tại sao|mấy giờ|có bao nhiêu)$/gi, '').trim();
  if (simplified.length > 0 && !terms.includes(simplified)) {
    terms.unshift(simplified);
  }
  return terms;
}

// ── BƯỚC 1: TRA CỨU BÁCH KHOA TOÀN THƯ WIKIPEDIA TIẾNG VIỆT CHUẨN KHOA HỌC & TOÀN VĂN ──
async function fetchWikiExtract(terms) {
  try {
    const headers = { 'User-Agent': 'NoriAIHub/2.1 (contact@norihub.internal)' };
    const termList = Array.isArray(terms) ? terms : [terms];

    for (const term of termList) {
      if (!term || term.length < 2) continue;

      // 1. Thử OpenSearch và Summary API trực tiếp
      try {
        const sumUrl = `https://vi.wikipedia.org/api/rest_v1/page/summary/${encodeURIComponent(term)}`;
        const sumRes = await axios.get(sumUrl, { headers, timeout: 2500 });
        if (sumRes.data?.extract && sumRes.data.extract.length > 30) {
          return {
            title: sumRes.data.title,
            extract: sumRes.data.extract,
            description: sumRes.data.description || ''
          };
        }
      } catch (e) {}

      // 2. Thử Full-Text Search qua MediaWiki API
      try {
        const ftUrl = `https://vi.wikipedia.org/w/api.php?action=query&list=search&srsearch=${encodeURIComponent(term)}&utf8=&format=json`;
        const ftRes = await axios.get(ftUrl, { headers, timeout: 3000 });
        if (ftRes.data?.query?.search?.length > 0) {
          for (const item of ftRes.data.query.search.slice(0, 3)) {
            try {
              const sumUrl = `https://vi.wikipedia.org/api/rest_v1/page/summary/${encodeURIComponent(item.title)}`;
              const sumRes = await axios.get(sumUrl, { headers, timeout: 2500 });
              if (sumRes.data?.extract && sumRes.data.extract.length > 30) {
                return {
                  title: sumRes.data.title,
                  extract: sumRes.data.extract,
                  description: sumRes.data.description || ''
                };
              }
            } catch (e) {}
          }
        }
      } catch (e) {}
    }
  } catch (e) {
    console.warn("Lỗi tra cứu Wikipedia:", e.message);
  }
  return null;
}

// ── BƯỚC 2: TRA CỨU THÔNG TIN KÊNH YOUTUBE & NGƯỜI THEO DÕI THỜI GIAN THỰC ──
async function fetchYouTubeChannelInfo(terms) {
  try {
    const termList = Array.isArray(terms) ? terms : [terms];
    for (const term of termList) {
      if (!term || term.length < 2) continue;
      const url = `https://www.youtube.com/results?search_query=${encodeURIComponent(term)}`;
      const res = await axios.get(url, {
        headers: {
          'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36',
          'Accept-Language': 'vi-VN,vi;q=0.9'
        },
        timeout: 4000
      });
      const subMatch = res.data.match(/"subscriberCountText":\{"accessibility":\{"accessibilityData":\{"label":"([^"]+)"\}\},"simpleText":"([^"]+)"\}/);
      if (subMatch) {
        return `Thông tin Kênh YouTube [${term}]: Hiện tại có ${subMatch[1]} (${subMatch[2]}).`;
      }
    }
  } catch (e) {}
  return null;
}

// ── BƯỚC 3: TRA CỨU DUCKDUCKGO INSTANT ANSWER & TRI THỨC ĐỊNH NGHĨA ──
async function fetchDuckDuckGoAnswer(query) {
  try {
    const url = `https://api.duckduckgo.com/?q=${encodeURIComponent(query)}&format=json&no_html=1&skip_disambig=1`;
    const res = await axios.get(url, {
      headers: { 'User-Agent': 'NoriAIHub/2.1' },
      timeout: 3000
    });
    const d = res.data;
    if (d && (d.AbstractText || d.Answer)) {
      return (d.Answer || '') + ' ' + (d.AbstractText || '');
    }
  } catch (e) {}
  return null;
}

// ── BƯỚC 4: THU THẬP DỮ LIỆU THỜI SỰ THỜI GIAN THỰC TỪ GOOGLE NEWS RSS TIẾNG VIỆT ──
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

// ── BƯỚC 4: THU THẬP DỮ LIỆU RSS TRỰC TIẾP TỪ CÁC TRANG BÁO LỚN (VNEXPRESS, TUỔI TRẺ, CAFEF) ──
async function fetchMajorNewspaperRSS(category) {
  const feedMap = {
    'general': 'https://vnexpress.net/rss/tin-moi-nhat.rss',
    'thoi_su': 'https://tuoitre.vn/rss/thoi-su.rss',
    'kinh_doanh': 'https://vnexpress.net/rss/kinh-doanh.rss',
    'chung_khoan': 'https://cafef.vn/thi-truong-chung-khoan.rss',
    'the_gioi': 'https://vnexpress.net/rss/the-gioi.rss',
    'so_hoa': 'https://vnexpress.net/rss/so-hoa.rss',
    'the_thao': 'https://vnexpress.net/rss/the-thao.rss'
  };

  const url = feedMap[category] || feedMap['general'];
  try {
    const res = await axios.get(url, {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36' },
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

// ── BƯỚC 5: GỌI GROQ LLM VỚI XOAY VÒNG KEY VÀ MODEL DỰ PHÒNG ──
async function callGroqLLM(prompt, temperature = 0.25, maxTokens = 400) {
  const models = ["llama-3.3-70b-versatile", "llama-3.1-8b-instant", "gemma2-9b-it"];
  for (const key of GROQ_KEYS) {
    for (const model of models) {
      try {
        const res = await axios.post(
          'https://api.groq.com/openai/v1/chat/completions',
          {
            model: model,
            messages: [{ role: "user", content: prompt }],
            temperature: temperature,
            max_tokens: maxTokens
          },
          {
            headers: {
              'Content-Type': 'application/json',
              'Authorization': `Bearer ${key}`
            },
            timeout: 6000
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
  return null;
}

// ── THUẬT TOÁN CHUYỂN ĐỔI DƯƠNG LỊCH -> ÂM LỊCH VIỆT NAM THIÊN VĂN (MÚI GIỜ GMT+7) - HỒ NGỌC ĐỨC ──
function INT(d) { return Math.floor(d); }

function jdFromDate(dd, mm, yy) {
  let a = INT((14 - mm) / 12);
  let y = yy + 4800 - a;
  let m = mm + 12 * a - 3;
  let jd = dd + INT((153 * m + 2) / 5) + 365 * y + INT(y / 4) - INT(y / 100) + INT(y / 400) - 32045;
  if (jd < 2299161) {
    jd = dd + INT((153 * m + 2) / 5) + 365 * y + INT(y / 4) - 32083;
  }
  return jd;
}

function getNewMoonDay(k, timeZone = 7) {
  var T = k / 1236.85;
  var T2 = T * T;
  var T3 = T2 * T;
  var dr = Math.PI / 180;
  var Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
  Jd1 = Jd1 + 0.00033 * Math.sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
  var M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
  var Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
  var F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
  var C1 = (0.1734 - 0.000393 * T) * Math.sin(M * dr) + 0.0021 * Math.sin(2 * dr * M);
  C1 = C1 - 0.4068 * Math.sin(Mpr * dr) + 0.0161 * Math.sin(2 * dr * Mpr);
  C1 = C1 - 0.0004 * Math.sin(3 * dr * Mpr);
  C1 = C1 + 0.0104 * Math.sin(2 * dr * F) - 0.0051 * Math.sin((M + Mpr) * dr);
  C1 = C1 - 0.0074 * Math.sin((M - Mpr) * dr) + 0.0004 * Math.sin((2 * F + M) * dr);
  C1 = C1 - 0.0004 * Math.sin((2 * F - M) * dr) - 0.0006 * Math.sin((2 * F + Mpr) * dr);
  C1 = C1 + 0.0100 * Math.sin((2 * F - Mpr) * dr) + 0.0005 * Math.sin((M + 2 * Mpr) * dr);
  var deltat = -0.000078 + 0.000287 * T + 0.0001494 * T2 + 0.00000410 * T3 + 0.000000004 * T * T3;
  var JdNew = Jd1 + C1 - deltat;
  var val = JdNew + 0.5 + timeZone / 24.0;
  if ((val - Math.floor(val)) > 0.98) {
    return Math.floor(val) + 1;
  }
  return Math.floor(val);
}

function getSunLongitude(jdn, timeZone = 7) {
  var T = (jdn - 2451545.0 + 0.5 - timeZone / 24.0) / 36525.0;
  var T2 = T * T;
  var dr = Math.PI / 180;
  var M = 357.52910 + 35999.05029 * T - 0.0001559 * T2 - 0.00000048 * T * T2;
  var L0 = 280.46645 + 36000.76983 * T + 0.0003032 * T2;
  var DL = (1.91460 - 0.004817 * T - 0.000014 * T2) * Math.sin(M * dr);
  DL = DL + (0.019993 - 0.000101 * T) * Math.sin(2 * M * dr) + 0.000290 * Math.sin(3 * M * dr);
  var L = L0 + DL;
  L = L * dr;
  L = L - Math.PI * 2 * Math.floor(L / (Math.PI * 2));
  return Math.floor(L / (Math.PI / 6));
}

function getLunarMonth11(yy, timeZone = 7) {
  var k = Math.floor((jdFromDate(31, 12, yy) - 2415021.0769986) / 29.530588853);
  var nm = getNewMoonDay(k, timeZone);
  var sunLong = getSunLongitude(nm, timeZone);
  if (sunLong >= 9) {
    nm = getNewMoonDay(k - 1, timeZone);
  }
  return nm;
}

function getLeapMonthOffset(a11, timeZone = 7) {
  var k = Math.floor((a11 - 2415021.0769986) / 29.530588853 + 0.5);
  var last = 0;
  var i = 1;
  var arc = getSunLongitude(getNewMoonDay(k + i, timeZone), timeZone);
  do {
    last = arc;
    i++;
    arc = getSunLongitude(getNewMoonDay(k + i, timeZone), timeZone);
  } while (arc != last && i < 14);
  return i - 1;
}

function convertSolar2Lunar(dd, mm, yy, timeZone = 7) {
  var dayNumber = jdFromDate(dd, mm, yy);
  var k = Math.floor((dayNumber - 2415021.0769986) / 29.530588853);
  var monthStart = getNewMoonDay(k + 1, timeZone);
  if (monthStart > dayNumber) {
    monthStart = getNewMoonDay(k, timeZone);
  }
  var a11 = getLunarMonth11(yy, timeZone);
  var b11 = a11;
  var lunarYear;
  if (a11 >= monthStart) {
    lunarYear = yy;
    a11 = getLunarMonth11(yy - 1, timeZone);
  } else {
    lunarYear = yy + 1;
    b11 = getLunarMonth11(yy + 1, timeZone);
  }
  var lunarDay = dayNumber - monthStart + 1;
  var diff = Math.floor((monthStart - a11) / 29);
  var lunarLeap = 0;
  var lunarMonth = diff + 11;
  if (b11 - a11 > 365) {
    var leapMonthDiff = getLeapMonthOffset(a11, timeZone);
    if (diff >= leapMonthDiff) {
      lunarMonth = diff + 10;
      if (diff == leapMonthDiff) {
        lunarLeap = 1;
      }
    }
  }
  if (lunarMonth > 12) {
    lunarMonth = lunarMonth - 12;
  }
  if (lunarMonth >= 11 && diff < 4) {
    lunarYear -= 1;
  }
  var CAN = ['Giáp', 'Ất', 'Bính', 'Đinh', 'Mậu', 'Kỷ', 'Canh', 'Tân', 'Nhâm', 'Quý'];
  var CHI = ['Tý', 'Sửu', 'Dần', 'Mão', 'Thìn', 'Tỵ', 'Ngọ', 'Mùi', 'Thân', 'Dậu', 'Tuất', 'Hợi'];
  var canChiDay = CAN[(dayNumber + 9) % 10] + ' ' + CHI[(dayNumber + 1) % 12];
  var canChiYear = CAN[(lunarYear + 6) % 10] + ' ' + CHI[(lunarYear + 8) % 12];

  return {
    lunarDay: lunarDay,
    lunarMonth: lunarMonth,
    lunarYear: lunarYear,
    lunarLeap: lunarLeap,
    canChiDay: canChiDay,
    canChiYear: canChiYear,
    solarDay: dd,
    solarMonth: mm,
    solarYear: yy
  };
}

module.exports = async function handler(req, res) {
  if (!req.headers) req.headers = {};
  const query = (req.query && (req.query.q || req.query.query)) || (req.body && (req.body.q || req.body.question || req.body.query));

  if (!query || String(query).trim() === '') {
    return res.status(400).json({ success: false, error: 'Thiếu câu hỏi cần tìm kiếm.' });
  }

  const cleanQuery = String(query).trim();
  const lowerQuery = removeAccents(cleanQuery);
  console.log(`🔎 [Real-time Search] Processing Query: "${cleanQuery}"`);

  // Tính toán thời gian thực tế tại Việt Nam (GMT+7)
  const nowVN = new Date(new Date().getTime() + 7 * 3600 * 1000);
  const curDay = nowVN.getUTCDate();
  const curMonth = nowVN.getUTCMonth() + 1;
  const curYear = nowVN.getUTCFullYear();
  const lunarInfo = convertSolar2Lunar(curDay, curMonth, curYear, 7);

  try {
    // ── 0. NẾU LÀ CÂU HỎI VỀ NGÀY THÁNG / ÂM LỊCH / DƯƠNG LỊCH HIỆN TẠI ──
    const isDateOrLunarQuery = lowerQuery.includes('am lich') || lowerQuery.includes('ngay am') || 
                               lowerQuery.includes('lich am') || lowerQuery.includes('ngay bao nhieu') || 
                               lowerQuery.includes('hom nay ngay may') || lowerQuery.includes('ngay duong') || 
                               lowerQuery.includes('may gio') || lowerQuery.includes('thu may') ||
                               lowerQuery.includes('nam nay nam con gi');

    if (isDateOrLunarQuery) {
      let dateAnswer = "";
      if (lowerQuery.includes('am lich') || lowerQuery.includes('ngay am') || lowerQuery.includes('lich am')) {
        dateAnswer = `Hôm nay là ngày ${lunarInfo.lunarDay} tháng ${lunarInfo.lunarMonth} âm lịch, năm ${lunarInfo.canChiYear}, ngày ${lunarInfo.canChiDay}.`;
      } else {
        dateAnswer = `Hôm nay là ngày ${curDay} tháng ${curMonth} năm ${curYear} dương lịch, tức ngày ${lunarInfo.lunarDay} tháng ${lunarInfo.lunarMonth} năm ${lunarInfo.canChiYear} âm lịch (${lunarInfo.canChiDay}).`;
      }

      const dateB64 = Buffer.from(dateAnswer).toString('base64url');
      const ttsUrl = `https://vercel-backend-woad-seven.vercel.app/api/tts.mp3?b64=${dateB64}`;

      return res.status(200).json({
        success: true,
        query: cleanQuery,
        answer: dateAnswer,
        tts_url: ttsUrl,
        source: "Vietnamese Astronomical Lunar Calendar Engine"
      });
    }

    // ── 1. NẾU LÀ CÂU HỎI GIAO TIẾP / HỎI DANH TÍNH / CHÀO HỎI ──
    if (isConversationalOrIdentityQuery(cleanQuery)) {
      const chitChatPrompt = `Bạn là trợ lý AI thông minh NORI của ngôi nhà thông minh.
Người dùng đang trò chuyện với bạn: "${cleanQuery}".
Hãy trả lời một cách tự nhiên, lịch sự, thông minh và hóm hỉnh phù hợp với vai trò trợ lý AI NORI:
- Nếu người dùng hỏi bạn có biết họ là ai không (hoặc họ là ai): Hãy khẳng định bạn luôn nhận ra họ là chủ nhân của ngôi nhà thông minh này, và bạn luôn sẵn lòng hỗ trợ điều khiển thiết bị, phát nhạc hay giải đáp mọi kiến thức.
- Nếu người dùng hỏi bạn là ai/tên gì: Giới thiệu ngắn gọn bạn là Nori, trợ lý nhà thông minh đa năng của họ.
- Trình bày ngắn gọn từ 2 đến 3 câu trên cùng 1 đoạn duy nhất, TUYỆT ĐỐI KHÔNG xuống dòng, không dùng ký tự đặc biệt (*, #, :).`;

      let chitChatAnswer = await callGroqLLM(chitChatPrompt, 0.3, 200);
      if (!chitChatAnswer) {
        chitChatAnswer = "Chào bạn, tôi là trợ lý AI Nori của ngôi nhà thông minh. Tôi luôn sẵn sàng hỗ trợ bạn điều khiển thiết bị và cập nhật tin tức mỗi ngày.";
      }
      chitChatAnswer = chitChatAnswer.replace(/[\r\n]+/g, ' ').replace(/\s+/g, ' ').trim();

      const chitChatB64 = Buffer.from(chitChatAnswer).toString('base64url');
      const ttsUrl = `https://vercel-backend-woad-seven.vercel.app/api/tts.mp3?b64=${chitChatB64}`;

      return res.status(200).json({
        success: true,
        query: cleanQuery,
        answer: chitChatAnswer,
        tts_url: ttsUrl,
        source: "Nori Conversational Core"
      });
    }

    // ── 2. BỘ ĐỊNH TUYẾN NGỮ CẢNH ĐA NGUỒN (MULTI-SOURCE ROUTER) ──
    let extraContext = [];
    let primarySource = "Multi-Source Intelligence";

    const isWeatherQuery = lowerQuery.includes('thoi tiet') || lowerQuery.includes('nhiet do') || 
                           lowerQuery.includes('do am') || lowerQuery.includes('khi hau') || 
                           lowerQuery.includes('du bao') || lowerQuery.includes('troi co mua') ||
                           lowerQuery.includes('bao nhieu do') || lowerQuery.includes('weather');

    const isNewsOrEventQuery = lowerQuery.includes('tin tuc') || lowerQuery.includes('thoi su') || 
                               lowerQuery.includes('hom nay co gi') || lowerQuery.includes('diem tin') ||
                               lowerQuery.includes('su kien') || lowerQuery.includes('vua xay ra') ||
                               lowerQuery.includes('bao chi') || lowerQuery.includes('moi nhat');

    const isFinanceQuery = lowerQuery.includes('chung khoan') || lowerQuery.includes('co phieu') || 
                           lowerQuery.includes('gia vang') || lowerQuery.includes('tai chinh') ||
                           lowerQuery.includes('ty gia') || lowerQuery.includes('gia usd') ||
                           lowerQuery.includes('gia xang');

    // 2.1 Thu thập thời tiết OpenWeatherMap nếu là câu hỏi thời tiết
    if (isWeatherQuery) {
      const locTarget = extractWeatherLocation(cleanQuery) || "Da Nang";
      const weatherData = await fetchOpenWeatherData(locTarget);

      if (weatherData) {
        const weatherSummary = `[DỮ LIỆU KHÍ TƯỢNG THỜI GIAN THỰC TỪ OPENWEATHERMAP]:
- Địa điểm: ${weatherData.city}, ${weatherData.country}
- Nhiệt độ thực tế: ${weatherData.temp}°C (Cảm giác thực tế: ${weatherData.feels_like}°C)
- Biên độ nhiệt: Thấp nhất ${weatherData.temp_min}°C, Cao nhất ${weatherData.temp_max}°C
- Độ ẩm không khí: ${weatherData.humidity}%
- Tốc độ gió: ${weatherData.wind_speed} m/s (~${Math.round(weatherData.wind_speed * 3.6)} km/h)
- Áp suất khí quyển: ${weatherData.pressure} hPa
- Tình trạng bầu trời: ${weatherData.description}`;

        extraContext.push(weatherSummary);
        primarySource = "OpenWeatherMap Real-time Meteorological Station";
      }
    }

    // 2.2 Thu thập Bách khoa toàn thư Wikipedia Tiếng Việt chuẩn khoa học & Toàn văn
    const searchTerms = extractCoreKeywords(cleanQuery);
    const wikiData = await fetchWikiExtract(searchTerms);
    if (wikiData && wikiData.extract) {
      extraContext.push(`[DỮ LIỆU BÁCH KHOA TOÀN THƯ WIKIPEDIA TIẾNG VIỆT CHUẨN XÁC VỀ "${wikiData.title}"]:
- Tóm tắt học thuật / Định nghĩa khoa học: ${wikiData.extract}`);
      primarySource = "Vietnamese Wikipedia Official Knowledge Base";
    }

    // 2.3 Thu thập thông tin Kênh YouTube / Người theo dõi thời gian thực
    const isYouTubeQuery = lowerQuery.includes('youtube') || lowerQuery.includes('kênh') || lowerQuery.includes('vlog') ||
                           lowerQuery.includes('nguoi theo doi') || lowerQuery.includes('dang ky') || lowerQuery.includes('sub') ||
                           lowerQuery.includes('streamer') || lowerQuery.includes('tiktoker') || lowerQuery.includes('ca si');
    if (isYouTubeQuery) {
      const ytInfo = await fetchYouTubeChannelInfo(searchTerms);
      if (ytInfo) {
        extraContext.push(`[DỮ LIỆU KÊNH YOUTUBE & SỐ LƯỢNG NGƯỜI THEO DÕI THỜI GIAN THỰC]:\n${ytInfo}`);
        if (!primarySource.includes("Wikipedia")) primarySource = "YouTube Real-time Metrics";
      }
    }

    // 2.4 Thu thập DuckDuckGo Answer nếu có
    const ddgAnswer = await fetchDuckDuckGoAnswer(searchTerms[0] || cleanQuery);
    if (ddgAnswer && ddgAnswer.trim().length > 20) {
      extraContext.push(`[DỮ LIỆU TRI THỨC ĐỊNH NGHĨA TỪ DUCKDUCKGO]:\n${ddgAnswer}`);
    }

    // 2.5 Thu thập Tin tức thời sự & báo chí thời gian thực (Google News RSS & Báo chí)
    // Chỉ bổ sung nếu là tin thời sự hoặc chưa có dữ liệu bách khoa
    if (isNewsOrEventQuery || isFinanceQuery || extraContext.length === 0) {
      const googleNews = await fetchGoogleNewsRSS(searchTerms[0] || cleanQuery);
      if (googleNews && googleNews.length > 0) {
        extraContext.push("[DỮ LIỆU THỜI SỰ & BÁO CHÍ TIẾNG VIỆT THỜI GIAN THỰC TỪ GOOGLE NEWS]:\n" + googleNews.join("\n"));
        if (!primarySource.includes("Wikipedia")) primarySource = "Google News & Vietnamese Media";
      }
    }

    // 2.6 Thu thập thêm RSS chuyên mục tài chính / chứng khoán
    if (isFinanceQuery) {
      const cafeF = await fetchMajorNewspaperRSS('chung_khoan');
      if (cafeF.length > 0) {
        extraContext.push("[BẢN TIN THỊ TRƯỜNG CHỨNG KHOÁN & KINH TẾ CAFEF]:\n" + cafeF.join("\n"));
        primarySource = "CafeF Financial Market Feed";
      }
    } else if (isNewsOrEventQuery) {
      const vnExpress = await fetchMajorNewspaperRSS('thoi_su');
      if (vnExpress.length > 0) {
        extraContext.push("[BẢN TIN THỜI SỰ MỚI NHẤT TỪ BÁO TUỔI TRẺ & VNEXPRESS]:\n" + vnExpress.join("\n"));
      }
    }

    const contextText = extraContext.join('\n\n');

    const prompt = `Bạn là trợ lý AI thông minh NORI của ngôi nhà thông minh.
Người dùng đang hỏi câu hỏi: "${cleanQuery}".

THỜI GIAN THỰC TẾ HÔM NAY TẠI VIỆT NAM (Múi giờ GMT+7):
- Dương lịch: Ngày ${curDay}/${curMonth}/${curYear}.
- Âm lịch: Ngày ${lunarInfo.lunarDay} tháng ${lunarInfo.lunarMonth} năm ${lunarInfo.canChiYear} (${lunarInfo.canChiDay}).

${contextText ? `DỮ LIỆU TRA CỨU TRI THỨC VÀ BÁCH KHOA TOÀN THƯ THỰC TẾ:\n${contextText}\n` : ''}

QUY TẮC PHẢN HỒI KHOA HỌC, ĐẦY ĐỦ VÀ CHÍNH XÁC:
1. TRẢ LỜI ĐẦY ĐỦ, CỤ THỂ VÀ TỰ TIN VỀ MỌI THÔNG TIN ĐƯỢC HỎI:
   - Đối với câu hỏi về nhân vật, YouTuber, nghệ sĩ, kênh truyền thông: Nêu rõ tên thật, nghệ danh, năm sinh/quê quán, nội dung hoạt động nổi bật, số lượng người đăng ký / người theo dõi thực tế trên YouTube/mạng xã hội và các thành tựu (Nút Vàng, Nút Kim Cương nếu có).
   - Đối với câu hỏi về động vật, thực vật, sinh học: BẮT BUỘC trả lời đúng phân loại sinh học chuẩn xác (Ví dụ: Rắn là loài động vật bò sát có vảy, không chân, thuộc phân bộ Serpentes; Cá heo là động vật có vú thuộc bộ Cá voi; Voi là động vật có vú thuộc bộ Có vòi). TUYỆT ĐỐI KHÔNG nhầm lẫn hoặc gán ghép các loài khác nhau.
   - Đối với câu hỏi về hóa học, y học, dược phẩm (như Natri Clorid 0,9%, Nước muối sinh lý): Nêu rõ thành phần, công thức, cơ chế nồng độ đẳng trương, công dụng và cách dùng an toàn.
   - Đối với câu hỏi về địa lý, lịch sử, văn hóa, nhân vật: Cung cấp thông tin chuẩn xác theo bách khoa toàn thư.
   - Đối với câu hỏi về thời tiết: Sử dụng trực tiếp số liệu khí tượng OpenWeatherMap ở trên để báo cáo cụ thể.
2. TUYỆT ĐỐI KHÔNG TRẢ LỜI NÉ TRÁNH HOẶC TỪ CHỐI:
   - CẤM nói: "tôi không có dữ liệu", "dữ liệu không cung cấp", "hãy truy cập trang khác", "cần tìm kiếm thêm".
   - Hãy kết hợp linh hoạt dữ liệu tra cứu ở trên cùng kho tri thức khổng lồ được đào tạo của bạn để trả lời cụ thể, chi tiết, chính xác từng con số và sự kiện cho người dùng.
3. TUYỆT ĐỐI BẮT ĐẦU TRẢ LỜI NGAY, LOẠI BỎ LỜI CHÀO VÀ DẪN DẮT RƯỜM RÀ:
   - CẤM nói: "Xin chào", "Xin chào ông chủ", "Tôi là Nori", "Tôi sẵn sàng trả lời", "Tôi hy vọng thông tin này".
   - Bắt đầu trực tiếp bằng nội dung kiến thức, thông tin sự kiện hoặc số liệu cụ thể.
4. ĐỊNH DẠNG: Trình bày từ 3 đến 5 câu súc tích, đầy đủ ý nghĩa trên CÙNG 1 ĐOẠN VĂN DUY NHẤT, TUYỆT ĐỐI KHÔNG dùng ký tự xuống dòng (Enter), KHÔNG dùng ký tự đặc biệt (*, #, :), không dùng danh sách gạch đầu dòng để loa ESP32 phát giọng nói liền mạch trơn tru.`;

    let answer = await callGroqLLM(prompt, 0.2, 450);

    if (!answer) {
      if (wikiData && wikiData.extract) {
        answer = `${wikiData.title} là ${wikiData.extract.substring(0, 300)}`;
      } else {
        answer = "Hiện tại hệ thống tri thức đang cập nhật, bạn vui lòng hỏi lại sau giây lát.";
      }
    }

    answer = answer.replace(/[\r\n]+/g, ' ').replace(/\s+/g, ' ');
    // Lọc bỏ triệt để các lời chào mở đầu nếu AI còn sót
    answer = answer.replace(/^(xin chào(\s+ông chủ|\s+bạn)?[\.,!]?\s*)+(tôi là nori[\.,!]?\s*)?/gi, '')
                   .replace(/^(tôi là nori[\.,!]?\s*)+/gi, '')
                   .replace(/^(tôi sẵn sàng trả lời câu hỏi( của bạn| của ông chủ)?( về)?[\.,!]?\s*)/gi, '')
                   .trim();

    const ansB64 = Buffer.from(answer).toString('base64url');
    const ttsUrl = `https://vercel-backend-woad-seven.vercel.app/api/tts.mp3?b64=${ansB64}`;

    return res.status(200).json({
      success: true,
      query: cleanQuery,
      answer: answer,
      tts_url: ttsUrl,
      source: primarySource
    });

  } catch (error) {
    console.error("Lỗi search API:", error.message);
    return res.status(500).json({
      success: false,
      error: 'Lỗi tổng hợp thông tin: ' + error.message
    });
  }
};
