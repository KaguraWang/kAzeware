#pragma once

namespace Hitsound {
    // Bundled Source 2 sounds use embedded decoded PCM; no external sound folder.
    void Install();
    void Shutdown();
    int Count();
    const char* NameAt(int index);        // Stable configuration ID: "bell.vsnd".
    const char* DisplayNameAt(int index); // Menu label: "bell".
    int IndexOf(const char* fileName);    // Accepts bundled .vsnd/.vsnd_c and WAV names.
    void Play(bool head, bool kill);
    void PreviewSelected();
} // namespace Hitsound
