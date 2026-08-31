#ifndef AI_AGENT_H
#define AI_AGENT_H

#include <Arduino.h>

extern String pendingSongUrl;
extern String pendingSongTitle;
extern String foundSongDisplay;
extern bool isAiBusy;
extern bool isWaitingFollowupCommand;
extern String pendingTtsUrl;
extern String pendingTtsSpeech;
extern bool hasPendingTts;

void setupAiTask();
void triggerAiAudioProcess(uint8_t* audioData, size_t size);
void triggerAiTextProcess(String text);
void triggerMusicSearchTask(String songTitle);

void processAudioAI(uint8_t* audioData, size_t size);
void createWavHeader(uint8_t* header, int waveDataSize);
void sendToLLM(String userText, bool isSilent = false);
void playTTS(String text, bool isPromptOnly = false, String customTtsUrl = "");
String searchMusicUrl(String songTitle);

#endif
