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

constexpr size_t maxQueueBytes = 32 * 1024 * 1024;

bool issueRead(HANDLE pipe, std::vector<BYTE>& buf, OVERLAPPED& ov)
{
    ResetEvent(ov.hEvent);
    DWORD bytesRead = 0;
    BOOL ok = ReadFile(pipe, buf.data(), static_cast<DWORD>(buf.size()),
                       &bytesRead, &ov);
    if (ok)
        return true;
    return GetLastError() == ERROR_IO_PENDING;
}

bool issueWrite(HANDLE pipe, std::vector<BYTE> const& buf, OVERLAPPED& ov)
{
    ResetEvent(ov.hEvent);
    DWORD bytesWritten = 0;
    BOOL ok = WriteFile(pipe, buf.data(), static_cast<DWORD>(buf.size()),
                        &bytesWritten, &ov);
    if (ok)
        return true;
    return GetLastError() == ERROR_IO_PENDING;
}

class KqTunnelChannelCallback : public IWTSVirtualChannelCallback
{
public:
    explicit KqTunnelChannelCallback(IWTSVirtualChannel* channel)
        : refCount_(1), channel_(channel)
    {
        InterlockedIncrement(&g_dllRefCount);
        channel_->AddRef();
        shutdownEvent_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        queueEvent_ = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        // Start background thread that connects to the pipe and handles all
        // pipe I/O. This is the sole owner of the pipe handle.
        channel_->AddRef();
        ioThread_ = std::thread(&KqTunnelChannelCallback::ioThreadFunc, this);
    }

    ~KqTunnelChannelCallback()
    {
        shutdown();
        if (channel_)
            channel_->Release();
        CloseHandle(queueEvent_);
        CloseHandle(shutdownEvent_);
        InterlockedDecrement(&g_dllRefCount);
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
        try {
            std::lock_guard lock(bufMtx_);
            if (queue_.size() + size > maxQueueBytes) {
                SetEvent(shutdownEvent_);
                return S_OK;
            }
            queue_.insert(queue_.end(), data, data + size);
        } catch (...) {
            SetEvent(shutdownEvent_);
            return S_OK;
        }
        SetEvent(queueEvent_);
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
        if (ioThread_.joinable())
            ioThread_.join();
    }

    void ioThreadFunc()
    {
        // Phase 1: Connect to the named pipe.
        // Pipe handle is local — only this thread ever touches it.
        HANDLE pipe = INVALID_HANDLE_VALUE;
        for (;;) {
            pipe = CreateFileA(
                kq::pipeName,
                GENERIC_READ | GENERIC_WRITE,
                0,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_OVERLAPPED,
                nullptr);

            if (pipe != INVALID_HANDLE_VALUE)
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
        SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

        // Phase 2: Set up overlapped I/O and kick off first read.
        // Any data queued by OnDataReceived during connection will be picked
        // up by the main loop via the already-signaled queueEvent_.
        OVERLAPPED readOv{};
        readOv.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);
        OVERLAPPED writeOv{};
        writeOv.hEvent = CreateEventA(nullptr, TRUE, FALSE, nullptr);

        std::vector<BYTE> readBuf(kq::bufferSize);
        std::vector<BYTE> writeBuf;
        bool readPending = issueRead(pipe, readBuf, readOv);
        bool writePending = false;

        // Phase 3: Multiplexed I/O loop.
        // Wait on a compact array built each iteration from the active events.
        enum WaitId { SHUTDOWN, PIPE_READ, QUEUE_READY, PIPE_WRITE };

        while (readPending || writePending) {
            HANDLE handles[4];
            WaitId ids[4];
            DWORD count = 0;

            auto addWait = [&](WaitId id, HANDLE h) {
                ids[count] = id;
                handles[count] = h;
                ++count;
            };

            addWait(SHUTDOWN, shutdownEvent_);
            if (writePending)
                addWait(PIPE_WRITE, writeOv.hEvent);
            else
                addWait(QUEUE_READY, queueEvent_);
            if (readPending)
                addWait(PIPE_READ, readOv.hEvent);

            DWORD result = WaitForMultipleObjects(count, handles, FALSE, INFINITE);
            if (result == WAIT_FAILED)
                break;

            DWORD index = result - WAIT_OBJECT_0;
            if (index >= count)
                break;

            WaitId signaled = ids[index];

            if (signaled == SHUTDOWN)
                break;

            if (signaled == PIPE_READ) {
                DWORD bytesRead = 0;
                if (!GetOverlappedResult(pipe, &readOv, &bytesRead, FALSE))
                    break;
                if (bytesRead > 0)
                    channel_->Write(bytesRead, readBuf.data(), nullptr);
                readPending = issueRead(pipe, readBuf, readOv);
                if (!readPending)
                    break;
            }

            if (signaled == QUEUE_READY) {
                {
                    std::lock_guard lock(bufMtx_);
                    writeBuf.swap(queue_);
                    ResetEvent(queueEvent_);
                }
                if (!writeBuf.empty()) {
                    writePending = issueWrite(pipe, writeBuf, writeOv);
                    if (!writePending)
                        break;
                }
            }

            if (signaled == PIPE_WRITE) {
                DWORD bytesWritten = 0;
                if (!GetOverlappedResult(pipe, &writeOv, &bytesWritten, FALSE))
                    break;
                writePending = false;
                writeBuf.clear();
            }
        }

        // Phase 4: Cancel any in-flight I/O before closing handles.
        if (readPending) {
            CancelIoEx(pipe, &readOv);
            DWORD dummy;
            GetOverlappedResult(pipe, &readOv, &dummy, TRUE);
        }
        if (writePending) {
            CancelIoEx(pipe, &writeOv);
            DWORD dummy;
            GetOverlappedResult(pipe, &writeOv, &dummy, TRUE);
        }

        CloseHandle(readOv.hEvent);
        CloseHandle(writeOv.hEvent);
        CloseHandle(pipe);
        channel_->Release();
    }

    LONG refCount_;
    IWTSVirtualChannel* channel_;
    HANDLE shutdownEvent_;
    HANDLE queueEvent_;
    std::thread ioThread_;
    std::mutex bufMtx_;
    std::vector<BYTE> queue_;
};

class KqTunnelListenerCallback : public IWTSListenerCallback
{
public:
    KqTunnelListenerCallback() : refCount_(1)
    {
        InterlockedIncrement(&g_dllRefCount);
    }

    ~KqTunnelListenerCallback()
    {
        InterlockedDecrement(&g_dllRefCount);
    }

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
