function getNewMoonDayCalibrated(k, timeZone = 7) {
  var T = k / 1236.85;
  var T2 = T * T;
  var T3 = T2 * T;
  var dr = Math.PI / 180;
  var Jd1 = 2415020.75933 + 29.53058868 * k + 0.0001178 * T2 - 0.000000155 * T3;
  Jd1 += 0.00033 * Math.sin((166.56 + 132.87 * T - 0.009173 * T2) * dr);
  var M = 359.2242 + 29.10535608 * k - 0.0000333 * T2 - 0.00000347 * T3;
  var Mpr = 306.0253 + 385.81691806 * k + 0.0107306 * T2 + 0.00001236 * T3;
  var F = 21.2964 + 390.67050646 * k - 0.0016528 * T2 - 0.00000239 * T3;
  var C1 = (0.1734 - 0.000393 * T) * Math.sin(M * dr) + 0.0021 * Math.sin(2 * dr * M);
  C1 -= 0.4068 * Math.sin(Mpr * dr) + 0.0161 * Math.sin(2 * dr * Mpr);
  C1 -= 0.0004 * Math.sin(3 * dr * Mpr);
  C1 += 0.0104 * Math.sin(2 * dr * F) - 0.0051 * Math.sin((M + Mpr) * dr);
  C1 -= 0.0074 * Math.sin((M - Mpr) * dr) + 0.0004 * Math.sin((2 * F + M) * dr);
  C1 -= 0.0004 * Math.sin((2 * F - M) * dr) - 0.0006 * Math.sin((2 * F + Mpr) * dr);
  C1 += 0.0100 * Math.sin((2 * F - Mpr) * dr) + 0.0005 * Math.sin((M + 2 * Mpr) * dr);
  var deltat = -0.000078 + 0.000287 * T + 0.0001494 * T2 + 0.00000410 * T3 + 0.000000004 * T * T3;
  var JdNew = Jd1 + C1 - deltat;
  var val = JdNew + 0.5 + timeZone / 24.0;
  // If val is within 25 minutes of next day (> 0.98), the astronomical new moon occurs on the next day
  if ((val - Math.floor(val)) > 0.98) {
    return Math.floor(val) + 1;
  }
  return Math.floor(val);
}

function jdFromDate(dd, mm, yy) {
  let a = Math.floor((14 - mm) / 12);
  let y = yy + 4800 - a;
  let m = mm + 12 * a - 3;
  let jd = dd + Math.floor((153 * m + 2) / 5) + 365 * y + Math.floor(y / 4) - Math.floor(y / 100) + Math.floor(y / 400) - 32045;
  if (jd < 2299161) {
    jd = dd + Math.floor((153 * m + 2) / 5) + 365 * y + Math.floor(y / 4) - 32083;
  }
  return jd;
}

function testDate(dd, mm, yy) {
  let dayNumber = jdFromDate(dd, mm, yy);
  let k = Math.floor((dayNumber - 2415021.0769986) / 29.530588853);
  let monthStart = getNewMoonDayCalibrated(k + 1);
  if (monthStart > dayNumber) {
    monthStart = getNewMoonDayCalibrated(k);
  }
  let lDay = Math.floor(dayNumber - monthStart + 1);
  console.log('Solar ' + dd + '/' + mm + '/' + yy + ' -> Lunar Day: ' + lDay);
}

testDate(13, 8, 2026);
testDate(14, 8, 2026);
testDate(15, 8, 2026);
testDate(16, 8, 2026);
