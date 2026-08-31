const https = require('https');
const axios = require('axios');
const cors = require('cors');

const httpsAgent = new https.Agent({ rejectUnauthorized: false });
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

function removeAccents(str) {
  return String(str || '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .replace(/đ/g, 'd')
    .replace(/Đ/g, 'D')
    .toLowerCase()
    .trim();
}

// Làm sạch triệt để các câu khẩu lệnh giao tiếp mà không làm mất từ khóa trong bài hát (như "của", "và")
function cleanMusicQuery(q) {
  let cleaned = String(q).replace(/[.,\/#!$%\^&\*;:{}=\-_`~()]/g, ' ');
  let prev = '';
  while (cleaned !== prev) {
    prev = cleaned;
    cleaned = cleaned
      .replace(/^(hãy mở|hay mo|hãy bật|hay bat|hãy phát|hay phat|mở giúp tôi|mo giup toi|bật giúp tôi|bat giup toi|phát giúp tôi|phat giup toi|mở cho tôi|mo cho toi|bật cho tôi|bat cho toi|phát cho tôi|phat cho toi|mở hộ tôi|mo ho toi|bật hộ tôi|bat ho toi|phát hộ tôi|phat ho toi|cho tôi nghe|cho tôi nghe|cho mình nghe|cho minh nghe|cho nghe|mở bài hát|mo bai hat|bật bài hát|bat bai hat|phát bài hát|phat bai hat|mở bài nhạc|mo bai nhac|bật bài nhạc|bat bai nhac|phát bài nhạc|phat bai nhac|mở bài|mo bai|bật bài|bat bai|phát bài|phat bai|hát bài|hat bai|nghe bài|nghe bai|mở ca khúc|mo ca khuc|bật ca khúc|bat ca khuc|phát ca khúc|phat ca khuc|mở nhạc|mo nhac|bật nhạc|bat nhac|phát nhạc|phat nhac|nghe nhạc|nghe nhac|tìm bài|tim bai|tìm nhạc|tim nhac|hát nhạc|hat nhac|play)\s+/gi, '')
      .replace(/^(cho tôi|cho toi|cho mình|cho minh|cho nghe|cho em|cho anh|tôi muốn|toi muon|mình muốn|minh muon|muốn nghe|muon nghe|muốn bật|muon bat|muốn mở|muon mo|muốn phát|muon phat)\s+/gi, '')
      .replace(/^(bài hát|bai hat|bài nhạc|bai nhac|ca khúc|ca khuc)\s+/gi, '')
      .replace(/\s+(đi|nào|nao|nha|nhé|nhe|với|voi|ạ|a|nhé bạn|nhe ban|nha bạn|nha ban)$/gi, '')
      .trim();
  }

  // Tự động sửa các lỗi phát âm / phiên âm tiếng Anh thông dụng
  cleaned = cleaned
    .replace(/\bbay bay justin\b/gi, 'Baby Justin Bieber')
    .replace(/\bbay bay\b/gi, 'Baby')
    .replace(/\bbay bi\b/gi, 'Baby')
    .replace(/\bbai bi\b/gi, 'Baby')
    .replace(/\bdet ba xi to\b/gi, 'Despacito')
    .replace(/\bsep op du\b/gi, 'Shape of You')
    .replace(/\bphay dit\b/gi, 'Faded')
    .replace(/\s+(của|cua|do|bởi|boi|ca sĩ|ca si)\s+/gi, ' ');

  while (cleaned.includes('  ')) cleaned = cleaned.replace('  ', ' ');
  return cleaned || q;
}

// ── BƯỚC 1: NHẬN DIỆN CHÍNH XÁC TÊN BÀI HÁT & TÁC GIẢ/CA SĨ (CANONICAL METADATA) ──
async function identifyCanonicalSong(cleanQuery) {
  const normClean = removeAccents(cleanQuery);
  const isRemixReq = normClean.includes('remix') || normClean.includes('rmx');

  const tasks = [
    // 1. ZingMP3 Autocomplete API
    (async () => {
      const zUrl = `https://ac.mp3.zing.vn/complete?type=artist,song,key,code&num=6&query=${encodeURIComponent(cleanQuery)}`;
      const zRes = await axios.get(zUrl, {
        httpsAgent: httpsAgent,
        headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36' },
        timeout: 2500
      });
      const songs = zRes.data?.data?.[0]?.song;
      if (songs && songs.length > 0) {
        const exactMatch = songs.find(s => removeAccents(s.name) === normClean || s.name.toLowerCase() === cleanQuery.toLowerCase());
        const selected = exactMatch || songs[0];
        return {
          title: selected.name,
          artist: selected.artist,
          duration: parseInt(selected.duration) || 0,
          source: 'ZingMP3 Official DB'
        };
      }
      throw new Error('No song in Zing');
    })(),

    // 2. iTunes Search API
    (async () => {
      const iUrl = `https://itunes.apple.com/search?term=${encodeURIComponent(cleanQuery)}&country=VN&media=music&entity=song&limit=6`;
      const iRes = await axios.get(iUrl, {
        headers: { 'User-Agent': 'Mozilla/5.0' },
        timeout: 2500
      });
      if (iRes.data?.results?.length > 0) {
        let chosen = iRes.data.results[0];
        if (!isRemixReq) {
          const nonRemix = iRes.data.results.find(r => !r.trackName.toLowerCase().includes('remix') && !r.trackName.toLowerCase().includes('karaoke'));
          if (nonRemix) chosen = nonRemix;
        }
        let cleanTitle = chosen.trackName.replace(/\s*\([^)]*remix[^)]*\)/gi, '').trim();
        return {
          title: cleanTitle || chosen.trackName,
          artist: chosen.artistName,
          duration: Math.round(chosen.trackTimeMillis / 1000) || 0,
          source: 'Apple Music Database'
        };
      }
      throw new Error('No song in iTunes');
    })()
  ];

  try {
    return await Promise.any(tasks);
  } catch (err) {
    return null;
  }
}

// ── BƯỚC 2: CHẤM ĐIỂM BẢN THU (ƯU TIÊN BẢN CHÍNH HÃNG PHÒNG THU, LOẠI BỎ LIVE/REMIX RÁC) ──
function calculateTrackScore(track, targetTitle, targetArtist, rawQuery) {
  const tTitle = (track.title || '').toLowerCase();
  const tUser = (track.user?.username || '').toLowerCase();
  const qTitle = targetTitle.toLowerCase();

  const normTTitle = removeAccents(track.title);
  const normTUser = removeAccents(track.user?.username);
  const normQTitle = removeAccents(targetTitle);
  const normQArtist = removeAccents(targetArtist);
  const normRaw = removeAccents(rawQuery);

  const isRemixRequested = normRaw.includes('remix') || normRaw.includes('rmx');
  const isCoverRequested = normRaw.includes('cover') || normRaw.includes('hat lai');
  const isLiveRequested = normRaw.includes('live') || normRaw.includes('san khau');

  let score = 0;

  // 1. Phạt thời lượng quá ngắn (<75s) hoặc quá dài (>600s)
  const durSec = Math.round(track.duration / 1000);
  if (durSec < 75 || durSec > 600) return -999;
  if (durSec >= 150 && durSec <= 330) score += 30; // Thời lượng chuẩn 2.5 - 5.5 phút

  // 2. Khớp chính xác tên bài hát (cả có dấu và không dấu)
  const titleMatched = tTitle.includes(qTitle) || normTTitle.includes(normQTitle);
  if (titleMatched) {
    score += 60;
    const cleanNormTTitle = normTTitle.replace(/[\(\)\[\]\-_|]/g, ' ').replace(/\s+/g, ' ').trim();
    if (cleanNormTTitle === normQTitle || cleanNormTTitle === `${normQTitle} ${normQArtist}` || cleanNormTTitle === `${normQArtist} ${normQTitle}`) {
      score += 80;
    } else if (normTTitle.startsWith(normQTitle) || normTTitle.includes(' - ' + normQTitle) || normTTitle.includes(normQTitle + ' -')) {
      score += 40;
    }
  }

  // 3. Khớp tên nghệ sĩ / ca sĩ sáng tác / biểu diễn
  if (normQArtist) {
    const artistTokens = normQArtist.split(/[,&x/]/).map(a => a.trim()).filter(a => a.length > 1);
    for (const at of artistTokens) {
      if (normTTitle.includes(at) || normTUser.includes(at)) {
        score += 50;
        break;
      }
    }
  }

  // 4. Trừ điểm nặng các bản ghép bài (Mashup), Live, Sân khấu, Remix không mong muốn
  if (normTTitle.includes(' mashup') || normTTitle.includes('medley')) score -= 60;
  if (!isRemixRequested && (normTTitle.includes('remix') || normTTitle.includes('rmx') || normTTitle.includes('t-golden'))) score -= 50;
  if (!isCoverRequested && (normTTitle.includes('cover') || normTTitle.includes('hat lai'))) score -= 40;
  if (!isLiveRequested && (normTTitle.includes('live') || normTTitle.includes('san khau') || normTTitle.includes('fancam') || normTTitle.includes('concert') || normTTitle.includes('gala') || normTTitle.includes('fest'))) score -= 50;
  if (normTTitle.includes('karaoke') || normTTitle.includes('beat') || normTTitle.includes('instrumental')) score -= 70;
  if (normTTitle.includes('official') || normTTitle.includes('audio') || normTTitle.includes('mv') || normTTitle.includes('original')) score += 25;

  // 5. Thưởng tiêu đề súc tích chuẩn studio, phạt các tiêu đề chèn thêm nhiều từ thừa
  const cleanNormTTitle = normTTitle.replace(/[\(\)\[\]\-_|]/g, ' ').replace(/\s+/g, ' ').trim();
  const qWords = (normQTitle + ' ' + normQArtist).split(' ').filter(w => w.length > 0);
  const tWords = cleanNormTTitle.split(' ').filter(w => w.length > 0);
  const extraWords = tWords.filter(w => !qWords.includes(w) && !['official', 'audio', 'mv', 'ft', 'x', 'lyrics', 'video'].includes(w)).length;
  if (extraWords > 0) score -= extraWords * 8;

  return score;
}

// ── BƯỚC 3: LẤY DYNAMIC SOUNDCLOUD CLIENT ID ──
let cachedClientId = "pJ6Fj6roW2KRzWAOwGj6kkQ8VRBJjyBD";
let lastClientIdFetch = Date.now();

async function getSoundCloudClientId() {
  if (cachedClientId && (Date.now() - lastClientIdFetch < 1800000)) {
    return cachedClientId;
  }

  try {
    const res = await axios.get('https://soundcloud.com', {
      headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36' },
      timeout: 5000
    });
    const urls = [...res.data.matchAll(/src="([^"]+)"/g)].map(m => m[1]).filter(u => u.includes('sndcdn.com'));
    
    for (const u of urls.slice(0, 8)) {
      try {
        const js = (await axios.get(u, { timeout: 4000 })).data;
        const matches = [...js.matchAll(/client_id[:=]"([a-zA-Z0-9]{32})"/g)];
        if (matches.length > 0) {
          cachedClientId = matches[0][1];
          lastClientIdFetch = Date.now();
          return cachedClientId;
        }
      } catch (e) {}
    }
  } catch (err) {
    console.warn("Lỗi lấy dynamic SoundCloud client ID:", err.message);
  }
  return cachedClientId;
}

// ── BƯỚC 4: TÌM KIẾM LUỒNG ÂM THANH PROGRESSIVE MP3 CHUẨN ──
async function resolveAudioTrack(rawQuery, canonicalInfo) {
  const clientId = await getSoundCloudClientId();
  if (!clientId) return null;

  const clean = cleanMusicQuery(rawQuery);
  const searchTerms = [];

  if (canonicalInfo) {
    const cleanArtist = canonicalInfo.artist.replace(/[,&/x+]/g, ' ').replace(/\s+/g, ' ').trim();
    const firstArtist = canonicalInfo.artist.split(/[,&/x+]/)[0].trim();
    
    if (firstArtist && firstArtist !== cleanArtist) {
      searchTerms.push(`${canonicalInfo.title} ${firstArtist}`);
    }
    searchTerms.push(`${canonicalInfo.title} ${cleanArtist}`);
    searchTerms.push(`${canonicalInfo.title}`);
  }
  searchTerms.push(clean);

  for (const term of searchTerms) {
    if (!term || term.length < 2) continue;

    try {
      const sUrl = `https://api-v2.soundcloud.com/search/tracks?q=${encodeURIComponent(term)}&client_id=${clientId}&limit=15`;
      const res = await axios.get(sUrl, {
        headers: { 'User-Agent': 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36' },
        timeout: 6000
      });

      const collection = res.data?.collection || [];
      const validTracks = collection.filter(t => {
        const durSec = Math.round(t.duration / 1000);
        const hasProg = t.media && t.media.transcodings && t.media.transcodings.some(tc => tc.format && tc.format.protocol === 'progressive');
        return hasProg && durSec >= 75 && durSec <= 600;
      });

      if (validTracks.length > 0) {
        const targetTitle = canonicalInfo ? canonicalInfo.title : clean;
        const targetArtist = canonicalInfo ? canonicalInfo.artist : '';

        validTracks.sort((a, b) => calculateTrackScore(b, targetTitle, targetArtist, rawQuery) - calculateTrackScore(a, targetTitle, targetArtist, rawQuery));

        for (const chosen of validTracks.slice(0, 3)) {
          const tc = chosen.media.transcodings.find(t => t.format && t.format.protocol === 'progressive');
          if (tc) {
            try {
              const streamInfo = await axios.get(`${tc.url}?client_id=${clientId}`, { timeout: 3500 });
              if (streamInfo.data && streamInfo.data.url) {
                return {
                  title: canonicalInfo ? canonicalInfo.title : chosen.title,
                  artist: canonicalInfo ? canonicalInfo.artist : (chosen.user?.username || 'Nghệ sĩ'),
                  trackTitle: chosen.title,
                  duration: Math.round(chosen.duration / 1000),
                  streamUrl: streamInfo.data.url,
                  source: canonicalInfo ? `${canonicalInfo.source} → Studio Master Direct` : 'SoundCloud Studio'
                };
              }
            } catch (stErr) {
              console.warn(`Lỗi lấy stream cho track "${chosen.title}":`, stErr.message);
            }
          }
        }
      }
    } catch (err) {
      console.warn(`Lỗi tìm kiếm term "${term}":`, err.message);
    }
  }

  return null;
}

module.exports = async function handler(req, res) {
  if (!req.headers) req.headers = {};
  await runMiddleware(req, res, corsMiddleware);

  const rawQuery = req.query.q || (req.body && (req.body.q || req.body.song_name));

  if (!rawQuery || String(rawQuery).trim() === '') {
    return res.status(400).json({ success: false, error: 'Thiếu từ khóa tìm kiếm bài hát.' });
  }

  const cleanQuery = cleanMusicQuery(rawQuery);
  console.log(`🎙️ [Smart Music Resolver] User Query: "${rawQuery}" -> Cleaned: "${cleanQuery}"`);

  try {
    // 1. Nhận diện tác giả, ca sĩ và tên bài hát chính xác chuẩn bách khoa
    const canonicalInfo = await identifyCanonicalSong(cleanQuery);
    if (canonicalInfo) {
      console.log(`🎯 [Identified Song] ${canonicalInfo.title} - ${canonicalInfo.artist} (${canonicalInfo.source})`);
    } else {
      console.log(`ℹ️ [Identified Song] Không tìm thấy metadata chuẩn, tìm trực tiếp theo query.`);
    }

    // 2. Tìm bản thu Studio Progressive MP3 chuẩn
    const song = await resolveAudioTrack(rawQuery, canonicalInfo);

    if (song && song.streamUrl) {
      console.log(`✅ [Selected Track] ${song.title} - ${song.artist} (${song.duration}s) | Raw: "${song.trackTitle}"`);

      return res.status(200).json({
        success: true,
        title: song.title,
        artist: song.artist,
        full_title: `${song.title} - ${song.artist}`,
        duration: song.duration,
        stream_url: song.streamUrl, // Direct high-speed CDN URL for ESP32
        raw_url: song.streamUrl,
        source: song.source
      });
    }

    // 3. Fallback bản nhạc V-Pop phổ biến nếu không tìm thấy bài
    const fallbackSong = await resolveAudioTrack("Lạc trôi Sơn Tùng M-TP", { title: "Lạc Trôi", artist: "Sơn Tùng M-TP", source: "Fallback" });
    if (fallbackSong && fallbackSong.streamUrl) {
      return res.status(200).json({
        success: true,
        title: fallbackSong.title,
        artist: fallbackSong.artist,
        full_title: `${fallbackSong.title} - ${fallbackSong.artist}`,
        duration: fallbackSong.duration,
        stream_url: fallbackSong.streamUrl,
        raw_url: fallbackSong.streamUrl,
        source: "V-Pop Master Backup"
      });
    }

    return res.status(404).json({
      success: false,
      error: 'Không tìm thấy bản thu MP3 phù hợp.'
    });

  } catch (error) {
    console.error("Lỗi khi tìm kiếm nhạc:", error.message);
    return res.status(500).json({
      success: false,
      error: 'Lỗi tìm kiếm âm nhạc: ' + error.message
    });
  }
};
