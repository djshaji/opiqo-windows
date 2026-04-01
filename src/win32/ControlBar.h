#pragma once

#include <windows.h>

class ControlBar {
public:
    bool create(HWND parent, const RECT& bounds);
    HWND hwnd() const;

    // Programmatically set the checked state of the power button without
    // triggering a WM_COMMAND notification to the parent.
    void setPowerState(bool on);

    // Programmatically set the checked state of the record button.
    void setRecordState(bool on);

    // Returns the gain slider position in [0, 100].
    int gainValue() const;

    // Returns the currently selected format index (0=WAV, 1=MP3, 2=OGG).
    int formatIndex() const;

    // Selects the format combo entry matching the given index.
    void setFormatIndex(int index);

    // Repositions and resizes all child controls to fit new bounds.
    void resize(const RECT& bounds);

private:
    HWND hwnd_         = nullptr;
    HWND powerButton_  = nullptr;
    HWND gainSlider_   = nullptr;
    HWND recordButton_ = nullptr;
    HWND formatCombo_  = nullptr;
};
