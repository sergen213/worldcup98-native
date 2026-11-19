#pragma once
// WC98 scene sound-level layer: the percent table DAT_00632778[6] + Q7 gain producers
// (FUN_004906a4 / FUN_004906d0), setter+notifier dispatch FUN_0049067c over the notifier
// table DAT_004904b0{0, FUN_0048d7a0, FUN_0048e430, FUN_0048fca0, FUN_0048ec38, FUN_0049091c},
// and init FUN_00490728 (one-time flag DAT_00632774; table[0..5] = 100).
// table Dossier: analysis/pass17s6audio/{NOTES.md,DECOMPILE/}. Scene defaults measured live
// (winedbg pass-7e): {80,80,80,80,65,80} — idx4 (music lane, read in FUN_0048ed9c@0x48ee63) = 65.
#include <array>
#include <cstdint>
#include <functional>

namespace wc98::audio {

// Channel indices into the percent table (DAT_00632778 stride 4) and notifier table.
// Role proof (pass-49): the live capture GET idx=4 inside the FE-music list builder
// FUN_0048ed9c ret=0x48ee63 (analysis/pass7/percent1.txt) + gainQ7 callers FUN_0048eb98/FUN_0048ecbc
// (analysis/pass7/gain5.txt) pin idx4 = music stream lane (SNDsetvol FUN_004e6aa0, gate DAT_00632644);
// FUN_0048fca0 applies FUN_004df8a4 = SNDspeechautovol -> idx3 = speech/commentary stream;
// FUN_0048e550/FUN_0048e430 gate the crowd stream slots DAT_006325b0/5b8 -> idx2.
enum SceneChannel : int {
    kSceneMaster = 0,        // notifier slot [0] = 0 (no subsystem callback); SFX + speech-queue vol
    kSceneCommentary = 1,    // FUN_0048d7a0: gate DAT_006324b4, gain via FUN_004906d0 + FUN_0048d304
    kSceneCrowd = 2,         // FUN_0048e430: gates DAT_006325b0/5b8, stop flag 5b4, relevel FUN_0048e100
    kSceneSpeech = 3,        // FUN_0048fca0: gate DAT_006326b0, apply FUN_004df8a4 SNDspeechautovol
    kSceneMusic = 4,         // FUN_0048ec38: gate DAT_00632644, apply FUN_004e6aa0; consumer FUN_0048ed9c@0x48ee63
    kSceneCd = 5,            // FUN_0049091c: gates DAT_00632790 + positions 7a8/7ac
    kSceneChannels = 6,
};

class SceneLevels {
public:
    using Notifier = void (*)(int channel);

    SceneLevels() { reset(); } // FUN_00490728 body: table[0..5] = 100

    // DAT_004904b0 slot install (exe writes the table at subsystem-build time).
    void setNotifier(int channel, Notifier n) { notifier_[channel] = n; }
    [[nodiscard]] bool hasNotifier(int channel) const { return notifier_[channel] != nullptr; }

    // FUN_0049067c: store percent, then notify if the channel's notifier is installed.
    void setLevel(int channel, int value);

    [[nodiscard]] int level(int channel) const { return static_cast<int>(table_[channel]); }

    // FUN_004906a4: q7 = (percent * 0x7f) / 100; clamped up to 1.
    [[nodiscard]] int gainQ7(int channel) const;

    // FUN_004906d0: signed scaled multiply (value * percent * 0x7f) / 100.
    [[nodiscard]] std::int32_t scaled(int channel, std::int32_t value) const;

private:
    void reset(); // init grammar: all six percents = 100

    std::uint32_t table_[kSceneChannels] = {};
    std::array<Notifier, kSceneChannels> notifier_{};
};

// The exe layer is a global table (DAT_00632778) with global notifiers (DAT_004904b0).
SceneLevels& sceneLevels();

} // namespace wc98::audio
