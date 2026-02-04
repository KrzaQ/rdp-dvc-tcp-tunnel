#include <windows.h>
#include <initguid.h>
#include <tsvirtualchannels.h>

#include "protocol.hpp"

// {8B6D78AA-856B-4D4F-A2A2-0C0CCC4B4E18}
// clang-format off
DEFINE_GUID(CLSID_KqTunnelPlugin,
    0x8b6d78aa, 0x856b, 0x4d4f,
    0xa2, 0xa2, 0x0c, 0x0c, 0xcc, 0x4b, 0x4e, 0x18);
// clang-format on

namespace {

LONG g_dllRefCount = 0;

class KqTunnelChannelCallback : public IWTSVirtualChannelCallback
{
public:
    explicit KqTunnelChannelCallback(IWTSVirtualChannel* channel)
        : refCount_(1), channel_(channel)
    {
        channel_->AddRef();
    }

    ~KqTunnelChannelCallback()
    {
        if (channel_)
            channel_->Release();
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == __uuidof(IWTSVirtualChannelCallback)) {
            *ppv = static_cast<IWTSVirtualChannelCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    // IWTSVirtualChannelCallback
    HRESULT STDMETHODCALLTYPE OnDataReceived(ULONG size, BYTE* data) override
    {
        // TODO: forward data from DVC to named pipe
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnClose() override
    {
        // TODO: signal pipe disconnection
        channel_->Release();
        channel_ = nullptr;
        return S_OK;
    }

private:
    LONG refCount_;
    IWTSVirtualChannel* channel_;
};

class KqTunnelListenerCallback : public IWTSListenerCallback
{
public:
    KqTunnelListenerCallback() : refCount_(1) {}

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == __uuidof(IWTSListenerCallback)) {
            *ppv = static_cast<IWTSListenerCallback*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    // IWTSListenerCallback
    HRESULT STDMETHODCALLTYPE OnNewChannelConnection(
        IWTSVirtualChannel* channel,
        BSTR data,
        BOOL* accept,
        IWTSVirtualChannelCallback** callback) override
    {
        *accept = TRUE;
        auto* cb = new KqTunnelChannelCallback(channel);
        *callback = cb;
        return S_OK;
    }

private:
    LONG refCount_;
};

class KqTunnelPlugin : public IWTSPlugin
{
public:
    KqTunnelPlugin() : refCount_(1)
    {
        InterlockedIncrement(&g_dllRefCount);
    }

    ~KqTunnelPlugin()
    {
        InterlockedDecrement(&g_dllRefCount);
    }

    // IUnknown
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == __uuidof(IWTSPlugin)) {
            *ppv = static_cast<IWTSPlugin*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    // IWTSPlugin
    HRESULT STDMETHODCALLTYPE Initialize(IWTSVirtualChannelManager* channelMgr) override
    {
        auto* listener = new KqTunnelListenerCallback();
        HRESULT hr = channelMgr->CreateListener(
            kq::channelName, 0, listener, nullptr);
        listener->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE Connected() override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Disconnected(DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE Terminated() override { return S_OK; }

private:
    LONG refCount_;
};

class KqTunnelClassFactory : public IClassFactory
{
public:
    KqTunnelClassFactory() : refCount_(1) { InterlockedIncrement(&g_dllRefCount); }
    ~KqTunnelClassFactory() { InterlockedDecrement(&g_dllRefCount); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_INVALIDARG;
        if (riid == IID_IUnknown || riid == IID_IClassFactory) {
            *ppv = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount_); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        auto count = InterlockedDecrement(&refCount_);
        if (count == 0) delete this;
        return count;
    }

    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override
    {
        if (outer) return CLASS_E_NOAGGREGATION;
        auto* plugin = new KqTunnelPlugin();
        HRESULT hr = plugin->QueryInterface(riid, ppv);
        plugin->Release();
        return hr;
    }

    HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override
    {
        if (lock) InterlockedIncrement(&g_dllRefCount);
        else InterlockedDecrement(&g_dllRefCount);
        return S_OK;
    }

private:
    LONG refCount_;
};

} // namespace

extern "C" {

HRESULT STDAPICALLTYPE DllGetClassObject(REFCLSID clsid, REFIID riid, void** ppv)
{
    if (clsid != CLSID_KqTunnelPlugin) return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new KqTunnelClassFactory();
    HRESULT hr = factory->QueryInterface(riid, ppv);
    factory->Release();
    return hr;
}

HRESULT STDAPICALLTYPE DllCanUnloadNow()
{
    return g_dllRefCount == 0 ? S_OK : S_FALSE;
}

HRESULT STDAPICALLTYPE DllRegisterServer()
{
    // TODO: write registry entries for CLSID and Terminal Server Client AddIns
    return S_OK;
}

HRESULT STDAPICALLTYPE DllUnregisterServer()
{
    // TODO: remove registry entries
    return S_OK;
}

} // extern "C"
