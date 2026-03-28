#include "LiveEffectEngine.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    LiveEffectEngine engine;
    engine.initLV2();
    return 0;
}

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main();
}
#endif