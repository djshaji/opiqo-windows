#include "PluginDialog.h"

bool PluginDialog::showModal(HWND parent, std::string* selectedUri) {
    MessageBoxA(parent, "Plugin picker will be implemented in Milestone 6.", "Plugin Dialog", MB_OK);
    if (selectedUri != nullptr) {
        selectedUri->clear();
    }
    return false;
}
