#include <winsock2.h>
#include "QuickSource.h"
#include "OsdSource.h"
#include "SettingsService.h"
#include "../win32/ScreenshotOverlay.h"
#include "../win32/Win32Utils.h"

#include <windows.h>
#include <bluetoothapis.h>
#include <endpointvolume.h>
#include <highlevelmonitorconfigurationapi.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#include <mmdeviceapi.h>
#include <netlistmgr.h>
#include <oleauto.h>
#include <physicalmonitorenumerationapi.h>
#include <powrprof.h>
#include <wbemidl.h>
#include <wlanapi.h>

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QMetaObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

namespace SekaiPop::Services {

namespace {

// ============================================================================
// Pure WinRT ABI bridge for Windows.Devices.Radios.Radio (combase.dll)
// Toggles Wi-Fi (RadioKind=1) and Bluetooth (RadioKind=3) radios natively
// with zero UAC elevation and zero Settings app popups.
// ============================================================================

using WinRtHString = void*;

struct WinRtInspectable : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetIids(ULONG* iidCount, IID** iids) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetRuntimeClassName(WinRtHString* className) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetTrustLevel(int* trustLevel) = 0;
};

struct WinRtAsyncInfo : WinRtInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32* id) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int* status) = 0; // 0=Started, 1=Completed, 2=Canceled, 3=Error
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT* errorCode) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

template <typename TResult>
struct WinRtAsyncOperation : WinRtInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(void* handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(void** handler) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(TResult* results) = 0;
};

struct WinRtRadio;

struct WinRtRadioVectorView : WinRtInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetAt(UINT32 index, WinRtRadio** item) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(UINT32* size) = 0;
    virtual HRESULT STDMETHODCALLTYPE IndexOf(WinRtRadio* value, UINT32* index, unsigned char* found) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMany(UINT32 startIndex, UINT32 capacity, WinRtRadio** value, UINT32* actual) = 0;
};

struct WinRtRadio : WinRtInspectable {
    virtual HRESULT STDMETHODCALLTYPE SetStateAsync(int value, WinRtAsyncOperation<int>** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE add_StateChanged(void* handler, qint64* token) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_StateChanged(qint64 token) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_State(int* value) = 0; // 0=Unknown, 1=On, 2=Off, 3=Disabled
    virtual HRESULT STDMETHODCALLTYPE get_Name(WinRtHString* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Kind(int* value) = 0;  // 1=WiFi, 3=Bluetooth
};

struct WinRtRadioStatics : WinRtInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetRadiosAsync(WinRtAsyncOperation<WinRtRadioVectorView*>** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceSelector(WinRtHString* value) = 0;
    virtual HRESULT STDMETHODCALLTYPE FromIdAsync(WinRtHString id, WinRtAsyncOperation<WinRtRadio*>** op) = 0;
    virtual HRESULT STDMETHODCALLTYPE RequestAccessAsync(WinRtAsyncOperation<int>** op) = 0;
};

constexpr GUID kIidIAsyncInfo = { 0x00000036, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
constexpr GUID kIidIRadioStatics = { 0x5fb6a12e, 0x67cb, 0x46ae, { 0xaa, 0xe9, 0x65, 0x91, 0x9f, 0x86, 0xef, 0xf4 } };

struct CombaseApi {
    using FnRoInitialize = HRESULT(WINAPI*)(int);
    using FnRoUninitialize = void(WINAPI*)();
    using FnRoGetActivationFactory = HRESULT(WINAPI*)(WinRtHString, REFIID, void**);
    using FnWindowsCreateString = HRESULT(WINAPI*)(LPCWSTR, UINT32, WinRtHString*);
    using FnWindowsDeleteString = HRESULT(WINAPI*)(WinRtHString);

    FnRoInitialize roInit = nullptr;
    FnRoUninitialize roUninit = nullptr;
    FnRoGetActivationFactory roGetFactory = nullptr;
    FnWindowsCreateString createStr = nullptr;
    FnWindowsDeleteString deleteStr = nullptr;
    bool ok = false;

    static const CombaseApi& get() {
        static CombaseApi api = []() {
            CombaseApi a;
            HMODULE mod = LoadLibraryW(L"combase.dll");
            if (mod) {
                a.roInit = reinterpret_cast<FnRoInitialize>(GetProcAddress(mod, "RoInitialize"));
                a.roUninit = reinterpret_cast<FnRoUninitialize>(GetProcAddress(mod, "RoUninitialize"));
                a.roGetFactory = reinterpret_cast<FnRoGetActivationFactory>(GetProcAddress(mod, "RoGetActivationFactory"));
                a.createStr = reinterpret_cast<FnWindowsCreateString>(GetProcAddress(mod, "WindowsCreateString"));
                a.deleteStr = reinterpret_cast<FnWindowsDeleteString>(GetProcAddress(mod, "WindowsDeleteString"));
                a.ok = (a.roGetFactory && a.createStr && a.deleteStr);
            }
            return a;
        }();
        return api;
    }
};

template <typename TOp>
bool waitWinRtAsync(TOp* op, int timeoutMs = 2000, const std::atomic<bool>* stopFlag = nullptr) {
    if (!op) return false;
    WinRtAsyncInfo* info = nullptr;
    if (FAILED(op->QueryInterface(kIidIAsyncInfo, reinterpret_cast<void**>(&info))) || !info) {
        return false;
    }
    int status = 0;
    int waited = 0;
    while (waited < timeoutMs) {
        if (stopFlag && stopFlag->load()) {
            info->Cancel();
            info->Release();
            return false;
        }
        if (FAILED(info->get_Status(&status)) || status != 0) {
            break;
        }
        Sleep(10);
        waited += 10;
    }
    info->Release();
    return status == 1; // AsyncStatus::Completed
}

WinRtRadioStatics* getWinRtRadioStatics() {
    const CombaseApi& api = CombaseApi::get();
    if (!api.ok) return nullptr;
    static constexpr wchar_t kClassName[] = L"Windows.Devices.Radios.Radio";
    WinRtHString hClass = nullptr;
    if (FAILED(api.createStr(kClassName, static_cast<UINT32>(std::size(kClassName) - 1), &hClass)) || !hClass) {
        return nullptr;
    }
    WinRtRadioStatics* statics = nullptr;
    const HRESULT hr = api.roGetFactory(hClass, kIidIRadioStatics, reinterpret_cast<void**>(&statics));
    api.deleteStr(hClass);
    return SUCCEEDED(hr) ? statics : nullptr;
}

bool queryWinRtRadios(bool& wifiFound, bool& wifiOn, bool& btFound, bool& btOn, const std::atomic<bool>* stopFlag) {
    wifiFound = false;
    wifiOn = false;
    btFound = false;
    btOn = false;
    WinRtRadioStatics* statics = getWinRtRadioStatics();
    if (!statics) return false;

    WinRtAsyncOperation<WinRtRadioVectorView*>* op = nullptr;
    if (SUCCEEDED(statics->GetRadiosAsync(&op)) && op) {
        if (waitWinRtAsync(op, 1200, stopFlag)) {
            WinRtRadioVectorView* view = nullptr;
            if (SUCCEEDED(op->GetResults(&view)) && view) {
                UINT32 count = 0;
                if (SUCCEEDED(view->get_Size(&count))) {
                    for (UINT32 i = 0; i < count; ++i) {
                        WinRtRadio* radio = nullptr;
                        if (SUCCEEDED(view->GetAt(i, &radio)) && radio) {
                            int kind = 0;
                            int state = 0;
                            if (SUCCEEDED(radio->get_Kind(&kind))) {
                                const bool isOn = SUCCEEDED(radio->get_State(&state)) && state == 1;
                                if (kind == 1) { // WiFi
                                    wifiFound = true;
                                    if (isOn) wifiOn = true;
                                } else if (kind == 3) { // Bluetooth
                                    btFound = true;
                                    if (isOn) btOn = true;
                                }
                            }
                            radio->Release();
                        }
                    }
                }
                view->Release();
            }
        }
        op->Release();
    }
    statics->Release();
    return wifiFound || btFound;
}

bool setWinRtRadioState(int targetKind, bool turnOn) {
    const CombaseApi& api = CombaseApi::get();
    if (!api.ok) return false;
    const bool didRoInit = api.roInit && SUCCEEDED(api.roInit(1)); // RO_INIT_MULTITHREADED
    WinRtRadioStatics* statics = getWinRtRadioStatics();
    if (!statics) {
        if (didRoInit && api.roUninit) api.roUninit();
        return false;
    }

    // Request access first (auto-granted for desktop Win32 apps)
    WinRtAsyncOperation<int>* accessOp = nullptr;
    if (SUCCEEDED(statics->RequestAccessAsync(&accessOp)) && accessOp) {
        waitWinRtAsync(accessOp, 1500);
        accessOp->Release();
    }

    bool changedAny = false;
    WinRtAsyncOperation<WinRtRadioVectorView*>* listOp = nullptr;
    if (SUCCEEDED(statics->GetRadiosAsync(&listOp)) && listOp) {
        if (waitWinRtAsync(listOp, 2000)) {
            WinRtRadioVectorView* view = nullptr;
            if (SUCCEEDED(listOp->GetResults(&view)) && view) {
                UINT32 count = 0;
                if (SUCCEEDED(view->get_Size(&count))) {
                    for (UINT32 i = 0; i < count; ++i) {
                        WinRtRadio* radio = nullptr;
                        if (SUCCEEDED(view->GetAt(i, &radio)) && radio) {
                            int kind = 0;
                            if (SUCCEEDED(radio->get_Kind(&kind)) && kind == targetKind) {
                                WinRtAsyncOperation<int>* setOp = nullptr;
                                // RadioState: 1 = On, 2 = Off
                                if (SUCCEEDED(radio->SetStateAsync(turnOn ? 1 : 2, &setOp)) && setOp) {
                                    if (waitWinRtAsync(setOp, 3000)) {
                                        int accessRes = 0;
                                        setOp->GetResults(&accessRes);
                                        qInfo("[QuickSource] WinRT Radio kind=%d setOn=%d access=%d", targetKind, turnOn ? 1 : 0, accessRes);
                                        changedAny = (accessRes == 1);
                                    }
                                    setOp->Release();
                                }
                            }
                            radio->Release();
                        }
                    }
                }
                view->Release();
            }
        }
        listOp->Release();
    }
    statics->Release();
    if (didRoInit && api.roUninit) {
        api.roUninit();
    }
    return changedAny;
}

constexpr GUID kClsidMMDeviceEnumerator = { 0xbcde0395, 0xe52f, 0x467c, { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e } };
constexpr GUID kIidIMMDeviceEnumerator = { 0xa95664d2, 0x9614, 0x4f35, { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 } };
constexpr GUID kIidIAudioEndpointVolume = { 0x5cdf2c82, 0x841e, 0x4546, { 0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a } };
constexpr GUID kClsidWbemLocator = { 0x4590f811, 0x1d3a, 0x11d0, { 0x89, 0x1f, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 } };
constexpr GUID kIidIWbemLocator = { 0xdc12a687, 0x737f, 0x11cf, { 0x88, 0x4d, 0x00, 0xaa, 0x00, 0x4b, 0x2e, 0x24 } };
constexpr GUID kClsidNetworkListManager = { 0xdcb00c01, 0x570f, 0x4a9b, { 0x8d, 0x69, 0x19, 0x9f, 0xdb, 0xa5, 0x72, 0x3b } };
constexpr GUID kIidINetworkListManager = { 0xdcb00000, 0x570f, 0x4a9b, { 0x8d, 0x69, 0x19, 0x9f, 0xdb, 0xa5, 0x72, 0x3b } };

// Queries the active Windows Network List Manager (NLM) profile name for a specific adapter GUID.
// On Windows 11 24H2+, WlanQueryInterface(wlan_intf_opcode_current_connection) returns ERROR_ACCESS_DENIED (5)
// whenever Windows Location Services is disabled, whereas INetworkListManager reliably returns the connected
// Wi-Fi network name with zero location permission required.
QString queryConnectedWifiNameFromNlm(const GUID* targetAdapterGuid) {
    if (!targetAdapterGuid) {
        return {};
    }
    INetworkListManager* nlm = nullptr;
    const HRESULT hr = CoCreateInstance(
        kClsidNetworkListManager, nullptr, CLSCTX_ALL,
        kIidINetworkListManager, reinterpret_cast<void**>(&nlm));
    if (FAILED(hr) || !nlm) {
        return {};
    }

    QString foundName;
    IEnumNetworkConnections* enumConns = nullptr;
    if (SUCCEEDED(nlm->GetNetworkConnections(&enumConns)) && enumConns) {
        INetworkConnection* conn = nullptr;
        ULONG fetched = 0;
        while (enumConns->Next(1, &conn, &fetched) == S_OK && fetched == 1 && conn) {
            VARIANT_BOOL connected = VARIANT_FALSE;
            GUID adapterId = {};
            if (SUCCEEDED(conn->get_IsConnected(&connected)) && connected != VARIANT_FALSE &&
                SUCCEEDED(conn->GetAdapterId(&adapterId)) && IsEqualGUID(adapterId, *targetAdapterGuid)) {
                INetwork* net = nullptr;
                if (SUCCEEDED(conn->GetNetwork(&net)) && net) {
                    BSTR bstrName = nullptr;
                    if (SUCCEEDED(net->GetName(&bstrName)) && bstrName) {
                        foundName = QString::fromWCharArray(bstrName).trimmed();
                        SysFreeString(bstrName);
                    }
                    net->Release();
                }
                conn->Release();
                break;
            }
            conn->Release();
        }
        enumConns->Release();
    }
    nlm->Release();
    return foundName;
}

bool getEndpointVolume(EDataFlow flow, IAudioEndpointVolume** outVol) {
    if (!outVol) {
        return false;
    }
    *outVol = nullptr;
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(CoCreateInstance(kClsidMMDeviceEnumerator, nullptr, CLSCTX_ALL, kIidIMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator))) || !enumerator) {
        return false;
    }
    IMMDevice* dev = nullptr;
    bool ok = false;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &dev)) && dev) {
        if (SUCCEEDED(dev->Activate(kIidIAudioEndpointVolume, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(outVol))) && *outVol) {
            ok = true;
        }
        dev->Release();
    }
    enumerator->Release();
    return ok;
}

bool queryWmiBrightness(int& outPct) {
    IWbemLocator* locator = nullptr;
    if (FAILED(CoCreateInstance(kClsidWbemLocator, nullptr, CLSCTX_INPROC_SERVER, kIidIWbemLocator, reinterpret_cast<void**>(&locator))) || !locator) {
        return false;
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    if (FAILED(hr) || !svc) {
        locator->Release();
        return false;
    }
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR queryLang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT CurrentBrightness FROM WmiMonitorBrightness");
    hr = svc->ExecQuery(queryLang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    SysFreeString(queryLang);
    SysFreeString(query);

    bool ok = false;
    if (SUCCEEDED(hr) && enumerator) {
        IWbemClassObject* obj = nullptr;
        ULONG ret = 0;
        if (enumerator->Next(1000, 1, &obj, &ret) == S_OK && ret == 1 && obj) {
            VARIANT vt = {};
            if (SUCCEEDED(obj->Get(L"CurrentBrightness", 0, &vt, nullptr, nullptr))) {
                if (vt.vt == VT_UI1) {
                    outPct = static_cast<int>(vt.bVal);
                    ok = true;
                } else if (vt.vt == VT_I4 || vt.vt == VT_UI4) {
                    outPct = static_cast<int>(vt.lVal);
                    ok = true;
                }
                VariantClear(&vt);
            }
            obj->Release();
        }
        enumerator->Release();
    }
    svc->Release();
    locator->Release();
    return ok;
}

bool setWmiBrightness(int pct) {
    IWbemLocator* locator = nullptr;
    if (FAILED(CoCreateInstance(kClsidWbemLocator, nullptr, CLSCTX_INPROC_SERVER, kIidIWbemLocator, reinterpret_cast<void**>(&locator))) || !locator) {
        return false;
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = locator->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    if (FAILED(hr) || !svc) {
        locator->Release();
        return false;
    }
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

    IEnumWbemClassObject* enumerator = nullptr;
    BSTR queryLang = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT __RELPATH FROM WmiMonitorBrightnessMethods");
    hr = svc->ExecQuery(queryLang, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &enumerator);
    SysFreeString(queryLang);
    SysFreeString(query);

    bool ok = false;
    if (SUCCEEDED(hr) && enumerator) {
        IWbemClassObject* clsObj = nullptr;
        BSTR clsName = SysAllocString(L"WmiMonitorBrightnessMethods");
        IWbemClassObject* inSig = nullptr;
        if (SUCCEEDED(svc->GetObject(clsName, 0, nullptr, &clsObj, nullptr)) && clsObj) {
            clsObj->GetMethod(L"WmiSetBrightness", 0, &inSig, nullptr);
            clsObj->Release();
        }
        SysFreeString(clsName);

        IWbemClassObject* inst = nullptr;
        ULONG ret = 0;
        while (enumerator->Next(1000, 1, &inst, &ret) == S_OK && ret == 1 && inst) {
            VARIANT pathVt = {};
            if (SUCCEEDED(inst->Get(L"__RELPATH", 0, &pathVt, nullptr, nullptr)) && pathVt.vt == VT_BSTR && inSig) {
                IWbemClassObject* inParams = nullptr;
                if (SUCCEEDED(inSig->SpawnInstance(0, &inParams)) && inParams) {
                    VARIANT vTimeout = {};
                    vTimeout.vt = VT_I4;
                    vTimeout.lVal = 0;
                    inParams->Put(L"Timeout", 0, &vTimeout, 0);

                    VARIANT vBri = {};
                    vBri.vt = VT_UI1;
                    vBri.bVal = static_cast<BYTE>(qBound(0, pct, 100));
                    inParams->Put(L"Brightness", 0, &vBri, 0);

                    BSTR methodName = SysAllocString(L"WmiSetBrightness");
                    if (SUCCEEDED(svc->ExecMethod(pathVt.bstrVal, methodName, 0, nullptr, inParams, nullptr, nullptr))) {
                        ok = true;
                    }
                    SysFreeString(methodName);
                    inParams->Release();
                }
            }
            VariantClear(&pathVt);
            inst->Release();
        }
        if (inSig) inSig->Release();
        enumerator->Release();
    }
    svc->Release();
    locator->Release();
    return ok;
}

struct GammaApplyPair {
    const WORD* primary;
    const WORD* fallback;
};

BOOL CALLBACK enumGammaMonitorsProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam) {
    auto* pair = reinterpret_cast<const GammaApplyPair*>(lParam);
    if (!pair || !pair->primary) {
        return TRUE;
    }
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(hmon, &mi)) {
        HDC dc = CreateDCW(L"DISPLAY", mi.szDevice, nullptr, nullptr);
        if (dc) {
            if (!SetDeviceGammaRamp(dc, const_cast<WORD*>(pair->primary)) && pair->fallback) {
                SetDeviceGammaRamp(dc, const_cast<WORD*>(pair->fallback));
            }
            DeleteDC(dc);
        }
    }
    return TRUE;
}

BOOL CALLBACK queryDdcBrightnessProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam) {
    auto* res = reinterpret_cast<int*>(lParam);
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) || count == 0) {
        return TRUE;
    }
    std::vector<PHYSICAL_MONITOR> mons(count);
    if (GetPhysicalMonitorsFromHMONITOR(hmon, count, mons.data())) {
        for (DWORD i = 0; i < count; ++i) {
            DWORD minB = 0, curB = 0, maxB = 0;
            if (GetMonitorBrightness(mons[i].hPhysicalMonitor, &minB, &curB, &maxB) && maxB > minB) {
                *res = qRound(static_cast<double>(curB - minB) * 100.0 / static_cast<double>(maxB - minB));
                DestroyPhysicalMonitors(count, mons.data());
                return FALSE;
            }
        }
        DestroyPhysicalMonitors(count, mons.data());
    }
    return TRUE;
}

BOOL CALLBACK setDdcBrightnessProc(HMONITOR hmon, HDC, LPRECT, LPARAM lParam) {
    const int pct = static_cast<int>(lParam);
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hmon, &count) || count == 0) {
        return TRUE;
    }
    std::vector<PHYSICAL_MONITOR> mons(count);
    if (GetPhysicalMonitorsFromHMONITOR(hmon, count, mons.data())) {
        for (DWORD i = 0; i < count; ++i) {
            DWORD minB = 0, curB = 0, maxB = 0;
            if (GetMonitorBrightness(mons[i].hPhysicalMonitor, &minB, &curB, &maxB) && maxB > minB) {
                const DWORD target = minB + static_cast<DWORD>(qRound((maxB - minB) * qBound(0, pct, 100) / 100.0));
                SetMonitorBrightness(mons[i].hPhysicalMonitor, target);
            }
        }
        DestroyPhysicalMonitors(count, mons.data());
    }
    return TRUE;
}

} // namespace

QuickSource::QuickSource(SettingsService* settings, OsdSource* osd, QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_osd(osd) {
    wchar_t userBuf[256] = {};
    DWORD userLen = static_cast<DWORD>(std::size(userBuf));
    if (GetUserNameW(userBuf, &userLen) && userLen > 1) {
        m_userName = QString::fromWCharArray(userBuf, static_cast<int>(userLen - 1));
    } else {
        m_userName = QStringLiteral("user");
    }

    wchar_t hostBuf[256] = {};
    DWORD hostLen = static_cast<DWORD>(std::size(hostBuf));
    if (GetComputerNameW(hostBuf, &hostLen) && hostLen > 0) {
        m_hostName = QString::fromWCharArray(hostBuf, static_cast<int>(hostLen));
    }

    if (m_settings) {
        m_nightLightOn = m_settings->flag(QStringLiteral("quick.nightLight"));
    }

    if (m_nightLightOn) {
        applyGammaState();
    }

    m_pollThread = std::thread([this]() { pollThreadLoop(); });
    m_brightnessThread = std::thread([this]() { brightnessThreadLoop(); });
}

QuickSource::~QuickSource() {
    m_stop.store(true);
    m_brightnessCv.notify_all();
    if (m_brightnessThread.joinable()) {
        m_brightnessThread.join();
    }
    if (m_pollThread.joinable()) {
        m_pollThread.join();
    }
    if (m_gammaModified) {
        m_nightLightOn = false;
        m_brightness = 100.0;
        m_gamma = 100.0;
        applyGammaState();
    }
}

QString QuickSource::networkKind() const {
    return (!m_ethernetName.isEmpty() && m_wifiName.isEmpty())
        ? QStringLiteral("ethernet")
        : QStringLiteral("wifi");
}

bool QuickSource::networkOn() const {
    return networkKind() == QStringLiteral("ethernet") ? true : m_wifiOn;
}

QString QuickSource::networkDetail() const {
    if (networkKind() == QStringLiteral("ethernet")) {
        return m_ethernetName;
    }
    if (!m_wifiOn) {
        return QStringLiteral("OFF");
    }
    return !m_wifiName.isEmpty() ? m_wifiName : QStringLiteral("Not connected");
}

void QuickSource::refresh() {
    m_wakeNow.store(true);
}

void QuickSource::pollThreadLoop() {
    const CombaseApi& cbApi = CombaseApi::get();
    const bool didRoInit = cbApi.roInit && SUCCEEDED(cbApi.roInit(1)); // RO_INIT_MULTITHREADED
    const bool didCoInit = !didRoInit && SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));

    struct PollSnapshot {
        bool speakerAvailable = false;
        bool speakerOn = true;
        qreal speakerVolume = 0.0;
        bool microphoneAvailable = false;
        bool microphoneOn = true;
        qreal microphoneVolume = 0.0;
        bool wifiOn = false;
        QString wifiName;
        QString ethernetName;
        bool bluetoothAvailable = false;
        bool bluetoothOn = false;
        bool bluetoothConnected = false;
        QString bluetoothDetail = QStringLiteral("OFF");
        bool hasBrightness = false;
        bool hardwareBrightness = false;
        qreal brightness = 100.0;
        qreal downloadBps = 0.0;
        qreal uploadBps = 0.0;
    };

    int tick = 0;
    bool queriedBrightness = false;
    bool lastBtAvail = false;
    bool lastBtOn = false;
    bool lastBtConn = false;
    QString lastBtDetail = QStringLiteral("OFF");
    quint64 prevIn = 0;
    quint64 prevOut = 0;
    qint64 prevMs = 0;

    while (!m_stop.load()) {
        const bool forcePoll = m_wakeNow.exchange(false);
        PollSnapshot snap;

        // 1. Audio (fast COM query on worker thread)
        IAudioEndpointVolume* spk = nullptr;
        if (getEndpointVolume(eRender, &spk)) {
            float level = 0.0f;
            BOOL muted = FALSE;
            spk->GetMasterVolumeLevelScalar(&level);
            spk->GetMute(&muted);
            spk->Release();
            snap.speakerAvailable = true;
            snap.speakerVolume = qRound(qBound(0.0f, level, 1.0f) * 100.0f);
            snap.speakerOn = (muted == FALSE);
        }
        IAudioEndpointVolume* mic = nullptr;
        if (getEndpointVolume(eCapture, &mic)) {
            float level = 0.0f;
            BOOL muted = FALSE;
            mic->GetMasterVolumeLevelScalar(&level);
            mic->GetMute(&muted);
            mic->Release();
            snap.microphoneAvailable = true;
            snap.microphoneVolume = qRound(qBound(0.0f, level, 1.0f) * 100.0f);
            snap.microphoneOn = (muted == FALSE);
        }

        if (m_stop.load()) break;

        // 2. Network (WinRT Wi-Fi & BT Radio state + WLAN + Ethernet)
        bool rtWifiFound = false;
        bool rtWifiOn = false;
        bool rtBtFound = false;
        bool rtBtOn = false;
        queryWinRtRadios(rtWifiFound, rtWifiOn, rtBtFound, rtBtOn, &m_stop);
        if (tick == 0) {
            qInfo("[QuickSource] WinRT radios: wifiFound=%d wifiOn=%d btFound=%d btOn=%d",
                  rtWifiFound ? 1 : 0, rtWifiOn ? 1 : 0, rtBtFound ? 1 : 0, rtBtOn ? 1 : 0);
        }
        const bool hasRtWifi = rtWifiFound;

        DWORD negotiatedVer = 0;
        HANDLE hWlan = nullptr;
        if (WlanOpenHandle(2, nullptr, &negotiatedVer, &hWlan) == ERROR_SUCCESS && hWlan) {
            PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
            if (WlanEnumInterfaces(hWlan, nullptr, &ifList) == ERROR_SUCCESS && ifList) {
                for (DWORD i = 0; i < ifList->dwNumberOfItems; ++i) {
                    const WLAN_INTERFACE_INFO& info = ifList->InterfaceInfo[i];
                    if (!hasRtWifi && info.isState != wlan_interface_state_not_ready) {
                        snap.wifiOn = true;
                    }
                    if (info.isState == wlan_interface_state_connected) {
                        snap.wifiOn = true;
                        DWORD dataSize = 0;
                        PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
                        if (WlanQueryInterface(hWlan, &info.InterfaceGuid, wlan_intf_opcode_current_connection,
                                               nullptr, &dataSize, reinterpret_cast<PVOID*>(&conn), nullptr) == ERROR_SUCCESS && conn) {
                            const DOT11_SSID& ssid = conn->wlanAssociationAttributes.dot11Ssid;
                            if (ssid.uSSIDLength > 0 && ssid.uSSIDLength <= DOT11_SSID_MAX_LENGTH) {
                                snap.wifiName = QString::fromUtf8(reinterpret_cast<const char*>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
                            }
                            WlanFreeMemory(conn);
                        }
                        if (snap.wifiName.isEmpty()) {
                            snap.wifiName = queryConnectedWifiNameFromNlm(&info.InterfaceGuid);
                        }
                        break;
                    }
                }
                WlanFreeMemory(ifList);
            }
            WlanCloseHandle(hWlan, nullptr);
        }
        if (hasRtWifi) {
            snap.wifiOn = rtWifiOn;
            if (!rtWifiOn) {
                snap.wifiName.clear();
            }
        }
        if (tick == 0) {
            qInfo("[QuickSource] Initial Wi-Fi state: wifiOn=%d wifiName='%s'",
                  snap.wifiOn ? 1 : 0, qPrintable(snap.wifiName));
        }
        ULONG bufSize = 16 * 1024;
        std::vector<BYTE> buf(bufSize);
        const ULONG gaaFlags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_GATEWAYS;
        ULONG res = GetAdaptersAddresses(AF_UNSPEC, gaaFlags,
                                         nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufSize);
        if (res == ERROR_BUFFER_OVERFLOW) {
            buf.resize(bufSize);
            res = GetAdaptersAddresses(AF_UNSPEC, gaaFlags,
                                       nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufSize);
        }
        if (res == NO_ERROR) {
            auto isVirtualNic = [](const QString& descLower, const QString& friendlyLower) -> bool {
                static const QStringList virtualHints = {
                    QStringLiteral("virtual"), QStringLiteral("vmware"), QStringLiteral("vbox"),
                    QStringLiteral("hyper-v"), QStringLiteral("spacedesk"), QStringLiteral("tap-"),
                    QStringLiteral("wintun"), QStringLiteral("wireguard"), QStringLiteral("tailscale"),
                    QStringLiteral("cloudflare"), QStringLiteral("warp"), QStringLiteral("wsl"),
                    QStringLiteral("npcap"), QStringLiteral("bluetooth"), QStringLiteral("loopback"),
                    QStringLiteral("km-test"), QStringLiteral("radmin"), QStringLiteral("zerotier"),
                    QStringLiteral("hamachi"), QStringLiteral("openvpn"), QStringLiteral("vethernet")
                };
                for (const QString& h : virtualHints) {
                    if (descLower.contains(h) || friendlyLower.contains(h)) {
                        return true;
                    }
                }
                return false;
            };
            for (auto* aa = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); aa != nullptr; aa = aa->Next) {
                if (aa->OperStatus != IfOperStatusUp) continue;
                if (aa->IfType == IF_TYPE_IEEE80211 && snap.wifiName.isEmpty() && aa->AdapterName) {
                    const QString guidStr = QString::fromLatin1(aa->AdapterName);
                    GUID adapterGuid = {};
                    if (SUCCEEDED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(guidStr.utf16()), &adapterGuid))) {
                        const QString nlmWifi = queryConnectedWifiNameFromNlm(&adapterGuid);
                        if (!nlmWifi.isEmpty()) {
                            snap.wifiName = nlmWifi;
                            if (!hasRtWifi) {
                                snap.wifiOn = true;
                            }
                        }
                    }
                    continue;
                }
                if (aa->IfType == IF_TYPE_ETHERNET_CSMACD && snap.ethernetName.isEmpty()) {
                    const QString friendly = QString::fromWCharArray(aa->FriendlyName);
                    const QString desc = aa->Description ? QString::fromWCharArray(aa->Description) : QString();
                    if (isVirtualNic(desc.toLower(), friendly.toLower())) {
                        continue;
                    }
                    // Require an active default gateway so disconnected/dummy virtual LAN adapters never hijack Wi-Fi
                    if (aa->FirstGatewayAddress == nullptr) {
                        continue;
                    }
                    snap.ethernetName = friendly;
                }
            }
        }

        // 3. Throughput
        ULONG ifSize = 0;
        if (GetIfTable(nullptr, &ifSize, FALSE) == ERROR_INSUFFICIENT_BUFFER && ifSize > 0) {
            std::vector<BYTE> ifBuf(ifSize);
            auto* table = reinterpret_cast<PMIB_IFTABLE>(ifBuf.data());
            if (GetIfTable(table, &ifSize, FALSE) == NO_ERROR) {
                quint64 totalIn = 0, totalOut = 0;
                for (DWORD i = 0; i < table->dwNumEntries; ++i) {
                    const MIB_IFROW& row = table->table[i];
                    if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK || row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL) continue;
                    totalIn += row.dwInOctets;
                    totalOut += row.dwOutOctets;
                }
                const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
                if (prevMs > 0 && nowMs > prevMs) {
                    const qreal dtSec = static_cast<qreal>(nowMs - prevMs) / 1000.0;
                    if (totalIn >= prevIn && totalOut >= prevOut) {
                        snap.downloadBps = static_cast<qreal>(totalIn - prevIn) / dtSec;
                        snap.uploadBps = static_cast<qreal>(totalOut - prevOut) / dtSec;
                    }
                }
                prevIn = totalIn;
                prevOut = totalOut;
                prevMs = nowMs;
            }
        }

        if (m_stop.load()) break;

        // 4. Bluetooth (immediate when woken after toggle, otherwise every 3rd cycle)
        if (forcePoll || (tick % 3) == 0) {
            const bool hasRtBt = rtBtFound;

            if (hasRtBt) {
                lastBtAvail = true;
                lastBtOn = rtBtOn;
            }
            HANDLE hRadio = nullptr;
            BLUETOOTH_FIND_RADIO_PARAMS params = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
            HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&params, &hRadio);
            if (!hFind) {
                if (!hasRtBt) {
                    lastBtAvail = false;
                    lastBtOn = false;
                }
                lastBtConn = false;
                lastBtDetail = lastBtAvail ? QStringLiteral("OFF") : QStringLiteral("Unavailable");
            } else {
                lastBtAvail = true;
                if (!hasRtBt) {
                    lastBtOn = true;
                }
                CloseHandle(hRadio);
                BluetoothFindRadioClose(hFind);
                if (!lastBtOn) {
                    lastBtConn = false;
                    lastBtDetail = QStringLiteral("OFF");
                } else {
                    QString connectedName;
                    BLUETOOTH_DEVICE_SEARCH_PARAMS dParams = {};
                    dParams.dwSize = sizeof(dParams);
                    dParams.fReturnAuthenticated = TRUE;
                    dParams.fReturnRemembered = FALSE;
                    dParams.fReturnConnected = TRUE;
                    dParams.fIssueInquiry = FALSE;
                    dParams.cTimeoutMultiplier = 0;
                    BLUETOOTH_DEVICE_INFO devInfo = {};
                    devInfo.dwSize = sizeof(devInfo);
                    HBLUETOOTH_DEVICE_FIND hDev = BluetoothFindFirstDevice(&dParams, &devInfo);
                    if (hDev) {
                        do {
                            if (devInfo.fConnected) {
                                connectedName = QString::fromWCharArray(devInfo.szName).trimmed();
                                break;
                            }
                        } while (BluetoothFindNextDevice(hDev, &devInfo));
                        BluetoothFindDeviceClose(hDev);
                    }
                    lastBtConn = !connectedName.isEmpty();
                    lastBtDetail = lastBtConn ? connectedName : QStringLiteral("Ready");
                }
            }
        }
        snap.bluetoothAvailable = lastBtAvail;
        snap.bluetoothOn = lastBtOn;
        snap.bluetoothConnected = lastBtConn;
        snap.bluetoothDetail = lastBtDetail;

        // 5. One-time hardware brightness probe on worker thread (never stalls UI!)
        if (!queriedBrightness && !m_stop.load()) {
            queriedBrightness = true;
            int pct = -1;
            if (queryWmiBrightness(pct)) {
                snap.hasBrightness = true;
                snap.hardwareBrightness = true;
                snap.brightness = qBound(0, pct, 100);
            } else {
                EnumDisplayMonitors(nullptr, nullptr, queryDdcBrightnessProc, reinterpret_cast<LPARAM>(&pct));
                if (pct >= 0) {
                    snap.hasBrightness = true;
                    snap.hardwareBrightness = true;
                    snap.brightness = qBound(0, pct, 100);
                } else {
                    snap.hasBrightness = true;
                    snap.hardwareBrightness = false;
                    snap.brightness = 100.0;
                }
            }
        }

        ++tick;

        if (!m_stop.load()) {
            QMetaObject::invokeMethod(this, [this, snap]() {
                bool dirty = false;
                auto upd = [&](auto& field, const auto& val) {
                    if (field != val) {
                        field = val;
                        dirty = true;
                    }
                };
                upd(m_speakerAvailable, snap.speakerAvailable);
                upd(m_speakerOn, snap.speakerOn);
                upd(m_speakerVolume, snap.speakerVolume);
                upd(m_microphoneAvailable, snap.microphoneAvailable);
                upd(m_microphoneOn, snap.microphoneOn);
                upd(m_microphoneVolume, snap.microphoneVolume);
                upd(m_wifiOn, snap.wifiOn);
                upd(m_wifiName, snap.wifiName);
                upd(m_ethernetName, snap.ethernetName);
                upd(m_bluetoothAvailable, snap.bluetoothAvailable);
                upd(m_bluetoothOn, snap.bluetoothOn);
                upd(m_bluetoothConnected, snap.bluetoothConnected);
                upd(m_bluetoothDetail, snap.bluetoothDetail);
                if (snap.hasBrightness) {
                    upd(m_hardwareBrightness, snap.hardwareBrightness);
                    upd(m_brightness, snap.brightness);
                }
                if (qAbs(m_downloadBps - snap.downloadBps) > 128.0 || qAbs(m_uploadBps - snap.uploadBps) > 128.0) {
                    m_downloadBps = snap.downloadBps;
                    m_uploadBps = snap.uploadBps;
                    dirty = true;
                }
                if (dirty) {
                    Q_EMIT changed();
                }
            }, Qt::QueuedConnection);
        }

        // Sleep ~5s in 100ms slices so shutdown and on-demand refresh() are instantaneous.
        for (int i = 0; i < 50 && !m_stop.load() && !m_wakeNow.load(); ++i) {
            Sleep(100);
        }
    }

    if (didRoInit && cbApi.roUninit) {
        cbApi.roUninit();
    } else if (didCoInit) {
        CoUninitialize();
    }
}

void QuickSource::pollAudio() {
    Win32::ComInitGuard com;

    IAudioEndpointVolume* spk = nullptr;
    if (getEndpointVolume(eRender, &spk)) {
        float level = 0.0f;
        BOOL muted = FALSE;
        spk->GetMasterVolumeLevelScalar(&level);
        spk->GetMute(&muted);
        spk->Release();
        m_speakerAvailable = true;
        m_speakerVolume = qRound(qBound(0.0f, level, 1.0f) * 100.0f);
        m_speakerOn = (muted == FALSE);
    } else {
        m_speakerAvailable = false;
    }

    IAudioEndpointVolume* mic = nullptr;
    if (getEndpointVolume(eCapture, &mic)) {
        float level = 0.0f;
        BOOL muted = FALSE;
        mic->GetMasterVolumeLevelScalar(&level);
        mic->GetMute(&muted);
        mic->Release();
        m_microphoneAvailable = true;
        m_microphoneVolume = qRound(qBound(0.0f, level, 1.0f) * 100.0f);
        m_microphoneOn = (muted == FALSE);
    } else {
        m_microphoneAvailable = false;
    }
}

void QuickSource::pollNetwork() {
    m_wifiOn = false;
    m_wifiName.clear();
    m_ethernetName.clear();

    DWORD negotiatedVer = 0;
    HANDLE hWlan = nullptr;
    if (WlanOpenHandle(2, nullptr, &negotiatedVer, &hWlan) == ERROR_SUCCESS && hWlan) {
        PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
        if (WlanEnumInterfaces(hWlan, nullptr, &ifList) == ERROR_SUCCESS && ifList) {
            for (DWORD i = 0; i < ifList->dwNumberOfItems; ++i) {
                const WLAN_INTERFACE_INFO& info = ifList->InterfaceInfo[i];
                if (info.isState != wlan_interface_state_not_ready) {
                    m_wifiOn = true;
                }
                if (info.isState == wlan_interface_state_connected) {
                    DWORD dataSize = 0;
                    PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
                    if (WlanQueryInterface(
                            hWlan,
                            &info.InterfaceGuid,
                            wlan_intf_opcode_current_connection,
                            nullptr,
                            &dataSize,
                            reinterpret_cast<PVOID*>(&conn),
                            nullptr) == ERROR_SUCCESS && conn) {
                        const DOT11_SSID& ssid = conn->wlanAssociationAttributes.dot11Ssid;
                        if (ssid.uSSIDLength > 0 && ssid.uSSIDLength <= DOT11_SSID_MAX_LENGTH) {
                            m_wifiName = QString::fromUtf8(reinterpret_cast<const char*>(ssid.ucSSID), static_cast<int>(ssid.uSSIDLength));
                        }
                        WlanFreeMemory(conn);
                    }
                    if (m_wifiName.isEmpty()) {
                        m_wifiName = queryConnectedWifiNameFromNlm(&info.InterfaceGuid);
                    }
                    break;
                }
            }
            WlanFreeMemory(ifList);
        }
        WlanCloseHandle(hWlan, nullptr);
    }

    ULONG bufSize = 16 * 1024;
    std::vector<BYTE> buf(bufSize);
    ULONG res = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                     nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufSize);
    if (res == ERROR_BUFFER_OVERFLOW) {
        buf.resize(bufSize);
        res = GetAdaptersAddresses(AF_UNSPEC, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                   nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &bufSize);
    }
    if (res == NO_ERROR) {
        for (auto* aa = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); aa != nullptr; aa = aa->Next) {
            if (aa->OperStatus != IfOperStatusUp) {
                continue;
            }
            if (aa->IfType == IF_TYPE_ETHERNET_CSMACD) {
                m_ethernetName = QString::fromWCharArray(aa->FriendlyName);
                break;
            }
        }
    }
}

void QuickSource::pollBluetooth() {
    HANDLE hRadio = nullptr;
    BLUETOOTH_FIND_RADIO_PARAMS params = { sizeof(BLUETOOTH_FIND_RADIO_PARAMS) };
    HBLUETOOTH_RADIO_FIND hFind = BluetoothFindFirstRadio(&params, &hRadio);
    if (!hFind) {
        m_bluetoothAvailable = false;
        m_bluetoothOn = false;
        m_bluetoothConnected = false;
        m_bluetoothDetail = QStringLiteral("Unavailable");
        return;
    }

    m_bluetoothAvailable = true;
    m_bluetoothOn = true;
    CloseHandle(hRadio);
    BluetoothFindRadioClose(hFind);

    QString connectedName;
    BLUETOOTH_DEVICE_SEARCH_PARAMS dParams = {};
    dParams.dwSize = sizeof(dParams);
    dParams.fReturnAuthenticated = TRUE;
    dParams.fReturnRemembered = FALSE;
    dParams.fReturnConnected = TRUE;
    dParams.fIssueInquiry = FALSE;
    dParams.cTimeoutMultiplier = 0;

    BLUETOOTH_DEVICE_INFO devInfo = {};
    devInfo.dwSize = sizeof(devInfo);
    HBLUETOOTH_DEVICE_FIND hDev = BluetoothFindFirstDevice(&dParams, &devInfo);
    if (hDev) {
        do {
            if (devInfo.fConnected) {
                connectedName = QString::fromWCharArray(devInfo.szName).trimmed();
                break;
            }
        } while (BluetoothFindNextDevice(hDev, &devInfo));
        BluetoothFindDeviceClose(hDev);
    }

    m_bluetoothConnected = !connectedName.isEmpty();
    m_bluetoothDetail = m_bluetoothConnected ? connectedName : QStringLiteral("Ready");
}

void QuickSource::pollBrightness() {
    Win32::ComInitGuard com;
    int pct = -1;
    if (queryWmiBrightness(pct)) {
        m_hardwareBrightness = true;
        m_brightness = qBound(0, pct, 100);
        return;
    }
    EnumDisplayMonitors(nullptr, nullptr, queryDdcBrightnessProc, reinterpret_cast<LPARAM>(&pct));
    if (pct >= 0) {
        m_hardwareBrightness = true;
        m_brightness = qBound(0, pct, 100);
    } else {
        m_hardwareBrightness = false;
    }
}

void QuickSource::pollThroughput() {
    ULONG size = 0;
    if (GetIfTable(nullptr, &size, FALSE) != ERROR_INSUFFICIENT_BUFFER || size == 0) {
        return;
    }
    std::vector<BYTE> buf(size);
    auto* table = reinterpret_cast<PMIB_IFTABLE>(buf.data());
    if (GetIfTable(table, &size, FALSE) != NO_ERROR) {
        return;
    }

    quint64 totalIn = 0;
    quint64 totalOut = 0;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const MIB_IFROW& row = table->table[i];
        if (row.dwType == IF_TYPE_SOFTWARE_LOOPBACK || row.dwOperStatus != IF_OPER_STATUS_OPERATIONAL) {
            continue;
        }
        totalIn += row.dwInOctets;
        totalOut += row.dwOutOctets;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_prevSampleMs > 0 && nowMs > m_prevSampleMs) {
        const qreal dtSec = static_cast<qreal>(nowMs - m_prevSampleMs) / 1000.0;
        if (totalIn >= m_prevInBytes && totalOut >= m_prevOutBytes) {
            m_downloadBps = static_cast<qreal>(totalIn - m_prevInBytes) / dtSec;
            m_uploadBps = static_cast<qreal>(totalOut - m_prevOutBytes) / dtSec;
        }
    }
    m_prevInBytes = totalIn;
    m_prevOutBytes = totalOut;
    m_prevSampleMs = nowMs;
}

void QuickSource::applyGammaState() {
    // Combine software Gamma slider (10..100%), Night Light color temperature (2500K..6000K),
    // and software brightness fallback when hardware backlight/DDC is unavailable.
    const double gammaScale = qBound(0.10, m_gamma / 100.0, 1.0);
    const double briScale = m_hardwareBrightness ? 1.0 : (0.30 + 0.70 * qBound(0.0, m_brightness / 100.0, 1.0));
    const double lum = qBound(0.10, gammaScale * briScale, 1.0);

    const bool isIdentity = !m_nightLightOn && lum >= 0.995;
    if (isIdentity && !m_gammaModified) {
        return;
    }
    m_gammaModified = !isIdentity;

    // Smooth Kelvin warmth multipliers (6000K = neutral, 2500K = deep warm amber)
    double nlG = 1.0;
    double nlB = 1.0;
    if (m_nightLightOn) {
        const double t = qBound(0.0, (6000.0 - static_cast<double>(m_nightLightKelvin)) / 3500.0, 1.0);
        nlG = 1.0 - 0.30 * t;
        nlB = 1.0 - 0.58 * t;
    }

    auto fillRamp = [](WORD* out, double rMul, double gMul, double bMul) {
        for (int i = 0; i < 256; ++i) {
            const double base = i * 257.0;
            out[i] = static_cast<WORD>(qBound(0, qRound(base * rMul), 65535));
            out[256 + i] = static_cast<WORD>(qBound(0, qRound(base * gMul), 65535));
            out[512 + i] = static_cast<WORD>(qBound(0, qRound(base * bMul), 65535));
        }
    };

    WORD primaryRamp[3 * 256] = {};
    fillRamp(primaryRamp, lum, lum * nlG, lum * nlB);

    // Windows GDI SetDeviceGammaRamp rejects ramps whose max slope is below ~0.50 unless
    // GdiIcmGammaRange is enabled; build a guaranteed-accepted GDI fallback ramp (0.50..1.0).
    const double safeLum = 0.50 + 0.50 * ((lum - 0.10) / 0.90);
    const double safeG = qMax(0.50, safeLum * nlG);
    const double safeB = qMax(0.50, safeLum * nlB);
    WORD fallbackRamp[3 * 256] = {};
    fillRamp(fallbackRamp, qMax(0.50, safeLum), safeG, safeB);

    GammaApplyPair pair{ primaryRamp, fallbackRamp };
    EnumDisplayMonitors(nullptr, nullptr, enumGammaMonitorsProc, reinterpret_cast<LPARAM>(&pair));

    HDC screenDc = GetDC(nullptr);
    if (screenDc) {
        if (!SetDeviceGammaRamp(screenDc, primaryRamp)) {
            SetDeviceGammaRamp(screenDc, fallbackRamp);
        }
        ReleaseDC(nullptr, screenDc);
    }
}

void QuickSource::scanWifiNetworks() {
    QVariantList found;
    DWORD negotiatedVer = 0;
    HANDLE hWlan = nullptr;
    if (WlanOpenHandle(2, nullptr, &negotiatedVer, &hWlan) == ERROR_SUCCESS && hWlan) {
        PWLAN_INTERFACE_INFO_LIST ifList = nullptr;
        if (WlanEnumInterfaces(hWlan, nullptr, &ifList) == ERROR_SUCCESS && ifList) {
            for (DWORD i = 0; i < ifList->dwNumberOfItems; ++i) {
                const GUID guid = ifList->InterfaceInfo[i].InterfaceGuid;
                PWLAN_AVAILABLE_NETWORK_LIST netList = nullptr;
                if (WlanGetAvailableNetworkList(hWlan, &guid, 0, nullptr, &netList) == ERROR_SUCCESS && netList) {
                    QStringList seen;
                    for (DWORD j = 0; j < netList->dwNumberOfItems; ++j) {
                        const WLAN_AVAILABLE_NETWORK& n = netList->Network[j];
                        if (n.dot11Ssid.uSSIDLength == 0 || n.dot11Ssid.uSSIDLength > DOT11_SSID_MAX_LENGTH) {
                            continue;
                        }
                        const QString ssid = QString::fromUtf8(
                            reinterpret_cast<const char*>(n.dot11Ssid.ucSSID),
                            static_cast<int>(n.dot11Ssid.uSSIDLength));
                        if (seen.contains(ssid)) {
                            continue;
                        }
                        seen.append(ssid);
                        found.push_back(QVariantMap{
                            { QStringLiteral("ssid"), ssid },
                            { QStringLiteral("signal"), static_cast<int>(n.wlanSignalQuality) },
                            { QStringLiteral("connected"), (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0 },
                            { QStringLiteral("secured"), n.bSecurityEnabled != FALSE }
                        });
                    }
                    WlanFreeMemory(netList);
                }
            }
            WlanFreeMemory(ifList);
        }
        WlanCloseHandle(hWlan, nullptr);
    }
    m_wifiNetworks = std::move(found);
    Q_EMIT changed();
}

void QuickSource::setWifiEnabled(bool enabled) {
    m_wifiOn = enabled;
    if (!enabled) {
        m_wifiName.clear();
    }
    Q_EMIT changed();

    QPointer<QuickSource> self(this);
    std::thread([self, enabled]() {
        setWinRtRadioState(1, enabled); // RadioKind::WiFi = 1
        if (self) {
            QMetaObject::invokeMethod(self, [self]() {
                if (self) {
                    self->refresh();
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void QuickSource::toggleWifi() {
    setWifiEnabled(!m_wifiOn);
}

void QuickSource::connectWifi(const QString& ssid) {
    if (ssid.isEmpty()) {
        return;
    }
    Win32::runWindowsCommand(QStringLiteral("netsh wlan connect name=\"%1\"").arg(ssid), false);
    QTimer::singleShot(1200, this, &QuickSource::refresh);
}

void QuickSource::setBluetoothEnabled(bool enabled) {
    m_bluetoothOn = enabled;
    if (!enabled) {
        m_bluetoothConnected = false;
        m_bluetoothDetail = QStringLiteral("OFF");
    } else if (m_bluetoothDetail == QStringLiteral("OFF")) {
        m_bluetoothDetail = QStringLiteral("Ready");
    }
    Q_EMIT changed();

    QPointer<QuickSource> self(this);
    std::thread([self, enabled]() {
        setWinRtRadioState(3, enabled); // RadioKind::Bluetooth = 3
        if (self) {
            QMetaObject::invokeMethod(self, [self]() {
                if (self) {
                    self->refresh();
                }
            }, Qt::QueuedConnection);
        }
    }).detach();
}

void QuickSource::toggleBluetooth() {
    setBluetoothEnabled(!m_bluetoothOn);
}

void QuickSource::setSpeakerVolume(qreal percent) {
    Win32::ComInitGuard com;
    const qreal clamped = qBound<qreal>(0.0, percent, 100.0);
    IAudioEndpointVolume* spk = nullptr;
    if (getEndpointVolume(eRender, &spk)) {
        spk->SetMasterVolumeLevelScalar(static_cast<float>(clamped / 100.0), nullptr);
        spk->Release();
        m_speakerVolume = qRound(clamped);
        if (m_osd) {
            m_osd->show(QStringLiteral("volume"),
                        !m_speakerOn ? QStringLiteral("volume_off") : (m_speakerVolume < 45 ? QStringLiteral("volume_down") : QStringLiteral("volume_up")),
                        QStringLiteral("Speaker"), m_speakerVolume, !m_speakerOn);
        }
        Q_EMIT changed();
    }
}

void QuickSource::toggleSpeakerMute() {
    Win32::ComInitGuard com;
    IAudioEndpointVolume* spk = nullptr;
    if (getEndpointVolume(eRender, &spk)) {
        BOOL muted = FALSE;
        spk->GetMute(&muted);
        spk->SetMute(muted ? FALSE : TRUE, nullptr);
        spk->Release();
        m_speakerOn = (muted != FALSE);
        if (m_osd) {
            m_osd->show(QStringLiteral("volume"),
                        m_speakerOn ? QStringLiteral("volume_up") : QStringLiteral("volume_off"),
                        m_speakerOn ? QStringLiteral("Speaker") : QStringLiteral("Muted"), m_speakerVolume, !m_speakerOn);
        }
        Q_EMIT changed();
    }
}

void QuickSource::setMicrophoneVolume(qreal percent) {
    Win32::ComInitGuard com;
    const qreal clamped = qBound<qreal>(0.0, percent, 100.0);
    IAudioEndpointVolume* mic = nullptr;
    if (getEndpointVolume(eCapture, &mic)) {
        mic->SetMasterVolumeLevelScalar(static_cast<float>(clamped / 100.0), nullptr);
        mic->Release();
        m_microphoneVolume = qRound(clamped);
        if (m_osd) {
            m_osd->show(QStringLiteral("mic"), m_microphoneOn ? QStringLiteral("mic") : QStringLiteral("mic_off"),
                        QStringLiteral("Microphone"), m_microphoneVolume, !m_microphoneOn);
        }
        Q_EMIT changed();
    }
}

void QuickSource::toggleMicrophoneMute() {
    Win32::ComInitGuard com;
    IAudioEndpointVolume* mic = nullptr;
    if (getEndpointVolume(eCapture, &mic)) {
        BOOL muted = FALSE;
        mic->GetMute(&muted);
        mic->SetMute(muted ? FALSE : TRUE, nullptr);
        mic->Release();
        m_microphoneOn = (muted != FALSE);
        if (m_osd) {
            m_osd->show(QStringLiteral("mic"), m_microphoneOn ? QStringLiteral("mic") : QStringLiteral("mic_off"),
                        m_microphoneOn ? QStringLiteral("Mic On") : QStringLiteral("Mic Muted"), m_microphoneVolume, !m_microphoneOn);
        }
        Q_EMIT changed();
    }
}

void QuickSource::brightnessThreadLoop() {
    const bool didCoInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    while (!m_stop.load()) {
        int pct = -1;
        {
            std::unique_lock<std::mutex> lock(m_brightnessMutex);
            m_brightnessCv.wait(lock, [this]() {
                return m_stop.load() || m_pendingBrightness >= 0;
            });
            if (m_stop.load()) {
                break;
            }
            pct = m_pendingBrightness;
            m_pendingBrightness = -1;
        }
        if (pct >= 0) {
            const bool wmiOk = setWmiBrightness(pct);
            EnumDisplayMonitors(nullptr, nullptr, setDdcBrightnessProc, static_cast<LPARAM>(pct));
            if (wmiOk && !m_stop.load()) {
                QMetaObject::invokeMethod(this, [this]() {
                    m_hardwareBrightness = true;
                }, Qt::QueuedConnection);
            }
        }
    }
    if (didCoInit) {
        CoUninitialize();
    }
}

void QuickSource::setBrightness(qreal percent) {
    const int pct = qRound(qBound<qreal>(5.0, percent, 100.0));
    m_brightness = pct;
    if (!m_hardwareBrightness) {
        applyGammaState();
    }
    {
        std::lock_guard<std::mutex> lock(m_brightnessMutex);
        m_pendingBrightness = pct;
    }
    m_brightnessCv.notify_one();
    if (m_osd) {
        m_osd->show(QStringLiteral("brightness"), QStringLiteral("brightness_6"), QStringLiteral("Brightness"), m_brightness, false);
    }
    Q_EMIT changed();
}

void QuickSource::setGamma(qreal percent) {
    const int pct = qRound(qBound<qreal>(10.0, percent, 100.0));
    m_gamma = pct;
    applyGammaState();
    Q_EMIT changed();
}

void QuickSource::setNightLightTemperature(int kelvin) {
    const int clamped = qBound(2500, kelvin, 6000);
    const bool wantOn = clamped < 5950;
    m_nightLightOn = wantOn;
    if (wantOn) {
        m_nightLightKelvin = clamped;
    }
    if (m_settings) {
        m_settings->set(QStringLiteral("quick.nightLight"), m_nightLightOn);
    }
    applyGammaState();
    Q_EMIT changed();
}

void QuickSource::toggleNightLight() {
    m_nightLightOn = !m_nightLightOn;
    if (m_settings) {
        m_settings->set(QStringLiteral("quick.nightLight"), m_nightLightOn);
    }
    applyGammaState();
    if (m_osd) {
        m_osd->show(QStringLiteral("nightlight"), QStringLiteral("bedtime"),
                    m_nightLightOn ? QStringLiteral("Night Light On") : QStringLiteral("Night Light Off"),
                    m_nightLightOn ? 100 : 0, !m_nightLightOn);
    }
    Q_EMIT changed();
}

void QuickSource::copyText(const QString& text) {
    if (auto* cb = QGuiApplication::clipboard()) {
        cb->setText(text);
    }
}

void QuickSource::openUrl(const QString& url) {
    const QUrl u = QUrl::fromUserInput(url.trimmed());
    if (u.isValid()) {
        Win32::launchShellTarget(u.toString());
    }
}

void QuickSource::runCommand(const QString& command, bool inTerminal) {
    Win32::runWindowsCommand(command, inTerminal);
}

void QuickSource::openWindowsSettings(const QString& pageUri) {
    const QString uri = pageUri.isEmpty() ? QStringLiteral("ms-settings:") : pageUri;
    Win32::launchShellTarget(uri);
}

void QuickSource::openTaskManager() {
    Win32::launchShellTarget(QStringLiteral("taskmgr.exe"));
}

void QuickSource::lockWorkstation() {
    LockWorkStation();
}

void QuickSource::sleepSystem() {
    SetSuspendState(FALSE, FALSE, FALSE);
}

void QuickSource::signOut() {
    ExitWindowsEx(EWX_LOGOFF, 0);
}

void QuickSource::restartSystem() {
    Win32::launchShellTarget(QStringLiteral("shutdown.exe"), QStringLiteral("/r /t 0"), {}, SW_HIDE);
}

void QuickSource::shutdownSystem() {
    Win32::launchShellTarget(QStringLiteral("shutdown.exe"), QStringLiteral("/s /t 0"), {}, SW_HIDE);
}

QString QuickSource::uptimeText() const {
    const quint64 totalSec = GetTickCount64() / 1000ULL;
    const quint64 days = totalSec / 86400ULL;
    const quint64 hours = (totalSec % 86400ULL) / 3600ULL;
    const quint64 mins = (totalSec % 3600ULL) / 60ULL;
    if (days > 0) {
        return QStringLiteral("%1d %2h").arg(days).arg(hours);
    }
    if (hours > 0) {
        return QStringLiteral("%1h %2m").arg(hours).arg(mins);
    }
    return QStringLiteral("%1m").arg(mins);
}

void QuickSource::toggleKeepAwake() {
    m_keepAwakeOn = !m_keepAwakeOn;
    if (m_keepAwakeOn) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    } else {
        SetThreadExecutionState(ES_CONTINUOUS);
    }
    if (m_osd) {
        m_osd->show(QStringLiteral("keepawake"), QStringLiteral("emoji_food_beverage"),
                    m_keepAwakeOn ? QStringLiteral("Keep Awake On") : QStringLiteral("Keep Awake Off"),
                    m_keepAwakeOn ? 100 : 0, !m_keepAwakeOn);
    }
    Q_EMIT changed();
}

void QuickSource::toggleNotifications() {
    m_notificationsOn = !m_notificationsOn;
    if (m_osd) {
        m_osd->show(QStringLiteral("notifications"),
                    m_notificationsOn ? QStringLiteral("notifications_active") : QStringLiteral("notifications_off"),
                    m_notificationsOn ? QStringLiteral("Alerts Enabled") : QStringLiteral("Do Not Disturb"),
                    m_notificationsOn ? 100 : 0, !m_notificationsOn);
    }
    Q_EMIT changed();
}

void QuickSource::toggleGameMode() {
    m_gameModeOn = !m_gameModeOn;
    if (m_osd) {
        m_osd->show(QStringLiteral("gamemode"), QStringLiteral("gamepad"),
                    m_gameModeOn ? QStringLiteral("Game Mode On") : QStringLiteral("Game Mode Off"),
                    m_gameModeOn ? 100 : 0, !m_gameModeOn);
    }
    Q_EMIT changed();
}

void QuickSource::openOnScreenKeyboard() {
    Win32::launchShellTarget(QStringLiteral("osk.exe"));
}

void QuickSource::triggerScreenSnip() {
    // Brief 160ms delay so Quick Panel / Phone finishes hiding before freezing the screen.
    QTimer::singleShot(160, []() {
        Win32::ScreenshotOverlay::startRegionSnip();
    });
}

void QuickSource::hibernate() {
    SetSuspendState(TRUE, FALSE, FALSE);
}

} // namespace SekaiPop::Services
