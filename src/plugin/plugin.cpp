#include <windows.h>
#include <initguid.h>
#include <tsvirtualchannels.h>

#include <mutex>
#include <thread>
#include <vector>

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
        shutdownEvent_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        // Start background thread that connects to the pipe and reads from it.
        // This runs immediately so the pipe connection is attempted before any
        // DVC data arrives, eliminating startup-order sensitivity.
        channel_->AddRef();
        ioThread_ = std::thread(&KqTunnelChannelCallback::connectAndRead, this);
    }

    ~KqTunnelChannelCallback()
    {
        shutdown();
        if (channel_)
            channel_->Release();
        CloseHandle(shutdownEvent_);
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
        // Fast path: pipe already connected, write directly
        HANDLE p = pipe_;
        if (p != INVALID_HANDLE_VALUE) {
            writePipe(p, data, size);
            return S_OK;
        }

        // Slow path: pipe not connected yet, buffer data for the IO thread to flush
        std::lock_guard lock(bufMtx_);
        if (pipe_ != INVALID_HANDLE_VALUE) {
            writePipe(pipe_, data, size);
        } else {
            pendingBuf_.insert(pendingBuf_.end(), data, data + size);
        }
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnClose() override
    {
        shutdown();
        if (channel_) {
            channel_->Release();
            channel_ = nullptr;
        }
        return S_OK;
    }

private:
    void shutdown()
    {
        SetEvent(shutdownEvent_);
        closePipe();
        if (ioThread_.joinable())
            ioThread_.join();
    }

    void closePipe()
    {
        HANDLE h = InterlockedExchangePointer(&pipe_, INVALID_HANDLE_VALUE);
        if (h != INVALID_HANDLE_VALUE) {
            CancelIoEx(h, nullptr);
            CloseHandle(h);
        }
    }

    void writePipe(HANDLE h, BYTE const* data, ULONG size)
    {
        DWORD written = 0;
        OVERLAPPED ov{};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        BOOL ok = WriteFile(h, data, size, &written, &ov);

        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING)
                GetOverlappedResult(h, &ov, &written, TRUE);
        }
        CloseHandle(ov.hEvent);
    }

    void connectAndRead()
    {
        HANDLE h = INVALID_HANDLE_VALUE;
        for (;;) {
            h = CreateFileA(
                kq::pipeName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr);

            if (h != INVALID_HANDLE_VALUE)
                break;

            DWORD err = GetLastError();
            if (err != ERROR_FILE_NOT_FOUND && err != ERROR_PIPE_BUSY) {
                channel_->Release();
                return;
            }

            if (WaitForSingleObject(shutdownEvent_, 500) == WAIT_OBJECT_0) {
                channel_->Release();
                return;
            }
        }

        DWORD mode = PIPE_READMODE_BYTE;
        SetNamedPipeHandleState(h, &mode, nullptr, nullptr);

        // Flush any data that OnDataReceived buffered while we were connecting,
        // then publish the pipe handle so OnDataReceived writes directly.
        {
            std::lock_guard lock(bufMtx_);
            if (!pendingBuf_.empty()) {
                writePipe(h, pendingBuf_.data(),
                    static_cast<ULONG>(pendingBuf_.size()));
                pendingBuf_.clear();
                pendingBuf_.shrink_to_fit();
            }
            pipe_ = h;
        }

        // Pipe reader loop: pipe -> DVC channel
        std::vector<BYTE> buf(kq::bufferSize);
        OVERLAPPED ov{};
        ov.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        while (pipe_ != INVALID_HANDLE_VALUE) {
            DWORD bytesRead = 0;
            ResetEvent(ov.hEvent);
            BOOL ok = ReadFile(pipe_, buf.data(), static_cast<DWORD>(buf.size()),
                    &bytesRead, &ov);

            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    if (!GetOverlappedResult(pipe_, &ov, &bytesRead, TRUE))
                        break;
                } else {
                    break;
                }
            }

            if (bytesRead == 0)
                continue;

            if (channel_)
                channel_->Write(bytesRead, buf.data(), nullptr);
        }

        CloseHandle(ov.hEvent);
        channel_->Release();
    }

    LONG refCount_;
    IWTSVirtualChannel* channel_;
    HANDLE pipe_ = INVALID_HANDLE_VALUE;
    HANDLE shutdownEvent_;
    std::thread ioThread_;
    std::mutex bufMtx_;
    std::vector<BYTE> pendingBuf_;
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
