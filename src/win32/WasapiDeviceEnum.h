#pragma once

#include <functional>
#include <string>
#include <vector>

struct DeviceInfo {
    std::string id;
    std::string friendlyName;
    bool isDefault = false;
};

class WasapiDeviceEnum {
public:
    // Must be constructed after CoInitializeEx on the calling thread.
    WasapiDeviceEnum();
    ~WasapiDeviceEnum();

    WasapiDeviceEnum(const WasapiDeviceEnum&) = delete;
    WasapiDeviceEnum& operator=(const WasapiDeviceEnum&) = delete;

    std::vector<DeviceInfo> enumerateInputDevices()  const;
    std::vector<DeviceInfo> enumerateOutputDevices() const;

    // Register a callback invoked whenever device availability changes.
    // NOTE: the callback fires on an internal COM notification thread;
    // use PostMessage to marshal the event back to the UI thread.
    void setChangeCallback(std::function<void()> callback);

    // Returns savedId if found in list, the default device id if not,
    // the first device id as a last resort, or an empty string if list is empty.
    static std::string resolveOrDefault(const std::vector<DeviceInfo>& list,
                                        const std::string& savedId);

    // Queries the WASAPI mix format for the given device id and returns its
    // native sample rate. If deviceId is empty, queries the default render
    // endpoint. Returns 0 on failure.
    int getNativeSampleRate(const std::string& deviceId) const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};
