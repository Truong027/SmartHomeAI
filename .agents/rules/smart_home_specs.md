# SMART HOME ESP32 - CODING RULES & SPECIFICATIONS

## Core Architecture Rules
1. **WS2812B LED Count**: Strictly `NUM_LEDS 12`. Any animation rendering must assume 12 physical LEDs.
2. **Vietnamese Lunar Math**: Always apply the 0.98 midnight boundary calibration for GMT+7.
3. **Search Knowledge Grounding**: Biological & scientific questions must be grounded in official Wikipedia REST summary endpoints to prevent news headline hallucinations.
4. **Music Playback Lifecycle**:
   - `pendingSongTitle` queuing pattern.
   - High speed CDN MP3 direct URLs via `searchMusicUrl`.
   - Never abort active playback from TTS watchdog timers.
5. **AI Long-term Memory**:
   - Preferences storage namespace `"ai_mem"` for userName, aiName, userFacts, customNotes.
   - Synchronized with Firebase `/smart_home/ai_memory`.
