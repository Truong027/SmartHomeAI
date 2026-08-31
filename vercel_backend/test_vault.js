const MASTER_STUDIO_VAULT = [
  {
    keywords: ["khong the say", "hieuthuhai", "hieu thu hai"],
    title: "Không Thể Say",
    artist: "HIEUTHUHAI",
    duration: 228,
    url: "https://cf-media.sndcdn.com/aWZNERPXzLFF.128.mp3"
  },
  {
    keywords: ["cat doi noi sau", "tang duy tan"],
    title: "Cắt Đôi Nỗi Sầu",
    artist: "Tăng Duy Tân",
    duration: 196,
    url: "https://cf-media.sndcdn.com/bLY8tvXAgxoZ.128.mp3"
  },
  {
    keywords: ["am tham ben em", "son tung"],
    title: "Âm Thầm Bên Em",
    artist: "Sơn Tùng M-TP",
    duration: 291,
    url: "https://cf-media.sndcdn.com/0x2TnIEVkrLR.128.mp3"
  }
];

function findInVault(cleanQuery) {
  const q = cleanQuery.toLowerCase();
  for (const item of MASTER_STUDIO_VAULT) {
    if (item.keywords.some(kw => q.includes(kw))) {
      return item;
    }
  }
  return null;
}

console.log('Vault result 1:', findInVault('khong the say hieu thu hai'));
console.log('Vault result 2:', findInVault('cat doi noi sau'));
console.log('Vault result 3:', findInVault('am tham ben em'));
