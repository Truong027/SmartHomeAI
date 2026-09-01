/**
 * NORI AI Hub — Smart Home Terminal Logic
 * Hallmark Atmospheric Workbench Controller
 */

// 1. Firebase Realtime Configuration (Đầy đủ cấu hình để xác thực hoạt động 100%)
const firebaseConfig = {
    apiKey: "AIzaSyDND5fdH_tduPrnFHPsAo2Ggxzu1zJk18o",
    authDomain: "esp32app-30335.firebaseapp.com",
    databaseURL: "https://esp32app-30335-default-rtdb.asia-southeast1.firebasedatabase.app/",
    projectId: "esp32app-30335",
    storageBucket: "esp32app-30335.appspot.com",
    messagingSenderId: "30335",
    appId: "1:30335:web:esp32hub"
};

// Initialize Firebase
if (!firebase.apps.length) {
    firebase.initializeApp(firebaseConfig);
}
const db = firebase.database();
const auth = firebase.auth();

// 2. DOM Elements
const statusIndicator = document.getElementById('status-indicator');
const statusText = document.getElementById('status-text');
const refreshBtn = document.getElementById('refresh-btn');
const micBtn = document.getElementById('mic-btn');
const micTriggerWrap = document.querySelector('.mic-trigger-wrap');
const transcriptBox = document.getElementById('transcript');
const ttsIndicator = document.getElementById('tts-indicator');
const commandForm = document.getElementById('text-command-form');
const commandInput = document.getElementById('command-input');

// Telemetry Elements
const webIndoorTemp = document.getElementById('web-indoor-temp');
const webIndoorHum = document.getElementById('web-indoor-hum');
const webOutTemp = document.getElementById('web-out-temp');
const webOutDesc = document.getElementById('web-out-desc');
const webPressure = document.getElementById('web-pressure');
const webOutHum = document.getElementById('web-out-hum');
const webOutWind = document.getElementById('web-out-wind');
const webAiAdvice = document.getElementById('web-ai-advice');
const liveTimeEl = document.getElementById('live-time');
const rtcTimeEl = document.getElementById('rtc-time');
const rtcDateSolarEl = document.getElementById('rtc-date-solar');
const rtcDateLunarEl = document.getElementById('rtc-date-lunar');

// Device Controls
const ledSlider = document.getElementById('led-slider');
const ledVal = document.getElementById('led-val');
const volSlider = document.getElementById('volume-slider');
const volVal = document.getElementById('volume-val');
const relay1Toggle = document.getElementById('relay1-toggle');
const relay1Card = document.getElementById('relay1-card');
const relay1Status = document.getElementById('relay1-status');
const relay2Toggle = document.getElementById('relay2-toggle');
const relay2Card = document.getElementById('relay2-card');
const relay2Status = document.getElementById('relay2-status');

// Daikin AC Controls
const acPowerToggle = document.getElementById('ac-power');
const acTempSlider = document.getElementById('ac-temp-slider');
const acTempVal = document.getElementById('ac-temp-val');
const acTempMinusBtn = document.getElementById('ac-temp-minus');
const acTempPlusBtn = document.getElementById('ac-temp-plus');
const acFanVal = document.getElementById('ac-fan-val');
const acFanSlider = document.getElementById('ac-fan-slider');
const acDeck = document.getElementById('ac-deck');

// State variables
let isRecording = false;
let recognition = null;
let envChart = null;

// ==========================================================================
// 3. Application Initialization & Firebase Authentication
// ==========================================================================
async function initApp() {
    updateLiveClock();
    setInterval(updateLiveClock, 1000);

    // Initialize UI and listeners immediately
    setupSliders();
    setupRelays();
    setupDaikinAC();
    setupSensorsAndWeather();
    setupSpeechRecognition();
    setupCommandForm();
    setupQuickChips();
    setupPresets();

    try {
        setConnectionState('connecting', 'Đang kết nối...');
        
        // Attempt auth if configured
        if (auth) {
            try {
                await auth.signInWithEmailAndPassword("admin@esp32.local", "123456");
                console.log("✅ Firebase Auth: Đăng nhập thành công với tài khoản admin@esp32.local");
            } catch (authErr) {
                console.warn("Email auth fallback:", authErr.message);
                try {
                    await auth.signInAnonymously();
                    console.log("✅ Firebase Auth: Đăng nhập ẩn danh thành công");
                } catch (anonErr) {
                    console.warn("Anonymous auth fallback:", anonErr.message);
                }
            }
        }

        // Monitor Firebase RTDB connection status
        db.ref('.info/connected').on('value', (snap) => {
            if (snap.val() === true) {
                setConnectionState('online', 'Đã kết nối');
            } else {
                setConnectionState('offline', 'Mất kết nối');
            }
        });
    } catch (error) {
        setConnectionState('offline', 'Lỗi kết nối');
        console.error("Firebase Init Error:", error);
    }
}

function setConnectionState(state, text) {
    if (!statusIndicator || !statusText) return;
    statusIndicator.className = `status-pill ${state}`;
    statusText.innerText = text;
}

if (refreshBtn) {
    refreshBtn.addEventListener('click', () => {
        refreshBtn.querySelector('i').classList.add('fa-spin');
        initApp().finally(() => {
            setTimeout(() => refreshBtn.querySelector('i').classList.remove('fa-spin'), 600);
        });
    });
}

function updateLiveClock() {
    if (!liveTimeEl) return;
    const now = new Date();
    const h = String(now.getHours()).padStart(2, '0');
    const m = String(now.getMinutes()).padStart(2, '0');
    const s = String(now.getSeconds()).padStart(2, '0');
    liveTimeEl.innerText = `${h}:${m}:${s}`;
}

// ==========================================================================
// 4. Sliders & Device Controls
// ==========================================================================
function setupSliders() {
    // LED Brightness (0-255)
    db.ref('/ESP32_AI_Hub/settings/ledBrightness').on('value', (snapshot) => {
        const val = snapshot.val();
        if (val !== null) {
            ledSlider.value = val;
            ledVal.innerText = Math.round((val / 255) * 100) + '%';
        }
    });

    ledSlider.addEventListener('input', (e) => {
        const val = parseInt(e.target.value, 10);
        ledVal.innerText = Math.round((val / 255) * 100) + '%';
        db.ref('/ESP32_AI_Hub/settings/ledBrightness').set(val);
    });

    // Audio Volume (0-21)
    db.ref('/ESP32_AI_Hub/settings/audioVolume').on('value', (snapshot) => {
        const val = snapshot.val();
        if (val !== null) {
            volSlider.value = val;
            volVal.innerText = Math.round((val / 21) * 100) + '%';
        }
    });

    volSlider.addEventListener('input', (e) => {
        const val = parseInt(e.target.value, 10);
        volVal.innerText = Math.round((val / 21) * 100) + '%';
        db.ref('/ESP32_AI_Hub/settings/audioVolume').set(val);
    });
}

// Relays
function setupRelays() {
    // Relay 1
    db.ref('/ESP32_AI_Hub/relay1').on('value', (snapshot) => {
        const val = snapshot.val() === true;
        relay1Toggle.checked = val;
        updateRelayCard(relay1Card, relay1Status, val);
    });

    relay1Toggle.addEventListener('change', (e) => {
        const checked = e.target.checked;
        updateRelayCard(relay1Card, relay1Status, checked);
        db.ref('/ESP32_AI_Hub/relay1').set(checked);
    });

    // Relay 2
    db.ref('/ESP32_AI_Hub/relay2').on('value', (snapshot) => {
        const val = snapshot.val() === true;
        relay2Toggle.checked = val;
        updateRelayCard(relay2Card, relay2Status, val);
    });

    relay2Toggle.addEventListener('change', (e) => {
        const checked = e.target.checked;
        updateRelayCard(relay2Card, relay2Status, checked);
        db.ref('/ESP32_AI_Hub/relay2').set(checked);
    });
}

function updateRelayCard(cardEl, statusEl, isActive) {
    if (!cardEl || !statusEl) return;
    if (isActive) {
        cardEl.classList.add('is-active');
        statusEl.innerText = 'Đang bật (ON)';
    } else {
        cardEl.classList.remove('is-active');
        statusEl.innerText = 'Đang tắt (OFF)';
    }
}

// Preset Chip Setters
function setLedPreset(val) {
    if (!ledSlider) return;
    ledSlider.value = val;
    ledVal.innerText = Math.round((val / 255) * 100) + '%';
    db.ref('/ESP32_AI_Hub/settings/ledBrightness').set(val);
}

function setVolPreset(val) {
    if (!volSlider) return;
    volSlider.value = val;
    volVal.innerText = Math.round((val / 21) * 100) + '%';
    db.ref('/ESP32_AI_Hub/settings/audioVolume').set(val);
}

function setupPresets() {
    const led0 = document.getElementById('led-p-0');
    const led25 = document.getElementById('led-p-25');
    const led50 = document.getElementById('led-p-50');
    const led100 = document.getElementById('led-p-100');

    if (led0) led0.onclick = () => setLedPreset(0);
    if (led25) led25.onclick = () => setLedPreset(64);
    if (led50) led50.onclick = () => setLedPreset(128);
    if (led100) led100.onclick = () => setLedPreset(255);

    const volMute = document.getElementById('vol-p-mute');
    const vol33 = document.getElementById('vol-p-33');
    const vol66 = document.getElementById('vol-p-66');
    const volMax = document.getElementById('vol-p-max');

    if (volMute) volMute.onclick = () => setVolPreset(0);
    if (vol33) vol33.onclick = () => setVolPreset(7);
    if (vol66) vol66.onclick = () => setVolPreset(14);
    if (volMax) volMax.onclick = () => setVolPreset(21);
}

// ==========================================================================
// 5. Daikin AC Climate Controller
// ==========================================================================
function setupDaikinAC() {
    // Power Toggle
    db.ref('/ESP32_AI_Hub/settings/acPower').on('value', (snapshot) => {
        const val = snapshot.val() === true;
        acPowerToggle.checked = val;
    });

    acPowerToggle.addEventListener('change', (e) => {
        db.ref('/ESP32_AI_Hub/settings/acPower').set(e.target.checked);
    });

    // Target Temperature (18-32)
    db.ref('/ESP32_AI_Hub/settings/acTemp').on('value', (snapshot) => {
        const val = snapshot.val();
        if (val !== null) {
            acTempSlider.value = val;
            acTempVal.innerText = val;
            updateAcTempPresetHighlight(val);
        }
    });

    acTempSlider.addEventListener('input', (e) => {
        const val = parseInt(e.target.value, 10);
        acTempVal.innerText = val;
        updateAcTempPresetHighlight(val);
        db.ref('/ESP32_AI_Hub/settings/acTemp').set(val);
    });

    if (acTempMinusBtn) {
        acTempMinusBtn.addEventListener('click', () => {
            let current = parseInt(acTempSlider.value, 10) || 25;
            if (current > 18) {
                current -= 1;
                acTempSlider.value = current;
                acTempVal.innerText = current;
                updateAcTempPresetHighlight(current);
                db.ref('/ESP32_AI_Hub/settings/acTemp').set(current);
            }
        });
    }

    if (acTempPlusBtn) {
        acTempPlusBtn.addEventListener('click', () => {
            let current = parseInt(acTempSlider.value, 10) || 25;
            if (current < 32) {
                current += 1;
                acTempSlider.value = current;
                acTempVal.innerText = current;
                updateAcTempPresetHighlight(current);
                db.ref('/ESP32_AI_Hub/settings/acTemp').set(current);
            }
        });
    }

    // AC Temp Presets
    document.querySelectorAll('.preset-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const targetTemp = parseInt(btn.dataset.temp, 10);
            if (targetTemp) {
                acTempSlider.value = targetTemp;
                acTempVal.innerText = targetTemp;
                updateAcTempPresetHighlight(targetTemp);
                db.ref('/ESP32_AI_Hub/settings/acTemp').set(targetTemp);
            }
        });
    });

    // Fan Speed (1-5, 10=Auto)
    db.ref('/ESP32_AI_Hub/settings/acFan').on('value', (snapshot) => {
        const val = snapshot.val();
        if (val !== null) {
            if (acFanSlider) acFanSlider.value = val;
            updateAcFanDisplay(val);
        }
    });

    document.querySelectorAll('.fan-chip').forEach(chip => {
        chip.addEventListener('click', () => {
            const fanVal = parseInt(chip.dataset.fan, 10);
            if (fanVal !== undefined) {
                if (acFanSlider) acFanSlider.value = fanVal;
                updateAcFanDisplay(fanVal);
                db.ref('/ESP32_AI_Hub/settings/acFan').set(fanVal);
            }
        });
    });
}

function updateAcTempPresetHighlight(currentTemp) {
    document.querySelectorAll('.preset-btn').forEach(btn => {
        if (parseInt(btn.dataset.temp, 10) === currentTemp) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });
}

function updateAcFanDisplay(val) {
    if (!acFanVal) return;
    if (val === 10) {
        acFanVal.innerText = 'Tự động (Auto)';
    } else {
        acFanVal.innerText = `Mức ${val} / 5`;
    }

    document.querySelectorAll('.fan-chip').forEach(chip => {
        if (parseInt(chip.dataset.fan, 10) === val) {
            chip.classList.add('active');
        } else {
            chip.classList.remove('active');
        }
    });
}

// ==========================================================================
// 6. Environmental Telemetry & Chart.js
// ==========================================================================
function setupSensorsAndWeather() {
    // 1. Weather Data
    db.ref('/ESP32_AI_Hub/outTemp').on('value', snap => {
        if (snap.val() !== null && webOutTemp) {
            const val = parseFloat(snap.val());
            webOutTemp.innerText = val.toFixed(1) + '°C';
            const badge = document.getElementById('badge-out-temp');
            if (badge) {
                if (val < 22) badge.innerText = 'Se mát';
                else if (val <= 30) badge.innerText = 'Dễ chịu';
                else badge.innerText = 'Nắng nóng';
            }
        }
    });

    db.ref('/ESP32_AI_Hub/outDesc').on('value', snap => {
        if (snap.val() !== null && webOutDesc) {
            webOutDesc.innerText = snap.val();
        }
    });

    db.ref('/ESP32_AI_Hub/aiAdvice').on('value', snap => {
        if (snap.val() !== null && webAiAdvice) {
            webAiAdvice.innerText = snap.val();
        }
    });

    // 2. Indoor Sensors
    db.ref('/ESP32_AI_Hub/indoorTemp').on('value', snap => {
        if (snap.val() !== null) {
            const val = parseFloat(snap.val());
            if (webIndoorTemp) webIndoorTemp.innerText = val.toFixed(1) + '°C';
            const badge = document.getElementById('badge-indoor-temp');
            if (badge) {
                if (val < 24) badge.innerText = 'Se lạnh';
                else if (val <= 28) badge.innerText = 'Lý tưởng';
                else if (val <= 32) badge.innerText = 'Hơi ấm';
                else badge.innerText = 'Nóng';
            }
            updateChartData(0, val);
        }
    });

    db.ref('/ESP32_AI_Hub/indoorHum').on('value', snap => {
        if (snap.val() !== null) {
            const val = parseFloat(snap.val());
            if (webIndoorHum) webIndoorHum.innerText = val.toFixed(0) + '%';
            const badge = document.getElementById('badge-indoor-hum');
            if (badge) {
                if (val < 45) badge.innerText = 'Hơi khô';
                else if (val <= 70) badge.innerText = 'Lý tưởng';
                else badge.innerText = 'Ẩm cao';
            }
            updateChartData(1, val);
        }
    });

    // 3. Pressure, Outdoor Humidity, Wind Speed (Robust multi-key fallback)
    const updatePressureUI = (val) => {
        if (webPressure) webPressure.innerText = parseFloat(val).toFixed(0) + ' hPa';
        const badge = document.getElementById('badge-pres');
        if (badge) {
            const p = parseFloat(val);
            if (p < 1000) badge.innerText = 'Áp thấp';
            else if (p <= 1015) badge.innerText = 'Ổn định';
            else badge.innerText = 'Áp cao';
        }
    };
    db.ref('/ESP32_AI_Hub/pressure').on('value', snap => {
        if (snap.val() !== null) updatePressureUI(snap.val());
    });
    db.ref('/ESP32_AI_Hub/sensors/pressure').on('value', snap => {
        if (snap.val() !== null) updatePressureUI(snap.val());
    });

    const updateOutHumUI = (val) => {
        if (webOutHum) webOutHum.innerText = parseFloat(val).toFixed(0) + '%';
        const badge = document.getElementById('badge-out-hum');
        if (badge) {
            const h = parseFloat(val);
            if (h < 50) badge.innerText = 'Khô ráo';
            else if (h <= 80) badge.innerText = 'Bình thường';
            else badge.innerText = 'Ẩm ướt';
        }
    };
    db.ref('/ESP32_AI_Hub/outHum').on('value', snap => {
        if (snap.val() !== null) updateOutHumUI(snap.val());
    });
    db.ref('/ESP32_AI_Hub/outdoor_hum').on('value', snap => {
        if (snap.val() !== null) updateOutHumUI(snap.val());
    });

    const updateOutWindUI = (val) => {
        if (webOutWind) webOutWind.innerText = parseFloat(val).toFixed(1) + ' m/s';
        const badge = document.getElementById('badge-wind');
        if (badge) {
            const w = parseFloat(val);
            if (w < 1.5) badge.innerText = 'Gió lặng';
            else if (w <= 3.3) badge.innerText = 'Gió nhẹ';
            else if (w <= 5.5) badge.innerText = 'Gió vừa';
            else badge.innerText = 'Gió mạnh';
        }
    };
    db.ref('/ESP32_AI_Hub/outWindSpd').on('value', snap => {
        if (snap.val() !== null) updateOutWindUI(snap.val());
    });
    db.ref('/ESP32_AI_Hub/wind_speed').on('value', snap => {
        if (snap.val() !== null) updateOutWindUI(snap.val());
    });

    // 4. RTC Time & Date from ESP32
    db.ref('/ESP32_AI_Hub/time').on('value', snap => {
        if (snap.val() && rtcTimeEl) {
            rtcTimeEl.innerText = snap.val();
        }
    });

    db.ref('/ESP32_AI_Hub/dateSolar').on('value', snap => {
        if (snap.val() && rtcDateSolarEl) {
            rtcDateSolarEl.innerText = snap.val();
        }
    });

    db.ref('/ESP32_AI_Hub/dateLunar').on('value', snap => {
        if (snap.val() && rtcDateLunarEl) {
            rtcDateLunarEl.innerText = snap.val();
        }
    });

    // 3. AI Last Answer Listener & Speech
    db.ref('/ESP32_AI_Hub/ai/last_answer').on('value', snap => {
        const val = snap.val();
        if (val) {
            if (transcriptBox) transcriptBox.innerText = val;
            speakResponse(val);
        }
    });

    // 4. Initialize Chart.js
    const canvas = document.getElementById('envChart');
    if (canvas) {
        const ctx = canvas.getContext('2d');
        
        // Gradient backgrounds
        const tempGrad = ctx.createLinearGradient(0, 0, 0, 170);
        tempGrad.addColorStop(0, 'rgba(244, 63, 94, 0.25)');
        tempGrad.addColorStop(1, 'rgba(244, 63, 94, 0.0)');

        const humGrad = ctx.createLinearGradient(0, 0, 0, 170);
        humGrad.addColorStop(0, 'rgba(56, 189, 248, 0.25)');
        humGrad.addColorStop(1, 'rgba(56, 189, 248, 0.0)');

        envChart = new Chart(ctx, {
            type: 'line',
            data: {
                labels: [],
                datasets: [
                    {
                        label: 'Nhiệt độ (°C)',
                        data: [],
                        borderColor: '#f43f5e',
                        backgroundColor: tempGrad,
                        borderWidth: 2,
                        pointRadius: 2,
                        pointHoverRadius: 5,
                        tension: 0.35,
                        fill: true,
                        yAxisID: 'y'
                    },
                    {
                        label: 'Độ ẩm (%)',
                        data: [],
                        borderColor: '#38bdf8',
                        backgroundColor: humGrad,
                        borderWidth: 2,
                        pointRadius: 2,
                        pointHoverRadius: 5,
                        tension: 0.35,
                        fill: true,
                        yAxisID: 'y1'
                    }
                ]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                interaction: {
                    mode: 'index',
                    intersect: false
                },
                scales: {
                    x: {
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                        ticks: { color: '#64748b', font: { family: 'JetBrains Mono', size: 10 } }
                    },
                    y: {
                        type: 'linear',
                        position: 'left',
                        min: 15,
                        max: 42,
                        grid: { color: 'rgba(255, 255, 255, 0.05)' },
                        ticks: { color: '#f43f5e', font: { family: 'JetBrains Mono', size: 10 }, callback: v => v + '°' }
                    },
                    y1: {
                        type: 'linear',
                        position: 'right',
                        min: 20,
                        max: 100,
                        grid: { drawOnChartArea: false },
                        ticks: { color: '#38bdf8', font: { family: 'JetBrains Mono', size: 10 }, callback: v => v + '%' }
                    }
                },
                plugins: {
                    legend: { display: false },
                    tooltip: {
                        backgroundColor: '#182234',
                        borderColor: 'rgba(255, 255, 255, 0.15)',
                        borderWidth: 1,
                        titleFont: { family: 'JetBrains Mono', size: 12 },
                        bodyFont: { family: 'Plus Jakarta Sans', size: 12 },
                        padding: 10,
                        cornerRadius: 8
                    }
                }
            }
        });
    }
}

function updateChartData(datasetIndex, value) {
    if (!envChart) return;
    const now = new Date();
    const timeLabel = now.getHours() + ':' + String(now.getMinutes()).padStart(2, '0');
    
    if (envChart.data.labels.length === 0 || envChart.data.labels[envChart.data.labels.length - 1] !== timeLabel) {
        envChart.data.labels.push(timeLabel);
        envChart.data.datasets[datasetIndex].data.push(value);
        
        const otherIndex = datasetIndex === 0 ? 1 : 0;
        const otherDataArr = envChart.data.datasets[otherIndex].data;
        if (otherDataArr.length > 0) {
            otherDataArr.push(otherDataArr[otherDataArr.length - 1]);
        } else {
            otherDataArr.push(null);
        }
    } else {
        envChart.data.datasets[datasetIndex].data[envChart.data.datasets[datasetIndex].data.length - 1] = value;
    }

    if (envChart.data.labels.length > 12) {
        envChart.data.labels.shift();
        envChart.data.datasets[0].data.shift();
        envChart.data.datasets[1].data.shift();
    }
    envChart.update();
}

function speakResponse(text) {
    if ('speechSynthesis' in window && window.speechSynthesis) {
        window.speechSynthesis.cancel();
        const utterance = new SpeechSynthesisUtterance(text);
        utterance.lang = 'vi-VN';
        
        if (ttsIndicator) {
            ttsIndicator.classList.add('speaking');
            ttsIndicator.innerHTML = '<i class="fa-solid fa-volume-high"></i> Đang đọc...';
        }

        utterance.onend = () => {
            if (ttsIndicator) {
                ttsIndicator.classList.remove('speaking');
                ttsIndicator.innerHTML = '<i class="fa-solid fa-volume-high"></i> Sẵn sàng';
            }
        };

        utterance.onerror = () => {
            if (ttsIndicator) {
                ttsIndicator.classList.remove('speaking');
                ttsIndicator.innerHTML = '<i class="fa-solid fa-volume-high"></i> Sẵn sàng';
            }
        };

        window.speechSynthesis.speak(utterance);
    }
}

// ==========================================================================
// 7. Voice Recognition & Command Center
// ==========================================================================
function setupSpeechRecognition() {
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    
    if (!SpeechRecognition) {
        if (transcriptBox) transcriptBox.innerText = "Trình duyệt chưa hỗ trợ Web Speech API. Bạn có thể gõ lệnh văn bản bên dưới.";
        if (micBtn) micBtn.style.opacity = '0.6';
        return;
    }

    recognition = new SpeechRecognition();
    recognition.lang = 'vi-VN';
    recognition.continuous = false;
    recognition.interimResults = true;

    recognition.onstart = () => {
        isRecording = true;
        if (micBtn) micBtn.classList.add('recording');
        if (micTriggerWrap) micTriggerWrap.classList.add('is-recording');
        if (transcriptBox) transcriptBox.innerText = "Đang lắng nghe giọng nói...";
    };

    recognition.onresult = (event) => {
        let finalTranscript = '';
        let interimTranscript = '';
        
        for (let i = event.resultIndex; i < event.results.length; ++i) {
            if (event.results[i].isFinal) {
                finalTranscript += event.results[i][0].transcript;
            } else {
                interimTranscript += event.results[i][0].transcript;
            }
        }
        
        if (finalTranscript !== '') {
            transcriptBox.innerText = `Bạn: "${finalTranscript}"`;
            sendVoiceCommandToFirebase(finalTranscript);
        } else if (interimTranscript !== '') {
            transcriptBox.innerText = `Đang nghe: "${interimTranscript}"`;
        }
    };

    recognition.onerror = (event) => {
        console.error("Speech Recognition Error:", event.error);
        if (event.error === 'not-allowed') {
            transcriptBox.innerText = "Vui lòng cấp quyền Microphone cho trình duyệt để nói.";
        } else {
            transcriptBox.innerText = "Lỗi nhận diện giọng nói: " + event.error;
        }
        stopRecording();
    };

    recognition.onend = () => {
        stopRecording();
    };

    if (micBtn) {
        // Desktop click / Hold
        micBtn.addEventListener('click', toggleRecording);
        
        // Touch support
        micBtn.addEventListener('touchstart', (e) => {
            e.preventDefault();
            startRecording();
        });
        micBtn.addEventListener('touchend', (e) => {
            e.preventDefault();
            stopRecording();
        });
    }
}

function toggleRecording() {
    if (isRecording) {
        stopRecording();
    } else {
        startRecording();
    }
}

function startRecording() {
    if (!recognition || isRecording) return;
    try {
        recognition.start();
    } catch(e) {
        console.warn("Recognition start error:", e);
    }
}

function stopRecording() {
    if (!recognition || !isRecording) return;
    isRecording = false;
    if (micBtn) micBtn.classList.remove('recording');
    if (micTriggerWrap) micTriggerWrap.classList.remove('is-recording');
    try {
        recognition.stop();
    } catch(e) {}
}

function sendVoiceCommandToFirebase(text) {
    if (!text || text.trim() === '') return;
    const cleanText = text.trim();
    db.ref('/ESP32_AI_Hub/ai/webCommand/text').set(cleanText).then(() => {
        console.log("Đã gửi lệnh RTDB:", cleanText);
    }).catch(err => {
        console.error("Lỗi gửi lệnh:", err);
    });
}

// Setup Text Command Input
function setupCommandForm() {
    if (!commandForm || !commandInput) return;
    
    commandForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const text = commandInput.value.trim();
        if (text) {
            if (transcriptBox) transcriptBox.innerText = `Bạn: "${text}"`;
            sendVoiceCommandToFirebase(text);
            commandInput.value = '';
        }
    });
}

// Setup Quick Chips
function setupQuickChips() {
    document.querySelectorAll('.quick-chip').forEach(chip => {
        chip.addEventListener('click', () => {
            const cmd = chip.dataset.cmd;
            if (cmd) {
                if (transcriptBox) transcriptBox.innerText = `Bạn: "${cmd}"`;
                sendVoiceCommandToFirebase(cmd);
            }
        });
    });
}

// Run App on Load
window.addEventListener('DOMContentLoaded', initApp);
