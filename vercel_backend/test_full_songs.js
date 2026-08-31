const axios = require('axios');

const FULL_SONG_DATABASE = [
  {
    keywords: ["khong the say", "hieuthuhai", "hieu thu hai"],
    title: "Không Thể Say",
    artist: "HIEUTHUHAI",
    url: "https://ia801502.us.archive.org/28/items/khong-the-say-hieuthuhai/KhongTheSay.mp3"
  },
  {
    keywords: ["am tham ben em", "am tham", "son tung"],
    title: "Âm Thầm Bên Em",
    artist: "Sơn Tùng M-TP",
    url: "https://ia801509.us.archive.org/27/items/AmThamBenEmPiano/AmThamBenEm.mp3"
  },
  {
    keywords: ["cat doi noi sau", "cat doi", "tang duy tan"],
    title: "Cắt Đôi Nỗi Sầu",
    artist: "Tăng Duy Tân",
    url: "https://ia801505.us.archive.org/29/items/cat-doi-noi-sau-tang-duy-tan/CatDoiNoiSau.mp3"
  },
  {
    keywords: ["lac troi"],
    title: "Lạc Trôi",
    artist: "Sơn Tùng M-TP",
    url: "https://ia801602.us.archive.org/31/items/LacTroiAcoustic/LacTroi.mp3"
  },
  {
    keywords: ["noi nay co anh"],
    title: "Nơi Này Có Anh",
    artist: "Sơn Tùng M-TP",
    url: "https://ia801503.us.archive.org/15/items/NoiNayCoAnhAcoustic/NoiNayCoAnh.mp3"
  },
  {
    keywords: ["di ve nha", "den vau", "ve nha"],
    title: "Đi Về Nhà",
    artist: "Đen Vâu x JustaTee",
    url: "https://ia801508.us.archive.org/12/items/DiVeNhaLofi/DiVeNha.mp3"
  },
  {
    keywords: ["nang tho", "hoang dung"],
    title: "Nàng Thơ",
    artist: "Hoàng Dũng",
    url: "https://ia801506.us.archive.org/14/items/nang-tho-hoang-dung/NangTho.mp3"
  },
  {
    keywords: ["sau tim thiep hong", "bolero", "sau tim"],
    title: "Sầu Tím Thiệp Hồng",
    artist: "Quang Lê & Lệ Quyên",
    url: "https://ia801500.us.archive.org/20/items/SauTimThiepHongBolero/SauTimThiepHong.mp3"
  }
];

async function checkStreams() {
  for (const song of FULL_SONG_DATABASE) {
    try {
      const res = await axios.head(song.url, { timeout: 4000 });
      console.log(`✅ [${song.title} - ${song.artist}] Status: ${res.status}, Type: ${res.headers['content-type']}, Size: ${res.headers['content-length']} bytes`);
    } catch (e) {
      console.log(`❌ [${song.title}] Error:`, e.message);
    }
  }
}

checkStreams();
