#include "VideoWallpaperItem.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMetaObject>
#include <QQuickWindow>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QtQuick/private/qsgplaintexture_p.h>
#include <rhi/qrhi.h>
#include <rhi/qrhi_platform.h>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include <windows.h>
#include <shlwapi.h>
#include <d3d11.h>
#include <d3d10.h>
#include <dxgi1_6.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propidl.h>

namespace SekaiPop::Services {

namespace {

struct ComMfScope {
    bool comInit = false;
    bool mfInit = false;

    ComMfScope() {
        const HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        comInit = SUCCEEDED(hrCom);
        const HRESULT hrMf = MFStartup(MF_VERSION, MFSTARTUP_LITE);
        mfInit = SUCCEEDED(hrMf);
    }

    ~ComMfScope() {
        if (mfInit) {
            MFShutdown();
        }
        if (comInit) {
            CoUninitialize();
        }
    }
};

struct VideoFormatInfo {
    int frameW = 0;
    int frameH = 0;
    int cropX = 0;
    int cropY = 0;
    int cropW = 0;
    int cropH = 0;
    LONG defaultStride = 0;
    LONGLONG frameDuration100ns = 333333; // ~30 fps default
    bool isNv12 = false;
};

bool queryVideoFormat(IMFSourceReader* reader, VideoFormatInfo& out) {
    IMFMediaType* mediaType = nullptr;
    if (FAILED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &mediaType)) || !mediaType) {
        return false;
    }

    GUID subtype = GUID_NULL;
    if (SUCCEEDED(mediaType->GetGUID(MF_MT_SUBTYPE, &subtype))) {
        out.isNv12 = (subtype == MFVideoFormat_NV12);
    }

    UINT32 w = 0, h = 0;
    if (FAILED(MFGetAttributeSize(mediaType, MF_MT_FRAME_SIZE, &w, &h)) || w == 0 || h == 0) {
        mediaType->Release();
        return false;
    }

    out.frameW = static_cast<int>(w);
    out.frameH = static_cast<int>(h);
    out.cropX = 0;
    out.cropY = 0;
    out.cropW = out.frameW;
    out.cropH = out.frameH;

    MFVideoArea aperture = {};
    UINT32 blobSize = 0;
    if (SUCCEEDED(mediaType->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE,
                                     reinterpret_cast<UINT8*>(&aperture),
                                     sizeof(aperture), &blobSize)) &&
        blobSize == sizeof(aperture)) {
        const int ax = static_cast<int>(aperture.OffsetX.value);
        const int ay = static_cast<int>(aperture.OffsetY.value);
        const int aw = static_cast<int>(aperture.Area.cx);
        const int ah = static_cast<int>(aperture.Area.cy);
        if (ax >= 0 && ay >= 0 && aw > 0 && ah > 0 &&
            ax + aw <= out.frameW && ay + ah <= out.frameH) {
            out.cropX = ax;
            out.cropY = ay;
            out.cropW = aw;
            out.cropH = ah;
        }
    }

    UINT32 rawStride = 0;
    if (SUCCEEDED(mediaType->GetUINT32(MF_MT_DEFAULT_STRIDE, &rawStride)) && rawStride != 0) {
        out.defaultStride = static_cast<LONG>(rawStride);
    } else {
        out.defaultStride = static_cast<LONG>(out.isNv12 ? out.frameW : (out.frameW * 4));
    }

    UINT32 fpsNum = 0, fpsDen = 0;
    if (SUCCEEDED(MFGetAttributeRatio(mediaType, MF_MT_FRAME_RATE, &fpsNum, &fpsDen)) &&
        fpsNum > 0 && fpsDen > 0) {
        const double fps = static_cast<double>(fpsNum) / static_cast<double>(fpsDen);
        if (fps >= 5.0 && fps <= 240.0) {
            out.frameDuration100ns = static_cast<LONGLONG>(10000000.0 / fps);
        }
    }

    mediaType->Release();
    return true;
}

// Creates an in-memory IMFByteStream for video files <= 48 MB so infinite loop playback
// performs 0.0 MB/s disk I/O after the initial read without bloating RAM on huge files.
IMFByteStream* createInMemoryByteStream(const QString& nativePath) {
    QFile f(nativePath);
    const qint64 sz = f.size();
    if (sz <= 0 || sz > 48LL * 1024LL * 1024LL) {
        return nullptr;
    }
    if (!f.open(QIODevice::ReadOnly)) {
        return nullptr;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        return nullptr;
    }

    IStream* memStream = SHCreateMemStream(
        reinterpret_cast<const BYTE*>(bytes.constData()),
        static_cast<UINT>(bytes.size()));
    if (!memStream) {
        return nullptr;
    }

    using PFN_MFCreateMFByteStreamOnStream = HRESULT(WINAPI*)(IStream*, IMFByteStream**);
    static const auto pfnCreateOnStream = reinterpret_cast<PFN_MFCreateMFByteStreamOnStream>(
        GetProcAddress(GetModuleHandleW(L"mfplat.dll"), "MFCreateMFByteStreamOnStream"));
    if (!pfnCreateOnStream) {
        memStream->Release();
        return nullptr;
    }

    IMFByteStream* mfStream = nullptr;
    const HRESULT hr = pfnCreateOnStream(memStream, &mfStream);
    memStream->Release();
    if (FAILED(hr) || !mfStream) {
        return nullptr;
    }

    IMFAttributes* streamAttrs = nullptr;
    if (SUCCEEDED(mfStream->QueryInterface(IID_PPV_ARGS(&streamAttrs))) && streamAttrs) {
        const std::wstring wpath = QDir::toNativeSeparators(nativePath).toStdWString();
        streamAttrs->SetString(MF_BYTESTREAM_ORIGIN_NAME, wpath.c_str());
        const QString ext = QFileInfo(nativePath).suffix().toLower();
        if (ext == QStringLiteral("mp4") || ext == QStringLiteral("mov")) {
            streamAttrs->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/mp4");
        } else if (ext == QStringLiteral("webm")) {
            streamAttrs->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/webm");
        } else if (ext == QStringLiteral("mkv")) {
            streamAttrs->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/x-matroska");
        } else if (ext == QStringLiteral("avi")) {
            streamAttrs->SetString(MF_BYTESTREAM_CONTENT_TYPE, L"video/avi");
        }
        streamAttrs->Release();
    }

    return mfStream;
}

IDXGIAdapter1* selectLowPowerVideoAdapter() {
    HMODULE dxgiMod = LoadLibraryExW(L"dxgi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!dxgiMod) {
        return nullptr;
    }
    using CreateDXGIFactory1Fn = HRESULT(WINAPI*)(REFIID, void**);
    auto createFactory1 = reinterpret_cast<CreateDXGIFactory1Fn>(
        GetProcAddress(dxgiMod, "CreateDXGIFactory1"));
    if (!createFactory1) {
        return nullptr;
    }

    IDXGIAdapter1* chosen = nullptr;
    IDXGIFactory1* factory1 = nullptr;
    if (SUCCEEDED(createFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory1))) && factory1) {
        IDXGIFactory6* factory6 = nullptr;
        if (SUCCEEDED(factory1->QueryInterface(__uuidof(IDXGIFactory6), reinterpret_cast<void**>(&factory6))) && factory6) {
            for (UINT i = 0;; ++i) {
                IDXGIAdapter1* adapter = nullptr;
                if (FAILED(factory6->EnumAdapterByGpuPreference(
                        i, DXGI_GPU_PREFERENCE_MINIMUM_POWER, __uuidof(IDXGIAdapter1),
                        reinterpret_cast<void**>(&adapter))) || !adapter) {
                    break;
                }
                DXGI_ADAPTER_DESC1 desc = {};
                if (SUCCEEDED(adapter->GetDesc1(&desc))) {
                    const bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
                    const bool isKnownVendor = (desc.VendorId == 0x8086 || desc.VendorId == 0x1002 ||
                                                desc.VendorId == 0x10DE || desc.VendorId == 0x5143);
                    if (!isSoftware && isKnownVendor) {
                        chosen = adapter;
                        break;
                    }
                }
                adapter->Release();
            }
            factory6->Release();
        }
        factory1->Release();
    }
    return chosen;
}

constexpr int kGpuPoolSlots = 3;

struct GpuOutputPool {
    ID3D11Texture2D* outTex[kGpuPoolSlots] = {};
    ID3D11VideoProcessorOutputView* outView[kGpuPoolSlots] = {};
    HANDLE sharedHandle[kGpuPoolSlots] = {};
    int width = 0;
    int height = 0;
    uint64_t poolId = 0;

    ~GpuOutputPool() {
        for (int i = 0; i < kGpuPoolSlots; ++i) {
            if (outView[i]) {
                outView[i]->Release();
                outView[i] = nullptr;
            }
            if (outTex[i]) {
                outTex[i]->Release();
                outTex[i] = nullptr;
            }
            sharedHandle[i] = nullptr;
        }
    }
};

struct DxgiVideoContext {
    ID3D11Device* d3dDevice = nullptr;
    ID3D11DeviceContext* d3dCtx = nullptr;
    IMFDXGIDeviceManager* dxgiMgr = nullptr;
    ID3D11VideoDevice* videoDevice = nullptr;
    ID3D11VideoContext* videoCtx = nullptr;
    ID3D11VideoProcessorEnumerator* videoEnum = nullptr;
    ID3D11VideoProcessor* videoProc = nullptr;
    std::shared_ptr<GpuOutputPool> outPool;
    QHash<quint64, ID3D11VideoProcessorInputView*> inViewCache;
    int vpSrcW = 0;
    int vpSrcH = 0;
    int vpDstW = 0;
    int vpDstH = 0;
    RECT lastSrcRect = {};
    RECT lastDstRect = {};
    bool hasVpRects = false;
    UINT resetToken = 0;
    bool gpuBlitBroken = false;

    bool init() {
        const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
            D3D_FEATURE_LEVEL_10_1,
            D3D_FEATURE_LEVEL_10_0
        };
        D3D_FEATURE_LEVEL flOut = D3D_FEATURE_LEVEL_11_0;
        // Always bind D3D11VA decoding to the low-power integrated GPU (UMA) so we never
        // wake up the discrete NVIDIA GPU (+200 MB driver heap) and always match Qt's QRhi GPU LUID.
        IDXGIAdapter1* lowPowerAdapter = selectLowPowerVideoAdapter();
        HRESULT hrDev = D3D11CreateDevice(
            lowPowerAdapter,
            lowPowerAdapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,
            D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels,
            static_cast<UINT>(std::size(levels)),
            D3D11_SDK_VERSION,
            &d3dDevice,
            &flOut,
            &d3dCtx);
        if (lowPowerAdapter) {
            lowPowerAdapter->Release();
        }
        if (FAILED(hrDev) || !d3dDevice) {
            hrDev = D3D11CreateDevice(
                nullptr,
                D3D_DRIVER_TYPE_HARDWARE,
                nullptr,
                D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                levels,
                static_cast<UINT>(std::size(levels)),
                D3D11_SDK_VERSION,
                &d3dDevice,
                &flOut,
                &d3dCtx);
        }
        if (FAILED(hrDev) || !d3dDevice) {
            return false;
        }
        ID3D10Multithread* mt = nullptr;
        if (SUCCEEDED(d3dDevice->QueryInterface(IID_PPV_ARGS(&mt))) && mt) {
            mt->SetMultithreadProtected(TRUE);
            mt->Release();
        }
        if (FAILED(MFCreateDXGIDeviceManager(&resetToken, &dxgiMgr)) || !dxgiMgr) {
            release();
            return false;
        }
        if (FAILED(dxgiMgr->ResetDevice(d3dDevice, resetToken))) {
            release();
            return false;
        }
        // Acquire ID3D11VideoDevice & ID3D11VideoContext for zero-copy GPU NV12->BGRA8 conversion
        if (FAILED(d3dDevice->QueryInterface(IID_PPV_ARGS(&videoDevice)))) {
            videoDevice = nullptr;
        }
        if (FAILED(d3dCtx->QueryInterface(IID_PPV_ARGS(&videoCtx)))) {
            videoCtx = nullptr;
        }
        return true;
    }

    void clearInputViews() {
        for (auto it = inViewCache.begin(); it != inViewCache.end(); ++it) {
            if (it.value()) {
                it.value()->Release();
            }
        }
        inViewCache.clear();
    }

    void releaseVideoProcessor() {
        clearInputViews();
        outPool.reset();
        if (videoProc) {
            videoProc->Release();
            videoProc = nullptr;
        }
        if (videoEnum) {
            videoEnum->Release();
            videoEnum = nullptr;
        }
        vpSrcW = 0;
        vpSrcH = 0;
        vpDstW = 0;
        vpDstH = 0;
        hasVpRects = false;
    }

    bool ensureVideoProcessor(int srcW, int srcH, int dstW, int dstH) {
        if (!d3dDevice || !videoDevice || !videoCtx || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) {
            return false;
        }
        if (videoProc && videoEnum && outPool &&
            vpSrcW == srcW && vpSrcH == srcH && vpDstW == dstW && vpDstH == dstH) {
            return true;
        }
        releaseVideoProcessor();

        D3D11_VIDEO_PROCESSOR_CONTENT_DESC desc = {};
        desc.InputFrameFormat = D3D11_VIDEO_FRAME_FORMAT_PROGRESSIVE;
        desc.InputWidth = static_cast<UINT>(srcW);
        desc.InputHeight = static_cast<UINT>(srcH);
        desc.OutputWidth = static_cast<UINT>(dstW);
        desc.OutputHeight = static_cast<UINT>(dstH);
        desc.Usage = D3D11_VIDEO_USAGE_PLAYBACK_NORMAL;

        if (FAILED(videoDevice->CreateVideoProcessorEnumerator(&desc, &videoEnum)) || !videoEnum) {
            releaseVideoProcessor();
            return false;
        }
        if (FAILED(videoDevice->CreateVideoProcessor(videoEnum, 0, &videoProc)) || !videoProc) {
            releaseVideoProcessor();
            return false;
        }

        // Configure BT.709/BT.601 studio-range NV12 -> full-range opaque BGRA8
        D3D11_VIDEO_PROCESSOR_COLOR_SPACE inCs = {};
        inCs.Usage = 0;
        inCs.RGB_Range = 0;
        inCs.YCbCr_Matrix = (srcH >= 720) ? 1 : 0;
        inCs.YCbCr_xvYCC = 0;
        inCs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_16_235;
        videoCtx->VideoProcessorSetStreamColorSpace(videoProc, 0, &inCs);

        D3D11_VIDEO_PROCESSOR_COLOR_SPACE outCs = {};
        outCs.Usage = 0;
        outCs.RGB_Range = 0;
        outCs.Nominal_Range = D3D11_VIDEO_PROCESSOR_NOMINAL_RANGE_0_255;
        videoCtx->VideoProcessorSetOutputColorSpace(videoProc, &outCs);
        D3D11_VIDEO_COLOR bgColor = {};
        bgColor.RGBA.R = 0.0f;
        bgColor.RGBA.G = 0.0f;
        bgColor.RGBA.B = 0.0f;
        bgColor.RGBA.A = 1.0f;
        videoCtx->VideoProcessorSetOutputBackgroundColor(videoProc, FALSE, &bgColor);
        videoCtx->VideoProcessorSetOutputAlphaFillMode(
            videoProc, D3D11_VIDEO_PROCESSOR_ALPHA_FILL_MODE_OPAQUE, 0);

        auto pool = std::make_shared<GpuOutputPool>();
        static std::atomic<uint64_t> s_nextPoolId{1};
        pool->poolId = s_nextPoolId.fetch_add(1, std::memory_order_relaxed);
        pool->width = dstW;
        pool->height = dstH;

        D3D11_TEXTURE2D_DESC td = {};
        td.Width = static_cast<UINT>(dstW);
        td.Height = static_cast<UINT>(dstH);
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.SampleDesc.Quality = 0;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        td.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

        D3D11_VIDEO_PROCESSOR_OUTPUT_VIEW_DESC ovd = {};
        ovd.ViewDimension = D3D11_VPOV_DIMENSION_TEXTURE2D;
        ovd.Texture2D.MipSlice = 0;

        for (int i = 0; i < kGpuPoolSlots; ++i) {
            if (FAILED(d3dDevice->CreateTexture2D(&td, nullptr, &pool->outTex[i])) || !pool->outTex[i]) {
                releaseVideoProcessor();
                return false;
            }
            IDXGIResource* dxgiRes = nullptr;
            if (FAILED(pool->outTex[i]->QueryInterface(IID_PPV_ARGS(&dxgiRes))) || !dxgiRes) {
                releaseVideoProcessor();
                return false;
            }
            const HRESULT hrHandle = dxgiRes->GetSharedHandle(&pool->sharedHandle[i]);
            dxgiRes->Release();
            if (FAILED(hrHandle) || !pool->sharedHandle[i]) {
                releaseVideoProcessor();
                return false;
            }
            if (FAILED(videoDevice->CreateVideoProcessorOutputView(pool->outTex[i], videoEnum, &ovd, &pool->outView[i])) || !pool->outView[i]) {
                releaseVideoProcessor();
                return false;
            }
        }

        outPool = std::move(pool);
        vpSrcW = srcW;
        vpSrcH = srcH;
        vpDstW = dstW;
        vpDstH = dstH;
        return true;
    }

    bool blitSampleToSharedGpu(IMFSample* sample, const VideoFormatInfo& fmt,
                               int maxW, int maxH, int slot, UINT frameIdx,
                               SharedGpuFrame& outGpuFrame) {
        if (gpuBlitBroken || !sample || !videoDevice || !videoCtx || fmt.cropW <= 0 || fmt.cropH <= 0) {
            return false;
        }
        IMFMediaBuffer* buf = nullptr;
        if (FAILED(sample->GetBufferByIndex(0, &buf)) || !buf) {
            return false;
        }
        IMFDXGIBuffer* dxgiBuf = nullptr;
        if (FAILED(buf->QueryInterface(IID_PPV_ARGS(&dxgiBuf))) || !dxgiBuf) {
            buf->Release();
            return false;
        }
        ID3D11Texture2D* srcTex = nullptr;
        UINT subIdx = 0;
        const HRESULT hrRes = dxgiBuf->GetResource(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&srcTex));
        const HRESULT hrSub = dxgiBuf->GetSubresourceIndex(&subIdx);
        dxgiBuf->Release();
        buf->Release();
        if (FAILED(hrRes) || FAILED(hrSub) || !srcTex) {
            if (srcTex) srcTex->Release();
            return false;
        }

        D3D11_TEXTURE2D_DESC srcDesc = {};
        srcTex->GetDesc(&srcDesc);
        const int srcW = static_cast<int>(srcDesc.Width);
        const int srcH = static_cast<int>(srcDesc.Height);

        const int w = fmt.cropW;
        const int h = fmt.cropH;
        const bool needScale = (maxW > 0 && w > maxW) || (maxH > 0 && h > maxH);
        QSize bounded(w, h);
        if (needScale) {
            bounded = QSize(w, h).scaled(
                maxW > 0 ? maxW : w,
                maxH > 0 ? maxH : h,
                Qt::KeepAspectRatio);
        }
        const int dstW = std::max(2, bounded.width());
        const int dstH = std::max(2, bounded.height());

        if (!ensureVideoProcessor(srcW, srcH, dstW, dstH)) {
            srcTex->Release();
            gpuBlitBroken = true;
            return false;
        }

        // Cache ID3D11VideoProcessorInputView per (srcTex pointer, ArraySlice) since D3D11VA reuses
        // a small fixed ring of decoder output slices.
        const quint64 viewKey = (reinterpret_cast<quintptr>(srcTex) << 8) ^ static_cast<quint64>(subIdx);
        ID3D11VideoProcessorInputView* inView = inViewCache.value(viewKey, nullptr);
        if (!inView) {
            if (inViewCache.size() >= 32) {
                clearInputViews();
            }
            D3D11_VIDEO_PROCESSOR_INPUT_VIEW_DESC ivd = {};
            ivd.FourCC = 0;
            ivd.ViewDimension = D3D11_VPIV_DIMENSION_TEXTURE2D;
            ivd.Texture2D.MipSlice = 0;
            ivd.Texture2D.ArraySlice = subIdx;
            if (FAILED(videoDevice->CreateVideoProcessorInputView(srcTex, videoEnum, &ivd, &inView)) || !inView) {
                srcTex->Release();
                gpuBlitBroken = true;
                return false;
            }
            inViewCache.insert(viewKey, inView);
        }

        RECT srcRect = { fmt.cropX, fmt.cropY, fmt.cropX + fmt.cropW, fmt.cropY + fmt.cropH };
        RECT dstRect = { 0, 0, dstW, dstH };
        if (!hasVpRects ||
            !EqualRect(&srcRect, &lastSrcRect) ||
            !EqualRect(&dstRect, &lastDstRect)) {
            videoCtx->VideoProcessorSetStreamSourceRect(videoProc, 0, TRUE, &srcRect);
            videoCtx->VideoProcessorSetStreamDestRect(videoProc, 0, TRUE, &dstRect);
            videoCtx->VideoProcessorSetOutputTargetRect(videoProc, TRUE, &dstRect);
            lastSrcRect = srcRect;
            lastDstRect = dstRect;
            hasVpRects = true;
        }

        D3D11_VIDEO_PROCESSOR_STREAM stream = {};
        stream.Enable = TRUE;
        stream.OutputIndex = 0;
        stream.InputFrameOrField = 0;
        stream.pInputSurface = inView;

        const HRESULT hrBlt = videoCtx->VideoProcessorBlt(
            videoProc, outPool->outView[slot], frameIdx, 1, &stream);
        srcTex->Release();

        if (FAILED(hrBlt)) {
            gpuBlitBroken = true;
            return false;
        }

        d3dCtx->Flush();

        outGpuFrame.sharedHandle = outPool->sharedHandle[slot];
        outGpuFrame.width = dstW;
        outGpuFrame.height = dstH;
        outGpuFrame.slot = slot;
        outGpuFrame.poolId = outPool->poolId;
        outGpuFrame.poolKeepAlive = outPool;
        return true;
    }

    void release() {
        releaseVideoProcessor();
        if (videoCtx) {
            videoCtx->Release();
            videoCtx = nullptr;
        }
        if (videoDevice) {
            videoDevice->Release();
            videoDevice = nullptr;
        }
        if (dxgiMgr) {
            dxgiMgr->Release();
            dxgiMgr = nullptr;
        }
        if (d3dCtx) {
            d3dCtx->Release();
            d3dCtx = nullptr;
        }
        if (d3dDevice) {
            d3dDevice->Release();
            d3dDevice = nullptr;
        }
    }

    ~DxgiVideoContext() {
        release();
    }
};

IMFSourceReader* createSourceReaderWithAttrs(const QString& nativePath, IMFByteStream* memStream,
                                             IMFDXGIDeviceManager* dxgiMgr,
                                             bool enableVideoProcessing, bool enableHwTransforms) {
    IMFAttributes* attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 4)) || !attrs) {
        return nullptr;
    }
    attrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, enableVideoProcessing ? TRUE : FALSE);
    attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, enableHwTransforms ? TRUE : FALSE);
    if (dxgiMgr && enableHwTransforms) {
        attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, dxgiMgr);
    }

    IMFSourceReader* reader = nullptr;
    HRESULT hr = E_FAIL;
    if (memStream) {
        QWORD pos = 0;
        memStream->SetCurrentPosition(pos);
        hr = MFCreateSourceReaderFromByteStream(memStream, attrs, &reader);
    }
    if (FAILED(hr) || !reader) {
        const std::wstring wpath = QDir::toNativeSeparators(nativePath).toStdWString();
        hr = MFCreateSourceReaderFromURL(wpath.c_str(), attrs, &reader);
    }
    attrs->Release();

    if (FAILED(hr) || !reader) {
        return nullptr;
    }

    reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE))) {
        reader->Release();
        return nullptr;
    }
    return reader;
}

IMFSourceReader* openVideoReader(const QString& nativePath, IMFByteStream* memStream,
                                 IMFDXGIDeviceManager* dxgiMgr, VideoFormatInfo& fmt) {
    if (nativePath.isEmpty() || !QFileInfo::exists(nativePath)) {
        return nullptr;
    }

    // 1. Prefer direct NV12 output with D3D11VA hardware acceleration (or software NV12 fallback)
    for (IMFDXGIDeviceManager* mgr : {dxgiMgr, static_cast<IMFDXGIDeviceManager*>(nullptr)}) {
        if (!mgr && !dxgiMgr && mgr != nullptr) continue;
        if (IMFSourceReader* reader = createSourceReaderWithAttrs(nativePath, memStream, mgr, false, true)) {
            IMFMediaType* nv12Type = nullptr;
            if (SUCCEEDED(MFCreateMediaType(&nv12Type)) && nv12Type) {
                nv12Type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                nv12Type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
                const HRESULT hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, nv12Type);
                nv12Type->Release();
                if (SUCCEEDED(hr) && queryVideoFormat(reader, fmt) && fmt.isNv12) {
                    return reader;
                }
            }
            reader->Release();
        }
        if (!dxgiMgr) break;
    }

    // 2. Fallback: RGB32 via Video Processor MFT if a codec does not expose NV12
    IMFSourceReader* reader = createSourceReaderWithAttrs(nativePath, memStream, nullptr, true, true);
    if (!reader) {
        reader = createSourceReaderWithAttrs(nativePath, memStream, nullptr, true, false);
    }
    if (!reader) {
        return nullptr;
    }

    IMFMediaType* rgbType = nullptr;
    if (FAILED(MFCreateMediaType(&rgbType)) || !rgbType) {
        reader->Release();
        return nullptr;
    }
    rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    const HRESULT hr = reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgbType);
    rgbType->Release();
    if (FAILED(hr) || !queryVideoFormat(reader, fmt)) {
        reader->Release();
        return nullptr;
    }
    return reader;
}

bool seekReader(IMFSourceReader* reader, LONGLONG pos100ns) {
    PROPVARIANT var;
    PropVariantInit(&var);
    var.vt = VT_I8;
    var.hVal.QuadPart = pos100ns;
    const HRESULT hr = reader->SetCurrentPosition(GUID_NULL, var);
    PropVariantClear(&var);
    return SUCCEEDED(hr);
}

bool decodeNv12ToImage(IMFMediaBuffer* buf, const VideoFormatInfo& fmt,
                       int maxW, int maxH, QImage& outFrame) {
    BYTE* scanline0 = nullptr;
    LONG pitch = 0;
    IMF2DBuffer2* buf2d2 = nullptr;
    IMF2DBuffer* buf2d = nullptr;
    bool locked2d2 = false;
    bool locked2d = false;
    bool locked1d = false;
    DWORD curLen = 0;
    buf->GetCurrentLength(&curLen);

    if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d2))) && buf2d2) {
        BYTE* bufStart = nullptr;
        DWORD bufLen = 0;
        if (SUCCEEDED(buf2d2->Lock2DSize(MF2DBuffer_LockFlags_Read, &scanline0, &pitch, &bufStart, &bufLen)) &&
            scanline0 && pitch > 0) {
            locked2d2 = true;
            if (bufLen > 0) {
                curLen = bufLen;
            }
        } else {
            buf2d2->Release();
            buf2d2 = nullptr;
        }
    }

    if (!locked2d2 && SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) && buf2d) {
        if (SUCCEEDED(buf2d->Lock2D(&scanline0, &pitch)) && scanline0 && pitch > 0) {
            locked2d = true;
        } else {
            buf2d->Release();
            buf2d = nullptr;
        }
    }

    if (!locked2d) {
        BYTE* rawPtr = nullptr;
        DWORD maxLen = 0;
        if (SUCCEEDED(buf->Lock(&rawPtr, &maxLen, &curLen)) && rawPtr) {
            locked1d = true;
            pitch = fmt.defaultStride > 0 ? fmt.defaultStride : static_cast<LONG>(fmt.frameW);
            scanline0 = rawPtr;
        }
    }

    bool ok = false;
    if (scanline0 && pitch >= fmt.cropX + fmt.cropW) {
        // Compute physical Y-plane row height (handles 1080->1088 macroblock alignment automatically)
        int yPlaneRows = fmt.frameH;
        if (curLen > 0 && pitch > 0) {
            const int derivedH = static_cast<int>((static_cast<uint64_t>(curLen) * 2ULL) / (static_cast<uint64_t>(pitch) * 3ULL));
            if (derivedH >= fmt.frameH && derivedH <= fmt.frameH + 64 && (derivedH % 2 == 0)) {
                yPlaneRows = derivedH;
            }
        }
        const DWORD minBytes = static_cast<DWORD>(pitch) * static_cast<DWORD>(yPlaneRows + (fmt.cropY + fmt.cropH) / 2);
        if (locked2d2 || locked2d || curLen >= minBytes) {
            const BYTE* yPlane = scanline0;
            const BYTE* uvPlane = scanline0 + static_cast<ptrdiff_t>(pitch) * yPlaneRows;

            const int w = fmt.cropW;
            const int h = fmt.cropH;
            const bool needScale = (maxW > 0 && w > maxW) || (maxH > 0 && h > maxH);
            QSize bounded(w, h);
            if (needScale) {
                bounded = QSize(w, h).scaled(
                    maxW > 0 ? maxW : w,
                    maxH > 0 ? maxH : h,
                    Qt::KeepAspectRatio);
            }
            const int dstW = std::max(1, bounded.width());
            const int dstH = std::max(1, bounded.height());
            if (outFrame.size() != QSize(dstW, dstH) || outFrame.format() != QImage::Format_RGB32 || !outFrame.isDetached()) {
                outFrame = QImage(dstW, dstH, QImage::Format_RGB32);
            }

            // Copy scanlines sequentially out of D3D11 Write-Combined (USWC) mapped surfaces
            // into L1-cached row buffers before converting YUV->RGB. Alternating byte reads
            // between Y and UV directly in USWC memory flushes the CPU's 64B line fill buffer
            // on every pixel (~4M stalls/frame); sequential memcpy + L1 conversion is ~10x faster.
            const int uvCopyBytes = (w + 3) & ~1;
            std::vector<uint8_t> yLineBuf(static_cast<size_t>(w + 4));
            std::vector<uint8_t> uvLineBuf(static_cast<size_t>(uvCopyBytes + 4));
            const int uvCropOffset = fmt.cropX & ~1;
            const int xParity = fmt.cropX & 1;
            int cachedSy = -1;
            int cachedUvSy = -1;

            if (!needScale && xParity == 0) {
                for (int dy = 0; dy < dstH; ++dy) {
                    const int sy = fmt.cropY + dy;
                    const BYTE* yRow = yPlane + static_cast<ptrdiff_t>(sy) * pitch + fmt.cropX;
                    std::memcpy(yLineBuf.data(), yRow, static_cast<size_t>(w));

                    const int uvSy = sy >> 1;
                    if (uvSy != cachedUvSy) {
                        cachedUvSy = uvSy;
                        const BYTE* uvRow = uvPlane + static_cast<ptrdiff_t>(uvSy) * pitch + uvCropOffset;
                        std::memcpy(uvLineBuf.data(), uvRow, static_cast<size_t>((w + 1) & ~1));
                    }

                    const uint8_t* ySrc = yLineBuf.data();
                    const uint8_t* uvSrc = uvLineBuf.data();
                    auto* dstPx = reinterpret_cast<quint32*>(outFrame.scanLine(dy));

                    int dx = 0;
                    for (; dx + 1 < dstW; dx += 2) {
                        const int U = static_cast<int>(uvSrc[dx]) - 128;
                        const int V = static_cast<int>(uvSrc[dx + 1]) - 128;
                        const int rChroma = 459 * V + 128;
                        const int gChroma = -55 * U - 136 * V + 128;
                        const int bChroma = 541 * U + 128;

                        const int y0 = static_cast<int>(ySrc[dx]);
                        const int c0 = (y0 > 16 ? (y0 - 16) : 0) * 298;
                        int r0 = (c0 + rChroma) >> 8;
                        int g0 = (c0 + gChroma) >> 8;
                        int b0 = (c0 + bChroma) >> 8;
                        r0 = r0 < 0 ? 0 : (r0 > 255 ? 255 : r0);
                        g0 = g0 < 0 ? 0 : (g0 > 255 ? 255 : g0);
                        b0 = b0 < 0 ? 0 : (b0 > 255 ? 255 : b0);
                        dstPx[dx] = 0xFF000000u | (static_cast<quint32>(r0) << 16) | (static_cast<quint32>(g0) << 8) | static_cast<quint32>(b0);

                        const int y1 = static_cast<int>(ySrc[dx + 1]);
                        const int c1 = (y1 > 16 ? (y1 - 16) : 0) * 298;
                        int r1 = (c1 + rChroma) >> 8;
                        int g1 = (c1 + gChroma) >> 8;
                        int b1 = (c1 + bChroma) >> 8;
                        r1 = r1 < 0 ? 0 : (r1 > 255 ? 255 : r1);
                        g1 = g1 < 0 ? 0 : (g1 > 255 ? 255 : g1);
                        b1 = b1 < 0 ? 0 : (b1 > 255 ? 255 : b1);
                        dstPx[dx + 1] = 0xFF000000u | (static_cast<quint32>(r1) << 16) | (static_cast<quint32>(g1) << 8) | static_cast<quint32>(b1);
                    }
                    if (dx < dstW) {
                        const int uvIdx = dx & ~1;
                        const int U = static_cast<int>(uvSrc[uvIdx]) - 128;
                        const int V = static_cast<int>(uvSrc[uvIdx + 1]) - 128;
                        const int y0 = static_cast<int>(ySrc[dx]);
                        const int c0 = (y0 > 16 ? (y0 - 16) : 0) * 298;
                        int r0 = (c0 + 459 * V + 128) >> 8;
                        int g0 = (c0 - 55 * U - 136 * V + 128) >> 8;
                        int b0 = (c0 + 541 * U + 128) >> 8;
                        r0 = r0 < 0 ? 0 : (r0 > 255 ? 255 : r0);
                        g0 = g0 < 0 ? 0 : (g0 > 255 ? 255 : g0);
                        b0 = b0 < 0 ? 0 : (b0 > 255 ? 255 : b0);
                        dstPx[dx] = 0xFF000000u | (static_cast<quint32>(r0) << 16) | (static_cast<quint32>(g0) << 8) | static_cast<quint32>(b0);
                    }
                }
            } else {
                std::vector<int> xMap(dstW);
                for (int dx = 0; dx < dstW; ++dx) {
                    xMap[dx] = (dx * w) / dstW;
                }

                for (int dy = 0; dy < dstH; ++dy) {
                    const int sy = fmt.cropY + (dy * h) / dstH;
                    if (sy != cachedSy) {
                        cachedSy = sy;
                        const BYTE* yRow = yPlane + static_cast<ptrdiff_t>(sy) * pitch + fmt.cropX;
                        std::memcpy(yLineBuf.data(), yRow, static_cast<size_t>(w));
                    }
                    const int uvSy = sy >> 1;
                    if (uvSy != cachedUvSy) {
                        cachedUvSy = uvSy;
                        const BYTE* uvRow = uvPlane + static_cast<ptrdiff_t>(uvSy) * pitch + uvCropOffset;
                        std::memcpy(uvLineBuf.data(), uvRow, static_cast<size_t>(uvCopyBytes));
                    }

                    const uint8_t* ySrc = yLineBuf.data();
                    const uint8_t* uvSrc = uvLineBuf.data();
                    auto* dstPx = reinterpret_cast<quint32*>(outFrame.scanLine(dy));

                    for (int dx = 0; dx < dstW; ++dx) {
                        const int relX = xMap[dx];
                        const int Y = static_cast<int>(ySrc[relX]);
                        const int uvIdx = (relX + xParity) & ~1;
                        const int U = static_cast<int>(uvSrc[uvIdx]) - 128;
                        const int V = static_cast<int>(uvSrc[uvIdx + 1]) - 128;

                        const int c = (Y > 16 ? (Y - 16) : 0) * 298;
                        int r = (c + 459 * V + 128) >> 8;
                        int g = (c - 55 * U - 136 * V + 128) >> 8;
                        int b = (c + 541 * U + 128) >> 8;
                        r = r < 0 ? 0 : (r > 255 ? 255 : r);
                        g = g < 0 ? 0 : (g > 255 ? 255 : g);
                        b = b < 0 ? 0 : (b > 255 ? 255 : b);

                        dstPx[dx] = 0xFF000000u |
                                    (static_cast<quint32>(r) << 16) |
                                    (static_cast<quint32>(g) << 8) |
                                    static_cast<quint32>(b);
                    }
                }
            }
            ok = !outFrame.isNull();
        }
    }

    if (locked2d2 && buf2d2) {
        buf2d2->Unlock2D();
        buf2d2->Release();
    } else if (locked2d && buf2d) {
        buf2d->Unlock2D();
        buf2d->Release();
    } else if (locked1d) {
        buf->Unlock();
    }
    return ok;
}

bool decodeSampleToImage(IMFSample* sample, const VideoFormatInfo& fmt,
                         int maxW, int maxH, Qt::TransformationMode scaleMode,
                         QImage& reusableRgb32, QImage& outFrame) {
    if (!sample || fmt.cropW <= 0 || fmt.cropH <= 0) {
        return false;
    }

    IMFMediaBuffer* buf = nullptr;
    if (FAILED(sample->ConvertToContiguousBuffer(&buf)) || !buf) {
        return false;
    }

    if (fmt.isNv12) {
        const bool ok = decodeNv12ToImage(buf, fmt, maxW, maxH, outFrame);
        buf->Release();
        return ok;
    }

    BYTE* scanline0 = nullptr;
    LONG pitch = 0;
    IMF2DBuffer* buf2d = nullptr;
    bool locked2d = false;
    bool locked1d = false;
    DWORD curLen = 0;

    if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) && buf2d) {
        if (SUCCEEDED(buf2d->Lock2D(&scanline0, &pitch)) && scanline0 && pitch != 0) {
            locked2d = true;
        } else {
            buf2d->Release();
            buf2d = nullptr;
        }
    }

    if (!locked2d) {
        BYTE* rawPtr = nullptr;
        DWORD maxLen = 0;
        if (SUCCEEDED(buf->Lock(&rawPtr, &maxLen, &curLen)) && rawPtr) {
            locked1d = true;
            pitch = fmt.defaultStride != 0 ? fmt.defaultStride : static_cast<LONG>(fmt.frameW * 4);
            const DWORD absPitch = static_cast<DWORD>(pitch < 0 ? -pitch : pitch);
            const DWORD needed = absPitch * static_cast<DWORD>(fmt.frameH);
            if (curLen >= needed) {
                scanline0 = (pitch < 0)
                    ? (rawPtr + static_cast<ptrdiff_t>(fmt.frameH - 1) * static_cast<ptrdiff_t>(absPitch))
                    : rawPtr;
            }
        }
    }

    bool ok = false;
    if (scanline0 && pitch != 0) {
        const ptrdiff_t absPitch = pitch < 0 ? -static_cast<ptrdiff_t>(pitch) : static_cast<ptrdiff_t>(pitch);
        if (absPitch >= static_cast<ptrdiff_t>(fmt.cropX + fmt.cropW) * 4) {
            const int w = fmt.cropW;
            const int h = fmt.cropH;
            const ptrdiff_t xByteOffset = static_cast<ptrdiff_t>(fmt.cropX) * 4;
            const bool needScale = (maxW > 0 && w > maxW) || (maxH > 0 && h > maxH);

            if (!needScale) {
                outFrame = QImage(w, h, QImage::Format_RGB32);
                for (int y = 0; y < h; ++y) {
                    const BYTE* srcRowBytes = scanline0 + static_cast<ptrdiff_t>(fmt.cropY + y) * static_cast<ptrdiff_t>(pitch) + xByteOffset;
                    const auto* srcPx = reinterpret_cast<const quint32*>(srcRowBytes);
                    auto* dstPx = reinterpret_cast<quint32*>(outFrame.scanLine(y));
                    for (int x = 0; x < w; ++x) {
                        dstPx[x] = srcPx[x] | 0xFF000000u;
                    }
                }
                ok = !outFrame.isNull();
            } else if (scaleMode == Qt::FastTransformation) {
                const QSize bounded = QSize(w, h).scaled(
                    maxW > 0 ? maxW : w,
                    maxH > 0 ? maxH : h,
                    Qt::KeepAspectRatio);
                const int dstW = std::max(1, bounded.width());
                const int dstH = std::max(1, bounded.height());
                outFrame = QImage(dstW, dstH, QImage::Format_RGB32);
                for (int dy = 0; dy < dstH; ++dy) {
                    const int sy = (dy * h) / dstH;
                    const BYTE* srcRowBytes = scanline0 + static_cast<ptrdiff_t>(fmt.cropY + sy) * static_cast<ptrdiff_t>(pitch) + xByteOffset;
                    const auto* srcPx = reinterpret_cast<const quint32*>(srcRowBytes);
                    auto* dstPx = reinterpret_cast<quint32*>(outFrame.scanLine(dy));
                    for (int dx = 0; dx < dstW; ++dx) {
                        dstPx[dx] = srcPx[(dx * w) / dstW] | 0xFF000000u;
                    }
                }
                ok = !outFrame.isNull();
            } else {
                if (reusableRgb32.size() != QSize(w, h) ||
                    reusableRgb32.format() != QImage::Format_RGB32) {
                    reusableRgb32 = QImage(w, h, QImage::Format_RGB32);
                }
                for (int y = 0; y < h; ++y) {
                    const BYTE* srcRowBytes = scanline0 + static_cast<ptrdiff_t>(fmt.cropY + y) * static_cast<ptrdiff_t>(pitch) + xByteOffset;
                    const auto* srcPx = reinterpret_cast<const quint32*>(srcRowBytes);
                    auto* dstPx = reinterpret_cast<quint32*>(reusableRgb32.scanLine(y));
                    for (int x = 0; x < w; ++x) {
                        dstPx[x] = srcPx[x] | 0xFF000000u;
                    }
                }
                const QSize bounded = QSize(w, h).scaled(
                    maxW > 0 ? maxW : w,
                    maxH > 0 ? maxH : h,
                    Qt::KeepAspectRatio);
                outFrame = reusableRgb32.scaled(bounded, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
                ok = !outFrame.isNull();
            }
        }
    }

    if (locked2d && buf2d) {
        buf2d->Unlock2D();
        buf2d->Release();
    } else if (locked1d) {
        buf->Unlock();
    }
    buf->Release();
    return ok;
}

// ============================================================================
// SharedVideoStream: Decodes a video file ONCE and broadcasts frames to all
// monitors displaying that video (cuts CPU & RAM in half on dual-monitor setups).
// ============================================================================
class SharedVideoStream {
public:
    explicit SharedVideoStream(QString path)
        : m_path(std::move(path)) {}

    ~SharedVideoStream() {
        stop();
    }

    void addSubscriber(VideoWallpaperItem* item) {
        bool shouldStart = false;
        SharedGpuFrame lastGpuCopy;
        QImage lastFrameCopy;
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            if (std::find(m_subscribers.begin(), m_subscribers.end(), item) == m_subscribers.end()) {
                m_subscribers.push_back(item);
            }
            lastGpuCopy = m_lastGpuFrame;
            lastFrameCopy = m_lastDecodedFrame;
            if (!m_worker.joinable()) {
                m_stopRequested.store(false, std::memory_order_release);
                shouldStart = true;
            }
        }
        if (lastGpuCopy.sharedHandle && !m_gpuShareFailed.load(std::memory_order_acquire)) {
            item->deliverSharedGpuFrame(lastGpuCopy);
            item->notifyStreamPlaying(true);
        } else if (!lastFrameCopy.isNull()) {
            item->deliverSharedFrame(lastFrameCopy);
            item->notifyStreamPlaying(true);
        }
        if (shouldStart) {
            m_worker = std::thread([this]() { workerLoop(); });
        } else {
            m_ctrlCv.notify_all();
        }
    }

    // Returns true if no subscribers remain
    bool removeSubscriber(VideoWallpaperItem* item) {
        bool empty = false;
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            m_subscribers.erase(
                std::remove(m_subscribers.begin(), m_subscribers.end(), item),
                m_subscribers.end());
            empty = m_subscribers.empty();
        }
        item->notifyStreamPlaying(false);
        m_ctrlCv.notify_all();
        return empty;
    }

    void notifyControlChanged(bool requestRestart) {
        if (requestRestart) {
            std::lock_guard<std::mutex> lock(m_ctrlMutex);
            m_seekToStartRequested = true;
        }
        m_ctrlCv.notify_all();
    }

    void markGpuShareFailed() {
        m_gpuShareFailed.store(true, std::memory_order_release);
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_lastGpuFrame = SharedGpuFrame{};
    }

    void stop() {
        m_stopRequested.store(true, std::memory_order_release);
        m_ctrlCv.notify_all();
        if (m_worker.joinable()) {
            m_worker.join();
        }
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_lastGpuFrame = SharedGpuFrame{};
        m_lastDecodedFrame = QImage();
    }

private:
    struct ActiveConfig {
        bool anyActive = false;
        int maxW = 1920;
        int maxH = 1080;
        int targetFps = 60;
        bool smooth = false;
    };

    ActiveConfig queryActiveConfig() {
        ActiveConfig cfg;
        std::lock_guard<std::mutex> lock(m_subMutex);
        if (m_subscribers.empty()) {
            return cfg;
        }
        int bestW = 320;
        int bestH = 180;
        int bestFps = 15;
        bool wantSmooth = false;
        for (VideoWallpaperItem* sub : m_subscribers) {
            if (!sub->paused() && sub->isItemVisible()) {
                cfg.anyActive = true;
                bestW = std::max(bestW, sub->maxWidth());
                bestH = std::max(bestH, sub->maxHeight());
                bestFps = std::max(bestFps, sub->targetFps());
                wantSmooth = wantSmooth || sub->smoothScaling();
            }
        }
        cfg.maxW = bestW;
        cfg.maxH = bestH;
        cfg.targetFps = std::clamp(bestFps, 15, 200);
        cfg.smooth = wantSmooth;
        return cfg;
    }

    void broadcastPlaying(bool playing) {
        std::lock_guard<std::mutex> lock(m_subMutex);
        for (VideoWallpaperItem* sub : m_subscribers) {
            sub->notifyStreamPlaying(playing);
        }
    }

    void broadcastFailed() {
        std::lock_guard<std::mutex> lock(m_subMutex);
        for (VideoWallpaperItem* sub : m_subscribers) {
            sub->notifyStreamFailed();
        }
    }

    void broadcastFrame(const QImage& frame) {
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_lastDecodedFrame = frame;
        m_lastGpuFrame = SharedGpuFrame{};
        for (VideoWallpaperItem* sub : m_subscribers) {
            if (!sub->paused() && sub->isItemVisible()) {
                sub->deliverSharedFrame(frame);
            }
        }
    }

    void broadcastGpuFrame(const SharedGpuFrame& gpuFrame) {
        std::lock_guard<std::mutex> lock(m_subMutex);
        m_lastGpuFrame = gpuFrame;
        m_lastDecodedFrame = QImage();
        for (VideoWallpaperItem* sub : m_subscribers) {
            if (!sub->paused() && sub->isItemVisible()) {
                sub->deliverSharedGpuFrame(gpuFrame);
            }
        }
    }

    void workerLoop() {
        ComMfScope comMf;
        if (!comMf.mfInit) {
            qWarning("[VideoWallpaper] MFStartup failed");
            broadcastFailed();
            return;
        }

        // Cache video file in RAM once (for files <= 48 MB) so infinite loop playback has 0.0 MB/s disk I/O
        IMFByteStream* memStream = createInMemoryByteStream(m_path);
        DxgiVideoContext dxgiCtx;
        const bool hasHwDxva = dxgiCtx.init();

        VideoFormatInfo fmt;
        IMFSourceReader* reader = openVideoReader(m_path, memStream, hasHwDxva ? dxgiCtx.dxgiMgr : nullptr, fmt);
        if (!reader) {
            if (memStream) memStream->Release();
            qWarning("[VideoWallpaper] Could not open video with Windows Media Foundation: %s",
                     qPrintable(m_path));
            broadcastFailed();
            return;
        }

        qInfo("[VideoWallpaper] Shared stream playing %s (%dx%d, ~%.1f fps, format=%s, inRam=%d, d3d11va=%d)",
              qPrintable(QFileInfo(m_path).fileName()),
              fmt.cropW, fmt.cropH,
              fmt.frameDuration100ns > 0 ? (10000000.0 / static_cast<double>(fmt.frameDuration100ns)) : 30.0,
              fmt.isNv12 ? "NV12" : "RGB32",
              memStream ? 1 : 0,
              hasHwDxva ? 1 : 0);

        broadcastPlaying(true);

        QImage scratchBuf;
        QImage framePool[3];
        int poolIdx = 0;
        int gpuSlot = 0;
        UINT gpuFrameIdx = 0;
        bool loggedZeroCopy = false;
        bool hasBasePts = false;
        LONGLONG basePts100ns = 0;
        LONGLONG lastPts100ns = 0;
        LONGLONG lastRenderedPts100ns = -100000000LL;
        auto baseWall = std::chrono::steady_clock::now();
        int consecutiveErrors = 0;

        while (!m_stopRequested.load(std::memory_order_acquire)) {
            ActiveConfig cfg = queryActiveConfig();
            if (!cfg.anyActive) {
                std::unique_lock<std::mutex> lock(m_ctrlMutex);
                while (!m_stopRequested.load(std::memory_order_acquire)) {
                    cfg = queryActiveConfig();
                    if (cfg.anyActive) {
                        break;
                    }
                    m_ctrlCv.wait(lock);
                }
                if (m_stopRequested.load(std::memory_order_acquire)) {
                    break;
                }
                if (m_seekToStartRequested) {
                    m_seekToStartRequested = false;
                    seekReader(reader, 0);
                }
                hasBasePts = false;
            }

            {
                std::lock_guard<std::mutex> lock(m_ctrlMutex);
                if (m_seekToStartRequested) {
                    m_seekToStartRequested = false;
                    seekReader(reader, 0);
                    hasBasePts = false;
                }
            }

            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG samplePts100ns = 0;
            IMFSample* sample = nullptr;
            const HRESULT hr = reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                0,
                &streamIndex,
                &flags,
                &samplePts100ns,
                &sample);

            if (FAILED(hr)) {
                if (sample) sample->Release();
                if (++consecutiveErrors > 10) {
                    qWarning("[VideoWallpaper] Aborting after repeated Media Foundation ReadSample errors (hr=0x%08lx)",
                             static_cast<unsigned long>(hr));
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(16));
                continue;
            }
            consecutiveErrors = 0;

            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                dxgiCtx.clearInputViews();
                queryVideoFormat(reader, fmt);
            }

            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (sample) sample->Release();
                if (!seekReader(reader, 0)) {
                    dxgiCtx.clearInputViews();
                    reader->Release();
                    reader = openVideoReader(m_path, memStream, hasHwDxva ? dxgiCtx.dxgiMgr : nullptr, fmt);
                    if (!reader) {
                        break;
                    }
                }
                hasBasePts = false;
                continue;
            }

            if (!sample) {
                continue;
            }

            const auto now = std::chrono::steady_clock::now();
            if (!hasBasePts) {
                hasBasePts = true;
                basePts100ns = samplePts100ns;
                lastPts100ns = samplePts100ns;
                lastRenderedPts100ns = -100000000LL;
                baseWall = now;
            } else {
                LONGLONG ptsDelta = samplePts100ns - basePts100ns;
                if (samplePts100ns <= lastPts100ns) {
                    ptsDelta = (lastPts100ns - basePts100ns) + fmt.frameDuration100ns;
                    samplePts100ns = lastPts100ns + fmt.frameDuration100ns;
                }
                lastPts100ns = samplePts100ns;

                const auto targetTime = baseWall + std::chrono::microseconds(ptsDelta / 10);
                if (targetTime > now) {
                    const auto waitDur = targetTime - now;
                    if (waitDur < std::chrono::seconds(2)) {
                        std::unique_lock<std::mutex> lock(m_ctrlMutex);
                        m_ctrlCv.wait_until(lock, targetTime, [&]() {
                            return m_stopRequested.load(std::memory_order_acquire) ||
                                   !queryActiveConfig().anyActive;
                        });
                    }
                } else if (now - targetTime > std::chrono::milliseconds(250)) {
                    basePts100ns = samplePts100ns;
                    baseWall = now;
                }
            }

            if (m_stopRequested.load(std::memory_order_acquire)) {
                sample->Release();
                break;
            }

            // Skip pixel conversion & GPU upload on intermediate frames when video FPS > targetFps
            const LONGLONG minFrameInterval100ns = 10000000LL / cfg.targetFps;
            if (lastRenderedPts100ns >= 0 && samplePts100ns > lastRenderedPts100ns &&
                (samplePts100ns - lastRenderedPts100ns) < (minFrameInterval100ns * 72 / 100)) {
                sample->Release();
                continue;
            }
            lastRenderedPts100ns = samplePts100ns;

            // Fast path: True Zero-Copy D3D11 GPU-to-GPU pipeline (NV12 -> shared BGRA8 texture via
            // ID3D11VideoContext::VideoProcessorBlt). Zero CPU pixel readback, zero UpdateSubresource!
            if (hasHwDxva && !m_gpuShareFailed.load(std::memory_order_acquire)) {
                SharedGpuFrame gpuFrame;
                if (dxgiCtx.blitSampleToSharedGpu(sample, fmt, cfg.maxW, cfg.maxH, gpuSlot, gpuFrameIdx, gpuFrame)) {
                    sample->Release();
                    gpuSlot = (gpuSlot + 1) % kGpuPoolSlots;
                    ++gpuFrameIdx;
                    if (!loggedZeroCopy) {
                        loggedZeroCopy = true;
                        qInfo("[VideoWallpaper] Zero-copy D3D11 GPU pipeline active (%dx%d BGRA8 shared, %d slots)",
                              gpuFrame.width, gpuFrame.height, kGpuPoolSlots);
                    }
                    broadcastGpuFrame(gpuFrame);
                    continue;
                }
            }

            const Qt::TransformationMode scaleMode = cfg.smooth ? Qt::SmoothTransformation : Qt::FastTransformation;
            QImage& frameSlot = framePool[poolIdx];
            poolIdx = (poolIdx + 1) % 3;
            const bool decoded = decodeSampleToImage(
                sample, fmt, cfg.maxW, cfg.maxH, scaleMode, scratchBuf, frameSlot);
            sample->Release();

            if (!decoded) {
                continue;
            }

            broadcastFrame(frameSlot);
        }

        if (reader) {
            reader->Release();
        }
        if (memStream) {
            memStream->Release();
        }
        dxgiCtx.release();
        {
            std::lock_guard<std::mutex> lock(m_subMutex);
            m_lastGpuFrame = SharedGpuFrame{};
            m_lastDecodedFrame = QImage();
        }
        broadcastPlaying(false);
    }

    QString m_path;
    std::mutex m_subMutex;
    std::vector<VideoWallpaperItem*> m_subscribers;
    SharedGpuFrame m_lastGpuFrame;
    QImage m_lastDecodedFrame;

    std::atomic<bool> m_stopRequested{false};
    std::atomic<bool> m_gpuShareFailed{false};
    std::mutex m_ctrlMutex;
    std::condition_variable m_ctrlCv;
    bool m_seekToStartRequested{false};
    std::thread m_worker;
};

class VideoWallpaperNode : public QSGSimpleTextureNode {
public:
    VideoWallpaperNode() {
        setOwnsTexture(false);
        setFiltering(QSGTexture::Linear);
    }

    ~VideoWallpaperNode() override {
        // ownsTexture() is false, so ~QSGSimpleTextureNode() will NOT touch texture().
        // Never call setTexture(nullptr) because QSGSimpleTextureNode::setTexture dereferences texture->textureSize().
        clearGpuCache();
        delete cpuTex;
        cpuTex = nullptr;
    }

    void clearGpuCache() {
        if (activeTex != cpuTex) {
            activeTex = nullptr;
        }
        for (int i = 0; i < kGpuPoolSlots; ++i) {
            delete qsgTex[i];
            qsgTex[i] = nullptr;
            if (qtTex[i]) {
                qtTex[i]->Release();
                qtTex[i] = nullptr;
            }
        }
        poolKeepAlive.reset();
        poolId = 0;
        gpuDev = nullptr;
    }

    void bindTexture(QSGTexture* tex) {
        activeTex = tex;
        setTexture(tex);
        markDirty(QSGNode::DirtyMaterial);
    }

    uint64_t poolId = 0;
    ID3D11Device* gpuDev = nullptr; // non-owning pointer to detect QRhi device changes
    std::shared_ptr<void> poolKeepAlive;
    ID3D11Texture2D* qtTex[kGpuPoolSlots] = {};
    QSGPlainTexture* qsgTex[kGpuPoolSlots] = {};
    QSGPlainTexture* cpuTex = nullptr;
    QSGTexture* activeTex = nullptr;
};

std::mutex g_hubMutex;
QHash<QString, std::shared_ptr<SharedVideoStream>> g_streams;

void reportGpuShareFailureForPath(const QString& path) {
    std::shared_ptr<SharedVideoStream> stream;
    {
        std::lock_guard<std::mutex> lock(g_hubMutex);
        stream = g_streams.value(path);
    }
    if (stream) {
        stream->markGpuShareFailed();
    }
}

} // namespace

VideoWallpaperItem::VideoWallpaperItem(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setSmooth(true);
    m_itemVisible.store(isVisible(), std::memory_order_release);
}

VideoWallpaperItem::~VideoWallpaperItem() {
    detachFromSharedStream();
}

QString VideoWallpaperItem::normalizeMediaPath(const QString& raw) {
    QString s = raw.trimmed();
    if (s.isEmpty()) {
        return QString();
    }
    if (s.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive)) {
        const QUrl u(s);
        const QString local = u.toLocalFile();
        if (!local.isEmpty()) {
            s = local;
        } else {
            s = s.mid(7);
        }
    }
    s.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (s.size() >= 3 && s.at(0) == QLatin1Char('/') && s.at(1).isLetter() && s.at(2) == QLatin1Char(':')) {
        s = s.mid(1);
    }
    return QDir::cleanPath(s);
}

void VideoWallpaperItem::attachToSharedStream() {
    if (m_path.isEmpty()) {
        return;
    }
    std::shared_ptr<SharedVideoStream> stream;
    {
        std::lock_guard<std::mutex> lock(g_hubMutex);
        auto it = g_streams.find(m_path);
        if (it == g_streams.end()) {
            stream = std::make_shared<SharedVideoStream>(m_path);
            g_streams.insert(m_path, stream);
        } else {
            stream = it.value();
        }
    }
    if (stream) {
        stream->addSubscriber(this);
    }
}

void VideoWallpaperItem::detachFromSharedStream() {
    if (m_path.isEmpty()) {
        return;
    }
    std::shared_ptr<SharedVideoStream> toStop;
    {
        std::lock_guard<std::mutex> lock(g_hubMutex);
        auto it = g_streams.find(m_path);
        if (it != g_streams.end()) {
            std::shared_ptr<SharedVideoStream> stream = it.value();
            if (stream->removeSubscriber(this)) {
                g_streams.erase(it);
                toStop = std::move(stream);
            }
        }
    }
    if (toStop) {
        toStop->stop();
    }
}

void VideoWallpaperItem::notifyStreamControlChanged() {
    if (m_path.isEmpty()) {
        return;
    }
    std::shared_ptr<SharedVideoStream> stream;
    {
        std::lock_guard<std::mutex> lock(g_hubMutex);
        stream = g_streams.value(m_path);
    }
    if (stream) {
        const bool restart = !m_paused.load(std::memory_order_acquire) &&
                             m_restartOnPause.load(std::memory_order_acquire);
        stream->notifyControlChanged(restart);
    }
}

void VideoWallpaperItem::setPath(const QString& path) {
    const QString norm = normalizeMediaPath(path);
    if (m_path == norm) {
        return;
    }
    detachFromSharedStream();
    m_path = norm;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingFrame = QImage();
        m_pendingGpuFrame = SharedGpuFrame{};
        m_hasNewFrame = false;
        m_hasNewGpuFrame = false;
        m_clearTexture = true;
    }
    Q_EMIT pathChanged();
    update();
    if (!m_path.isEmpty()) {
        attachToSharedStream();
    }
}

void VideoWallpaperItem::setPaused(bool paused) {
    const bool prev = m_paused.exchange(paused, std::memory_order_acq_rel);
    if (prev == paused) {
        return;
    }
    notifyStreamControlChanged();
    Q_EMIT pausedChanged();
}

void VideoWallpaperItem::setRestartOnPause(bool restart) {
    if (m_restartOnPause.exchange(restart, std::memory_order_acq_rel) != restart) {
        Q_EMIT restartOnPauseChanged();
    }
}

void VideoWallpaperItem::setMaxWidth(int w) {
    const int clamped = std::clamp(w, 320, 7680);
    if (m_maxWidth.exchange(clamped, std::memory_order_acq_rel) != clamped) {
        notifyStreamControlChanged();
        Q_EMIT maxWidthChanged();
    }
}

void VideoWallpaperItem::setMaxHeight(int h) {
    const int clamped = std::clamp(h, 180, 4320);
    if (m_maxHeight.exchange(clamped, std::memory_order_acq_rel) != clamped) {
        notifyStreamControlChanged();
        Q_EMIT maxHeightChanged();
    }
}

void VideoWallpaperItem::setTargetFps(int fps) {
    const int clamped = std::clamp(fps, 15, 200);
    if (m_targetFps.exchange(clamped, std::memory_order_acq_rel) != clamped) {
        notifyStreamControlChanged();
        Q_EMIT targetFpsChanged();
    }
}

void VideoWallpaperItem::setSmoothScaling(bool smooth) {
    if (m_smoothScaling.exchange(smooth, std::memory_order_acq_rel) != smooth) {
        notifyStreamControlChanged();
        Q_EMIT smoothScalingChanged();
    }
}

void VideoWallpaperItem::deliverSharedFrame(const QImage& frame) {
    if (m_paused.load(std::memory_order_acquire) || !m_itemVisible.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingFrame = frame; // implicit QImage cow ref, 0 pixel copy
        m_hasNewFrame = true;
        m_pendingGpuFrame = SharedGpuFrame{};
        m_hasNewGpuFrame = false;
        m_clearTexture = false;
    }
    if (!m_updateScheduled.exchange(true, std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(this, &VideoWallpaperItem::onFrameAvailable, Qt::QueuedConnection);
    }
}

void VideoWallpaperItem::deliverSharedGpuFrame(const SharedGpuFrame& gpuFrame) {
    if (m_paused.load(std::memory_order_acquire) || !m_itemVisible.load(std::memory_order_acquire)) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingGpuFrame = gpuFrame;
        m_hasNewGpuFrame = true;
        m_pendingFrame = QImage();
        m_hasNewFrame = false;
        m_clearTexture = false;
    }
    if (!m_updateScheduled.exchange(true, std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(this, &VideoWallpaperItem::onFrameAvailable, Qt::QueuedConnection);
    }
}

void VideoWallpaperItem::notifyStreamPlaying(bool playing) {
    setPlayingState(playing);
}

void VideoWallpaperItem::notifyStreamFailed() {
    QMetaObject::invokeMethod(this, [this]() { Q_EMIT failed(); }, Qt::QueuedConnection);
}

void VideoWallpaperItem::setPlayingState(bool playing) {
    if (m_playing.exchange(playing, std::memory_order_acq_rel) != playing) {
        QMetaObject::invokeMethod(this, [this]() {
            Q_EMIT playingChanged();
        }, Qt::QueuedConnection);
    }
}

void VideoWallpaperItem::itemChange(ItemChange change, const ItemChangeData& value) {
    QQuickItem::itemChange(change, value);
    if (change == ItemVisibleHasChanged) {
        m_itemVisible.store(value.boolValue, std::memory_order_release);
        notifyStreamControlChanged();
    }
}

void VideoWallpaperItem::releaseResources() {
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        m_pendingFrame = QImage();
        m_pendingGpuFrame = SharedGpuFrame{};
        m_hasNewFrame = false;
        m_hasNewGpuFrame = false;
        m_clearTexture = true;
    }
    QQuickItem::releaseResources();
}

void VideoWallpaperItem::onFrameAvailable() {
    m_updateScheduled.store(false, std::memory_order_release);
    if (isVisible() && window()) {
        update();
    }
}

QSGNode* VideoWallpaperItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    QImage frameToUpload;
    SharedGpuFrame gpuFrameToBind;
    bool clearTex = false;
    {
        std::lock_guard<std::mutex> lock(m_frameMutex);
        if (m_clearTexture) {
            clearTex = true;
            m_clearTexture = false;
            m_hasNewFrame = false;
            m_hasNewGpuFrame = false;
            m_pendingFrame = QImage();
            m_pendingGpuFrame = SharedGpuFrame{};
        } else if (m_hasNewGpuFrame && m_pendingGpuFrame.sharedHandle) {
            gpuFrameToBind = std::move(m_pendingGpuFrame);
            m_hasNewGpuFrame = false;
        } else if (m_hasNewFrame && !m_pendingFrame.isNull()) {
            frameToUpload = std::move(m_pendingFrame);
            m_hasNewFrame = false;
        }
    }

    if (clearTex) {
        delete oldNode;
        return nullptr;
    }

    auto* node = static_cast<VideoWallpaperNode*>(oldNode);

    if (gpuFrameToBind.sharedHandle && window()) {
        bool boundGpu = false;
        QRhi* rhi = window()->rhi();
        if (rhi && rhi->backend() == QRhi::D3D11 &&
            gpuFrameToBind.slot >= 0 && gpuFrameToBind.slot < kGpuPoolSlots) {
            const auto* nh = static_cast<const QRhiD3D11NativeHandles*>(rhi->nativeHandles());
            auto* qtDev = nh ? static_cast<ID3D11Device*>(nh->dev) : nullptr;
            if (qtDev) {
                if (!node) {
                    node = new VideoWallpaperNode();
                }
                if (node->poolId != gpuFrameToBind.poolId || node->gpuDev != qtDev) {
                    node->clearGpuCache();
                    node->poolId = gpuFrameToBind.poolId;
                    node->gpuDev = qtDev;
                    node->poolKeepAlive = gpuFrameToBind.poolKeepAlive;
                }
                const int slot = gpuFrameToBind.slot;
                if (!node->qsgTex[slot]) {
                    ID3D11Texture2D* opened = nullptr;
                    const HRESULT hrOpen = qtDev->OpenSharedResource(
                        static_cast<HANDLE>(gpuFrameToBind.sharedHandle),
                        __uuidof(ID3D11Texture2D),
                        reinterpret_cast<void**>(&opened));
                    if (SUCCEEDED(hrOpen) && opened) {
                        const QSize sz(gpuFrameToBind.width, gpuFrameToBind.height);
                        QRhiTexture* rhiTex = rhi->newTexture(QRhiTexture::BGRA8, sz, 1, {});
                        if (rhiTex && rhiTex->createFrom(QRhiTexture::NativeTexture{
                                reinterpret_cast<quint64>(opened), 0 })) {
                            auto* plain = new QSGPlainTexture();
                            plain->setOwnsTexture(true);
                            plain->setTexture(rhiTex);
                            plain->setTextureSize(sz);
                            plain->setHasAlphaChannel(false);
                            node->qtTex[slot] = opened;
                            node->qsgTex[slot] = plain;
                        } else {
                            delete rhiTex;
                            opened->Release();
                        }
                    } else {
                        qWarning("[VideoWallpaper] OpenSharedResource failed (hr=0x%08lx), falling back to CPU upload",
                                 static_cast<unsigned long>(hrOpen));
                    }
                }
                if (node->qsgTex[slot]) {
                    node->bindTexture(node->qsgTex[slot]);
                    if (node->cpuTex) {
                        delete node->cpuTex;
                        node->cpuTex = nullptr;
                    }
                    boundGpu = true;
                }
            }
        }
        if (!boundGpu) {
            reportGpuShareFailureForPath(m_path);
        }
    } else if (!frameToUpload.isNull() && window()) {
        if (!node) {
            node = new VideoWallpaperNode();
        }
        if (!node->cpuTex) {
            node->cpuTex = new QSGPlainTexture();
            node->cpuTex->setOwnsTexture(true);
        }
        node->cpuTex->setImage(frameToUpload);
        node->cpuTex->setHasAlphaChannel(false);
        node->bindTexture(node->cpuTex);
        node->clearGpuCache();
    }

    if (!node || !node->activeTex) {
        delete node;
        return nullptr;
    }

    const QRectF bounds = boundingRect();
    node->setRect(bounds);

    const QSize texSize = node->texture()->textureSize();
    if (texSize.width() > 0 && texSize.height() > 0 && bounds.width() > 0 && bounds.height() > 0) {
        const qreal itemAspect = bounds.width() / bounds.height();
        const qreal texAspect = static_cast<qreal>(texSize.width()) / static_cast<qreal>(texSize.height());
        QRectF srcRect(0, 0, texSize.width(), texSize.height());
        if (texAspect > itemAspect) {
            const qreal cropW = texSize.height() * itemAspect;
            srcRect.setX((texSize.width() - cropW) * 0.5);
            srcRect.setWidth(cropW);
        } else if (texAspect < itemAspect) {
            const qreal cropH = texSize.width() / itemAspect;
            srcRect.setY((texSize.height() - cropH) * 0.5);
            srcRect.setHeight(cropH);
        }
        node->setSourceRect(srcRect);
    }

    return node;
}

bool VideoWallpaperItem::extractFrameToJpeg(const QString& videoPath, const QString& outputPath,
                                            int maxWidth, int maxHeight, int quality) {
    const QString norm = normalizeMediaPath(videoPath);
    if (norm.isEmpty() || !QFileInfo::exists(norm)) {
        return false;
    }

    ComMfScope comMf;
    if (!comMf.mfInit) {
        return false;
    }

    VideoFormatInfo fmt;
    IMFSourceReader* reader = openVideoReader(norm, nullptr, nullptr, fmt);
    if (!reader) {
        return false;
    }

    auto readFirstValidFrame = [&](QImage& outFrame) -> bool {
        QImage scratch;
        for (int tries = 0; tries < 16; ++tries) {
            DWORD streamIndex = 0;
            DWORD flags = 0;
            LONGLONG ts = 0;
            IMFSample* sample = nullptr;
            const HRESULT hr = reader->ReadSample(
                MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &flags, &ts, &sample);
            if (FAILED(hr)) {
                if (sample) sample->Release();
                return false;
            }
            if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
                queryVideoFormat(reader, fmt);
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                if (sample) sample->Release();
                return false;
            }
            if (sample) {
                const bool ok = decodeSampleToImage(
                    sample, fmt, maxWidth, maxHeight, Qt::SmoothTransformation, scratch, outFrame);
                sample->Release();
                if (ok) {
                    return true;
                }
            }
        }
        return false;
    };

    QImage frame;
    bool got = false;
    if (seekReader(reader, 10000000LL)) {
        got = readFirstValidFrame(frame);
    }
    if (!got) {
        seekReader(reader, 0);
        got = readFirstValidFrame(frame);
    }

    reader->Release();
    if (!got || frame.isNull()) {
        return false;
    }

    QDir().mkpath(QFileInfo(outputPath).dir().absolutePath());
    const QString tmpPath = outputPath + QStringLiteral(".tmp.jpg");
    if (!frame.save(tmpPath, "JPG", quality)) {
        QFile::remove(tmpPath);
        return false;
    }
    QFile::remove(outputPath);
    if (!QFile::rename(tmpPath, outputPath)) {
        QFile::remove(tmpPath);
        return false;
    }
    return true;
}

} // namespace SekaiPop::Services
