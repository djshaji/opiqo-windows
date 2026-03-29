#pragma once

#include <string>
#include <vector>

struct DeviceInfo {
    std::string id;
    std::string friendlyName;
    bool isDefault = false;
};

class WasapiDeviceEnum {
public:
    std::vector<DeviceInfo> enumerateInputDevices() const;
    std::vector<DeviceInfo> enumerateOutputDevices() const;
};
