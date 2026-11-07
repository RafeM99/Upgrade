// Voice.h (akshat): character voiceover, made once with ElevenLabs by tools/voice/generate_vo.py.
// Every spoken line has its own file assets/vo/<key>.mp3, key = FNV-1a 64 of "speaker|text"
// (the generator uses the same function). A line with no file simply plays nothing, so the game
// works with or without voiceover. Speakers: gran, sam, narrator, school_bell, last_train, forgetting.
#pragma once
#include <string>

namespace vo {
std::string key(const std::string& speaker, const std::string& text);
void play(const std::string& speaker, const std::string& text);   // stops whatever was speaking
void stop();
bool playing();
void setVolume(float v);   // 0..1, on top of the master volume
void shutdown();           // unload every cached clip (before CloseAudioDevice)
}  // namespace vo
