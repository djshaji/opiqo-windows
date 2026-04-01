// Define GUIDs in this translation unit so mmdevapi/propsys symbols resolve
// without requiring an explicit -linitguid.
#define INITGUID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propsys.h>

#include "WasapiDeviceEnum.h"

#include <atomic>

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static std::string wstrToUtf8(const WCHAR* wstr) {
    if (!wstr) return {};
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return {};
    std::string s(sz - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, &s[0], sz, nullptr, nullptr);
    return s;
}

static std::vector<DeviceInfo> enumerateFlow(IMMDeviceEnumerator* enumerator,
                                             EDataFlow flow) {
    std::vector<DeviceInfo> result;
    if (!enumerator) return result;

    // Resolve default device id for this flow.
    std::string defaultId;
    {
        IMMDevice* def = nullptr;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &def))) {
            WCHAR* wid = nullptr;
            if (SUCCEEDED(def->GetId(&wid))) {
                defaultId = wstrToUtf8(wid);
                CoTaskMemFree(wid);
            }
            def->Release();
        }
    }

    IMMDeviceCollection* col = nullptr;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &col)))
        return result;

    UINT count = 0;
    col->GetCount(&count);
    result.reserve(count);

    for (UINT i = 0; i < count; ++i) {
        IMMDevice* dev = nullptr;
        if (FAILED(col->Item(i, &dev))) continue;

        DeviceInfo info;

        WCHAR* wid = nullptr;
        if (SUCCEEDED(dev->GetId(&wid))) {
            info.id = wstrToUtf8(wid);
            CoTaskMemFree(wid);
        }

        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps))) {
            PROPVARIANT pv;
            PropVariantInit(&pv);
            if (SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv))
                && pv.vt == VT_LPWSTR) {
                info.friendlyName = wstrToUtf8(pv.pwszVal);
            }
            PropVariantClear(&pv);
            ps->Release();
        }

        if (info.friendlyName.empty()) info.friendlyName = info.id;
        info.isDefault = (!info.id.empty() && info.id == defaultId);

        result.push_back(std::move(info));
        dev->Release();
    }

    col->Release();
    return result;
}

// ---------------------------------------------------------------------------
// Notification client (COM object, ref-counted)
// ---------------------------------------------------------------------------

class NotificationClientImpl final : public IMMNotificationClient {
public:
    explicit NotificationClientImpl(std::function<void()> cb)
        : cb_(std::move(cb)) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown
            || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refCount_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG c = --refCount_;
        if (c == 0) delete this;
        return c;
    }

    // IMMNotificationClient — notify on any topology change
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        if (cb_) cb_(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        if (cb_) cb_(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        if (cb_) cb_(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole,
                                                     LPCWSTR) override {
        if (cb_) cb_(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR,
                                                     const PROPERTYKEY) override {
        return S_OK;
    }

private:
    std::function<void()> cb_;
    std::atomic<ULONG> refCount_{1};
};

// ---------------------------------------------------------------------------
// WasapiDeviceEnum pimpl
// ---------------------------------------------------------------------------

struct WasapiDeviceEnum::Impl {
    IMMDeviceEnumerator*   enumerator  = nullptr;
    NotificationClientImpl* notifClient = nullptr;
};

WasapiDeviceEnum::WasapiDeviceEnum() : impl_(new Impl()) {
    HRESULT hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
        IID_IMMDeviceEnumerator,
        reinterpret_cast<void**>(&impl_->enumerator));
    if (FAILED(hr)) impl_->enumerator = nullptr;
}

WasapiDeviceEnum::~WasapiDeviceEnum() {
    if (impl_) {
        if (impl_->enumerator && impl_->notifClient) {
            impl_->enumerator->UnregisterEndpointNotificationCallback(
                impl_->notifClient);
            impl_->notifClient->Release();
        }
        if (impl_->enumerator) impl_->enumerator->Release();
        delete impl_;
    }
}

std::vector<DeviceInfo> WasapiDeviceEnum::enumerateInputDevices() const {
    return enumerateFlow(impl_->enumerator, eCapture);
}

std::vector<DeviceInfo> WasapiDeviceEnum::enumerateOutputDevices() const {
    return enumerateFlow(impl_->enumerator, eRender);
}

void WasapiDeviceEnum::setChangeCallback(std::function<void()> callback) {
    if (!impl_->enumerator) return;

    if (impl_->notifClient) {
        impl_->enumerator->UnregisterEndpointNotificationCallback(
            impl_->notifClient);
        impl_->notifClient->Release();
        impl_->notifClient = nullptr;
    }

    if (callback) {
        impl_->notifClient = new NotificationClientImpl(std::move(callback));
        impl_->enumerator->RegisterEndpointNotificationCallback(
            impl_->notifClient);
    }
}

std::string WasapiDeviceEnum::resolveOrDefault(const std::vector<DeviceInfo>& list,
                                               const std::string& savedId) {
    for (const auto& d : list)
        if (d.id == savedId) return d.id;

    for (const auto& d : list)
        if (d.isDefault) return d.id;

    if (!list.empty()) return list[0].id;
    return {};
}

