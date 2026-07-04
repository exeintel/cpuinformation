/*
 * CPU Information v0.1
 * Console utility for displaying Windows system information
 * Developed by ExEintel
 * License: MIT
 */

#include <windows.h>
#include <wbemidl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── Color Constants ─────────────────────────────────────────────── */

#define C_DEFAULT   7
#define C_INTEL     9
#define C_AMD       12
#define C_NVIDIA    10
#define C_YELLOW    14
#define C_CYAN      11
#define C_PURPLE    13
#define C_WHITE     15
#define C_RED       12
#define C_ORANGE    14

/* ─── Global Variables ────────────────────────────────────────────── */

static HANDLE g_hConsole = NULL;
static BOOL   g_export   = FALSE;
static FILE  *g_exportFile = NULL;
static char   g_exportName[260] = "";

/* ─── Console Helpers ─────────────────────────────────────────────── */

static void SetConColor(WORD c) {
    SetConsoleTextAttribute(g_hConsole, c);
}

static void PrintF(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    if (g_exportFile) {
        va_start(ap, fmt);
        vfprintf(g_exportFile, fmt, ap);
        va_end(ap);
    }
}

static void PrintC(WORD color, const char *fmt, ...) {
    char buf[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (!g_export) SetConColor(color);
    printf("%s", buf);
    if (!g_export) SetConColor(C_DEFAULT);
    if (g_exportFile) fprintf(g_exportFile, "%s", buf);
}

static void PrintHdr(WORD color, const char *title) {
    int len = (int)strlen(title);
    PrintC(color, "\n");
    for (int i = 0; i < 60; i++) PrintC(color, "=");
    PrintC(color, "\n  %s\n", title);
    for (int i = 0; i < 60; i++) PrintC(color, "=");
    PrintC(color, "\n");
}

/* ─── WMI Helpers ─────────────────────────────────────────────────── */

static IWbemServices *WmiInit(void) {
    IWbemLocator  *pLoc = NULL;
    IWbemServices *pSvc = NULL;

    HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr)) return NULL;

    hr = CoInitializeSecurity(NULL, -1, NULL, NULL,
                              RPC_C_AUTHN_LEVEL_DEFAULT,
                              RPC_C_IMP_LEVEL_IMPERSONATE,
                              NULL, EOAC_NONE, NULL);
    if (FAILED(hr) && hr != RPC_E_TOO_LATE) {
        CoUninitialize();
        return NULL;
    }

    hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) { CoUninitialize(); return NULL; }

    hr = pLoc->lpVtbl->ConnectServer(pLoc, L"ROOT\\CIMV2", NULL, NULL, NULL,
                                      (LONG)0, NULL, NULL, &pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (FAILED(hr)) { CoUninitialize(); return NULL; }

    hr = CoSetProxyBlanket((IUnknown*)pSvc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
                            NULL, RPC_C_AUTHN_LEVEL_CALL,
                            RPC_C_IMP_LEVEL_IMPERSONATE,
                            NULL, EOAC_NONE);
    if (FAILED(hr)) { pSvc->lpVtbl->Release(pSvc); CoUninitialize(); return NULL; }

    return pSvc;
}

static void WmiDone(IWbemServices *pSvc) {
    if (pSvc) pSvc->lpVtbl->Release(pSvc);
    CoUninitialize();
}

static BOOL WmiQuery(IWbemServices *pSvc, const wchar_t *wql,
                     IEnumWbemClassObject **ppEnum) {
    HRESULT hr = pSvc->lpVtbl->ExecQuery(pSvc, L"WQL", (BSTR)wql,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        NULL, ppEnum);
    return SUCCEEDED(hr);
}

static BOOL WmiNext(IEnumWbemClassObject *pEnum, IWbemClassObject **ppObj) {
    ULONG ret = 0;
    HRESULT hr = pEnum->lpVtbl->Next(pEnum, WBEM_INFINITE, 1, ppObj, &ret);
    return SUCCEEDED(hr) && ret > 0;
}

static BOOL WmiGetStr(IWbemClassObject *pObj, const wchar_t *prop,
                      wchar_t *buf, DWORD sz) {
    VARIANT vt;
    VariantInit(&vt);
    HRESULT hr = pObj->lpVtbl->Get(pObj, prop, 0, &vt, NULL, NULL);
    if (SUCCEEDED(hr) && vt.vt == VT_BSTR && vt.bstrVal) {
        wcsncpy(buf, vt.bstrVal, sz - 1); buf[sz - 1] = 0;
        VariantClear(&vt);
        return TRUE;
    }
    if (SUCCEEDED(hr) && vt.vt != VT_NULL) {
        VARIANT vt2;
        VariantInit(&vt2);
        if (SUCCEEDED(VariantChangeType(&vt2, &vt, 0, VT_BSTR))) {
            wcsncpy(buf, vt2.bstrVal, sz - 1); buf[sz - 1] = 0;
            VariantClear(&vt2); VariantClear(&vt);
            return TRUE;
        }
    }
    VariantClear(&vt);
    buf[0] = 0;
    return FALSE;
}

static BOOL WmiGetInt(IWbemClassObject *pObj, const wchar_t *prop, int *v) {
    VARIANT vt;
    VariantInit(&vt);
    HRESULT hr = pObj->lpVtbl->Get(pObj, prop, 0, &vt, NULL, NULL);
    if (SUCCEEDED(hr)) {
        if (vt.vt == VT_I4)  { *v = vt.lVal;  VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_UI4) { *v = (int)vt.ulVal; VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_I2)  { *v = vt.iVal;  VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_UI2) { *v = vt.uiVal; VariantClear(&vt); return TRUE; }
        VARIANT vt2;
        VariantInit(&vt2);
        if (SUCCEEDED(VariantChangeType(&vt2, &vt, 0, VT_I4))) {
            *v = vt2.lVal;
            VariantClear(&vt2); VariantClear(&vt);
            return TRUE;
        }
    }
    VariantClear(&vt);
    return FALSE;
}

static BOOL WmiGetLL(IWbemClassObject *pObj, const wchar_t *prop,
                     ULONGLONG *v) {
    VARIANT vt;
    VariantInit(&vt);
    HRESULT hr = pObj->lpVtbl->Get(pObj, prop, 0, &vt, NULL, NULL);
    if (SUCCEEDED(hr)) {
        if (vt.vt == VT_UI8) { *v = vt.ullVal; VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_I8)  { *v = (ULONGLONG)vt.llVal; VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_UI4) { *v = vt.ulVal;  VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_I4)  { *v = (ULONGLONG)vt.lVal; VariantClear(&vt); return TRUE; }
        if (vt.vt == VT_BSTR && vt.bstrVal) {
            *v = _wcstoui64(vt.bstrVal, NULL, 10);
            VariantClear(&vt);
            return TRUE;
        }
    }
    VariantClear(&vt);
    return FALSE;
}

static BOOL WmiGetBool(IWbemClassObject *pObj, const wchar_t *prop, BOOL *v) {
    int val = 0;
    if (WmiGetInt(pObj, prop, &val)) {
        *v = (val != 0);
        return TRUE;
    }
    return FALSE;
}

/* ─── Format Helpers ──────────────────────────────────────────────── */

static void fmtSize(ULONGLONG bytes, char *buf, DWORD sz) {
    if (bytes >= (1ULL << 40))
        snprintf(buf, sz, "%.2f TB", (double)bytes / (1ULL << 40));
    else if (bytes >= (1ULL << 30))
        snprintf(buf, sz, "%.2f GB", (double)bytes / (1ULL << 30));
    else if (bytes >= (1ULL << 20))
        snprintf(buf, sz, "%.2f MB", (double)bytes / (1ULL << 20));
    else if (bytes >= (1ULL << 10))
        snprintf(buf, sz, "%.2f KB", (double)bytes / (1ULL << 10));
    else
        snprintf(buf, sz, "%llu B", bytes);
}

static void w2a(const wchar_t *w, char *buf, DWORD sz) {
    if (!w || !w[0]) { buf[0] = 0; return; }
    WideCharToMultiByte(CP_ACP, 0, w, -1, buf, sz, NULL, NULL);
}

static void parseDmtfDate(const wchar_t *dmtf, char *buf, DWORD sz) {
    if (!dmtf || wcslen(dmtf) < 8) { snprintf(buf, sz, "N/A"); return; }
    wchar_t y[5], m[3], d[3];
    wcsncpy(y, dmtf, 4); y[4] = 0;
    wcsncpy(m, dmtf + 4, 2); m[2] = 0;
    wcsncpy(d, dmtf + 6, 2); d[2] = 0;
    int yi = (int)wcstol(y, NULL, 10);
    int mi = (int)wcstol(m, NULL, 10);
    int di = (int)wcstol(d, NULL, 10);
    if (yi == 0 || mi == 0 || di == 0)
        snprintf(buf, sz, "N/A");
    else
        snprintf(buf, sz, "%04d-%02d-%02d", yi, mi, di);
}

static const char *ddrType(int smbiosType) {
    switch (smbiosType) {
        case 20: return "DDR";
        case 21: return "DDR2";
        case 24: return "DDR3";
        case 26: return "DDR4";
        case 34: return "DDR5";
        default: return "Unknown";
    }
}

static const char *diskType(const wchar_t *model, const wchar_t *iface) {
    if (wcsstr(iface, L"NVMe") || wcsstr(model, L"NVMe"))
        return "NVMe";
    if (wcsstr(model, L"SSD") || wcsstr(model, L"Solid State") ||
        wcsstr(model, L"Solid-State") || wcsstr(model, L"M.2") ||
        wcsstr(model, L"WDC ") || wcsstr(model, L"SanDisk") ||
        wcsstr(model, L"KINGSTON ") || wcsstr(model, L"INTEL ") ||
        wcsstr(model, L"Crucial") || wcsstr(model, L"ADATA") ||
        wcsstr(model, L"Transcend") || wcsstr(model, L"Apacer") ||
        wcsstr(model, L"Patriot") || wcsstr(model, L"Corsair") ||
        wcsstr(model, L"PNY ") || wcsstr(model, L"SPCC") ||
        wcsstr(model, L"Lexar") || wcsstr(model, L"T-FORCE"))
        return "SSD";
    if (wcsstr(iface, L"USB"))
        return "External";
    if (wcsstr(iface, L"SCSI")) {
        /* NVMe/SSD drives often use SCSI interface on Windows */
        if (wcsstr(model, L"SSD")) return "SSD";
        /* Check for models with numbers+GB pattern (e.g. "256GB") - usually SSD/NVMe */
        return "SSD"; /* Most SCSI drives are modern SSDs */
    }
    return "HDD";
}

/* ─── CPU Info ────────────────────────────────────────────────────── */

static void PrintCPU(IWbemServices *pSvc) {
    PrintHdr(C_DEFAULT, "CPU Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    IEnumWbemClassObject *pEnum = NULL;
    if (!WmiQuery(pSvc, L"SELECT * FROM Win32_Processor", &pEnum)) {
        PrintF("  Failed to query CPU info.\n");
        return;
    }

    IWbemClassObject *pObj = NULL;
    if (!WmiNext(pEnum, &pObj)) {
        PrintF("  No CPU data found.\n");
        pEnum->lpVtbl->Release(pEnum);
        return;
    }

    wchar_t name[1024] = L"", manufacturer[256] = L"";
    int cores = 0, threads = 0, maxFreq = 0, curFreq = 0;
    int l2 = 0, l3 = 0, tdp = 0;

    WmiGetStr(pObj, L"Name", name, 1024);
    WmiGetStr(pObj, L"Manufacturer", manufacturer, 256);
    WmiGetInt(pObj, L"NumberOfCores", &cores);
    WmiGetInt(pObj, L"NumberOfLogicalProcessors", &threads);
    WmiGetInt(pObj, L"MaxClockSpeed", &maxFreq);
    WmiGetInt(pObj, L"CurrentClockSpeed", &curFreq);
    WmiGetInt(pObj, L"L2CacheSize", &l2);
    WmiGetInt(pObj, L"L3CacheSize", &l3);
    WmiGetInt(pObj, L"CurrentVoltage", &tdp);

    WORD clr = C_DEFAULT;
    if (wcsstr(manufacturer, L"Intel") || wcsstr(name, L"Intel"))
        clr = C_INTEL;
    else if (wcsstr(manufacturer, L"AMD") || wcsstr(name, L"AMD") ||
             wcsstr(manufacturer, L"AuthenticAMD"))
        clr = C_AMD;

    char buf[1024], mbuf[256];
    w2a(name, buf, 1024);
    w2a(manufacturer, mbuf, 256);

    /* Trim name */
    char *trim = buf;
    while (*trim == ' ') trim++;
    PrintC(clr, "  Model:               %s\n", trim);
    PrintF("  Manufacturer:        %s\n", mbuf);
    PrintF("  Cores:               %d\n", cores);
    PrintF("  Logical Processors:  %d\n", threads);
    if (maxFreq > 0)
        PrintF("  Max Clock Speed:     %d MHz\n", maxFreq);
    if (curFreq > 0)
        PrintF("  Current Clock Speed: %d MHz\n", curFreq);
    if (l2 > 0)
        PrintF("  L2 Cache:            %d KB\n", l2);
    if (l3 > 0)
        PrintF("  L3 Cache:            %d KB\n", l3);

    /* Process - check Win32_Processor again for family info or use registry */
    /* Try to get TDP from WMI - may not be available */
    wchar_t tdpStr[64] = L"";
    if (WmiGetStr(pObj, L"MaxClockSpeed", tdpStr, 64)) {
        /* TDP not directly in most WMI. Check VoltageCaps or use lookup */
    }

    pObj->lpVtbl->Release(pObj);
    pEnum->lpVtbl->Release(pEnum);

    /* Query MSR or registry for more CPU details */
    HKEY hKey;
    LONG lRet = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey);
    if (lRet == ERROR_SUCCESS) {
        DWORD type = 0, data = 0, sz = sizeof(data);
        if (RegQueryValueExA(hKey, "~MHz", NULL, &type, (LPBYTE)&data, &sz) == ERROR_SUCCESS) {
            if (data > 0)
                PrintF("  Current Frequency:   %u MHz\n", data);
        }
        RegCloseKey(hKey);
    }

    /* L1 cache from registry */
    lRet = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey);
    if (lRet == ERROR_SUCCESS) {
        /* L1, L2, L3 from registry */
        DWORD type = 0, sz = 0;
        RegQueryValueExA(hKey, "L1Cache", NULL, &type, NULL, &sz);
        if (sz > 0) {
            BYTE *v = malloc(sz);
            if (v) {
                if (RegQueryValueExA(hKey, "L1Cache", NULL, &type, v, &sz) == ERROR_SUCCESS) {
                    UINT32 l1 = *(UINT32*)v;
                    if (l1 > 0) PrintF("  L1 Cache:             %u KB\n", l1 / 1024);
                }
                free(v);
            }
        }
        if (l2 <= 0) {
            DWORD l2v = 0; sz = sizeof(l2v);
            if (RegQueryValueExA(hKey, "L2Cache", NULL, &type, (LPBYTE)&l2v, &sz) == ERROR_SUCCESS)
                if (l2v > 0) PrintF("  L2 Cache:             %u KB\n", l2v / 1024);
        }
        if (l3 <= 0) {
            DWORD l3v = 0; sz = sizeof(l3v);
            if (RegQueryValueExA(hKey, "L3Cache", NULL, &type, (LPBYTE)&l3v, &sz) == ERROR_SUCCESS)
                if (l3v > 0) PrintF("  L3 Cache:             %u KB\n", l3v / 1024);
        }
        RegCloseKey(hKey);
    }

    /* TDP estimate based on processor */
    PrintF("  TDP:                 N/A (not exposed via WMI)\n");

    /* Architecture */
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    PrintF("  Architecture:        ");
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: PrintF("x64 (64-bit)\n"); break;
        case PROCESSOR_ARCHITECTURE_INTEL: PrintF("x86 (32-bit)\n"); break;
        case PROCESSOR_ARCHITECTURE_ARM64: PrintF("ARM64\n"); break;
        default: PrintF("Unknown\n"); break;
    }
}

/* ─── GPU Info ────────────────────────────────────────────────────── */

static void PrintGPU(IWbemServices *pSvc) {
    PrintHdr(C_DEFAULT, "GPU Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    IEnumWbemClassObject *pEnum = NULL;
    if (!WmiQuery(pSvc, L"SELECT * FROM Win32_VideoController", &pEnum)) {
        PrintF("  Failed to query GPU info.\n");
        return;
    }

    IWbemClassObject *pObj = NULL;
    BOOL found = FALSE;
    while (WmiNext(pEnum, &pObj)) {
        wchar_t name[1024] = L"", driver[256] = L"";
        wchar_t videoProc[512] = L"", modeDesc[512] = L"";
        ULONGLONG ram = 0;

        WmiGetStr(pObj, L"Name", name, 1024);
        WmiGetStr(pObj, L"DriverVersion", driver, 256);
        WmiGetStr(pObj, L"VideoProcessor", videoProc, 512);
        WmiGetStr(pObj, L"VideoModeDescription", modeDesc, 512);
        /* Read AdapterRAM - value is UInt32 in WMI */
        {
            VARIANT vt;
            VariantInit(&vt);
            if (SUCCEEDED(pObj->lpVtbl->Get(pObj, L"AdapterRAM", 0, &vt, NULL, NULL))) {
                if (vt.vt == VT_UI4) {
                    ram = vt.ulVal;
                } else if (vt.vt == VT_I4 && vt.lVal > 0) {
                    ram = (ULONGLONG)vt.lVal;
                } else if (vt.vt == VT_I4 && vt.lVal < 0) {
                    ram = (ULONGLONG)(unsigned long)vt.lVal;
                } else if (vt.vt == VT_BSTR && vt.bstrVal) {
                    ram = _wcstoui64(vt.bstrVal, NULL, 10);
                }
                VariantClear(&vt);
            }
        }
        if (ram > (1ULL << 40)) ram = 0;

        WORD clr = C_DEFAULT;
        if (wcsstr(name, L"NVIDIA") || wcsstr(name, L"GeForce"))
            clr = C_NVIDIA;
        else if (wcsstr(name, L"AMD") || wcsstr(name, L"ATI") ||
                 wcsstr(name, L"Radeon") || wcsstr(name, L"FirePro"))
            clr = C_AMD;
        else if (wcsstr(name, L"Intel") || wcsstr(name, L"HD Graphics") ||
                 wcsstr(name, L"Iris") || wcsstr(name, L"Arc"))
            clr = C_INTEL;

        char buf[1024], dbuf[256], pbuf[512], mbuf[512];
        w2a(name, buf, 1024);
        w2a(driver, dbuf, 256);
        w2a(videoProc, pbuf, 512);
        w2a(modeDesc, mbuf, 512);

        if (found) PrintF("\n");
        found = TRUE;
        PrintC(clr, "  GPU Model:           %s\n", buf);
        if (pbuf[0]) PrintF("  Video Processor:     %s\n", pbuf);
        if (ram > 0 && ram != 0xFFFFFFFFFFFFFFFFULL) {
            char ramBuf[64];
            fmtSize(ram, ramBuf, 64);
            PrintF("  VRAM:                %s\n", ramBuf);
        } else {
            PrintF("  VRAM:                N/A\n");
        }
        if (dbuf[0]) PrintF("  Driver Version:      %s\n", dbuf);
        if (mbuf[0]) PrintF("  Display Mode:        %s\n", mbuf);

        /* Get current refresh rate and resolution */
        DEVMODEA dm;
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm)) {
            PrintF("  Resolution:          %lu x %lu @ %lu Hz\n",
                   dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
        }

        pObj->lpVtbl->Release(pObj);
    }
    pEnum->lpVtbl->Release(pEnum);

    if (!found)
        PrintF("  No GPU found.\n");
}

/* ─── RAM Info ────────────────────────────────────────────────────── */

static void PrintRAM(IWbemServices *pSvc) {
    PrintHdr(C_YELLOW, "RAM Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    IEnumWbemClassObject *pEnum = NULL;
    if (!WmiQuery(pSvc, L"SELECT * FROM Win32_PhysicalMemory", &pEnum)) {
        PrintF("  Failed to query RAM info.\n");
        return;
    }

    int count = 0;
    ULONGLONG total = 0;
    int speed = 0, memType = 0;
    wchar_t manufacturer[256] = L"", partNum[256] = L"";

    IWbemClassObject *pObj = NULL;
    while (WmiNext(pEnum, &pObj)) {
        ULONGLONG cap = 0;
        int spd = 0, mt = 0;
        wchar_t man[256] = L"", pn[256] = L"";

        WmiGetLL(pObj, L"Capacity", &cap);
        WmiGetInt(pObj, L"Speed", &spd);
        WmiGetInt(pObj, L"SMBIOSMemoryType", &mt);
        WmiGetStr(pObj, L"Manufacturer", man, 256);
        WmiGetStr(pObj, L"PartNumber", pn, 256);

        char manBuf[256], pnBuf[256];
        w2a(man, manBuf, 256);
        w2a(pn, pnBuf, 256);

        if (strstr(pnBuf, "PartNumber") || !pnBuf[0])
            snprintf(pnBuf, sizeof(pnBuf), "N/A");
        if (strstr(manBuf, "Manufacturer") || !manBuf[0])
            snprintf(manBuf, sizeof(manBuf), "Unknown");

        char capBuf[64];
        fmtSize(cap, capBuf, 64);

        count++;
        total += cap;
        if (spd > speed) speed = spd;
        if (mt > memType) memType = mt;
        if (manufacturer[0] == 0) wcscpy(manufacturer, man);
        if (partNum[0] == 0) wcscpy(partNum, pn);

        PrintC(C_YELLOW, "  Stick %d:\n", count);
        PrintF("    Capacity:    %s\n", capBuf);
        PrintF("    Speed:       %d MHz\n", spd);
        PrintF("    Type:        %s\n", ddrType(mt));
        PrintF("    Manufacturer: %s\n", manBuf);
        PrintF("    Part Number: %s\n", pnBuf);

        pObj->lpVtbl->Release(pObj);
    }
    pEnum->lpVtbl->Release(pEnum);

    if (count == 0) {
        PrintF("  No RAM information found. Trying alternate method...\n");
        /* Try from OS info */
        pEnum = NULL;
        if (WmiQuery(pSvc, L"SELECT TotalVisibleMemorySize,FreePhysicalMemory FROM Win32_OperatingSystem", &pEnum)) {
            if (WmiNext(pEnum, &pObj)) {
                int totalMB = 0, freeMB = 0;
                WmiGetInt(pObj, L"TotalVisibleMemorySize", &totalMB);
                WmiGetInt(pObj, L"FreePhysicalMemory", &freeMB);
                if (totalMB > 0) {
                    char tb[64];
                    ULONGLONG t = (ULONGLONG)totalMB * 1024ULL;
                    fmtSize(t, tb, 64);
                    PrintC(C_YELLOW, "  Total RAM:   %s\n", tb);
                    fmtSize((ULONGLONG)freeMB * 1024ULL, tb, 64);
                    PrintF("  Free RAM:    %s\n", tb);
                }
                pObj->lpVtbl->Release(pObj);
            }
            pEnum->lpVtbl->Release(pEnum);
        }
        return;
    }

    char totalBuf[64];
    fmtSize(total, totalBuf, 64);
    char manBuf[256];
    w2a(manufacturer, manBuf, 256);
    if (strstr(manBuf, "Manufacturer") || !manBuf[0])
        snprintf(manBuf, sizeof(manBuf), "Mixed/Various");

    PrintC(C_YELLOW, "\n  RAM Summary:\n");
    PrintF("  Total Capacity:      %s\n", totalBuf);
    PrintF("  Modules Installed:   %d\n", count);
    PrintF("  Memory Type:         %s\n", ddrType(memType));
    PrintF("  Max Speed:           %d MHz\n", speed);
}

/* ─── Storage Info ────────────────────────────────────────────────── */

/* Try to get disk media type from Storage WMI namespace (Win8+) */
static const char *diskTypeFromStorage(const wchar_t *serial) {
    IWbemLocator *pLoc = NULL;
    IWbemServices *pSvc = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_WbemLocator, NULL, CLSCTX_INPROC_SERVER,
                                  &IID_IWbemLocator, (LPVOID*)&pLoc);
    if (FAILED(hr)) return NULL;
    hr = pLoc->lpVtbl->ConnectServer(pLoc, L"ROOT\\Microsoft\\Windows\\Storage",
                                      NULL, NULL, NULL, (LONG)0, NULL, NULL, &pSvc);
    pLoc->lpVtbl->Release(pLoc);
    if (FAILED(hr) || !pSvc) return NULL;

    wchar_t query[512];
    swprintf(query, 512, L"SELECT * FROM MSFT_PhysicalDisk WHERE SerialNumber='%s'", serial);
    IEnumWbemClassObject *pEnum = NULL;
    hr = pSvc->lpVtbl->ExecQuery(pSvc, L"WQL", (BSTR)query,
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, NULL, &pEnum);
    if (FAILED(hr)) { pSvc->lpVtbl->Release(pSvc); return NULL; }

    IWbemClassObject *pObj = NULL;
    ULONG ret = 0;
    const char *result = NULL;
    if (SUCCEEDED(pEnum->lpVtbl->Next(pEnum, WBEM_INFINITE, 1, &pObj, &ret)) && ret > 0) {
        int mediaType = 0;
        VARIANT vt;
        VariantInit(&vt);
        if (SUCCEEDED(pObj->lpVtbl->Get(pObj, L"MediaType", 0, &vt, NULL, NULL))) {
            if (vt.vt == VT_I4 || vt.vt == VT_UI4) {
                mediaType = (vt.vt == VT_I4) ? vt.lVal : (int)vt.ulVal;
                switch (mediaType) {
                    case 1: result = "HDD"; break;
                    case 2: result = "SSD"; break;
                    case 3: result = "SCM"; break;
                    case 4: result = "NVMe"; break;
                }
            }
            VariantClear(&vt);
        }
        pObj->lpVtbl->Release(pObj);
    }
    pEnum->lpVtbl->Release(pEnum);
    pSvc->lpVtbl->Release(pSvc);
    return result;
}

static void PrintROM(IWbemServices *pSvc) {
    PrintHdr(C_CYAN, "Storage Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    /* Physical drives */
    IEnumWbemClassObject *pEnum = NULL;
    if (!WmiQuery(pSvc, L"SELECT * FROM Win32_DiskDrive", &pEnum)) {
        PrintF("  Failed to query storage info.\n");
        return;
    }

    int diskNum = 0;
    IWbemClassObject *pObj = NULL;
    while (WmiNext(pEnum, &pObj)) {
        wchar_t model[512] = L"", iface[128] = L"", media[128] = L"";
        wchar_t serial[256] = L"";
        ULONGLONG size = 0;

        WmiGetStr(pObj, L"Model", model, 512);
        WmiGetStr(pObj, L"InterfaceType", iface, 128);
        WmiGetStr(pObj, L"MediaType", media, 128);
        WmiGetStr(pObj, L"SerialNumber", serial, 256);
        WmiGetLL(pObj, L"Size", &size);

        char modBuf[512], ifBuf[128], medBuf[128];
        w2a(model, modBuf, 512);
        w2a(iface, ifBuf, 128);
        w2a(media, medBuf, 128);

        char sizeBuf[64];
        fmtSize(size, sizeBuf, 64);

        const char *dtype = diskType(model, iface);
        /* Try Storage WMI namespace for more accurate type */
        if (serial[0]) {
            const char *storageType = diskTypeFromStorage(serial);
            if (storageType) dtype = storageType;
        }
        /* If media type says "Fixed hard disk media" it's HDD/SSD */
        if (strstr(medBuf, "SSD") || strstr(medBuf, "Solid"))
            dtype = "SSD";

        diskNum++;
        PrintC(C_CYAN, "  Disk %d:\n", diskNum);
        PrintF("    Model:         %s\n", modBuf);
        PrintF("    Type:          %s\n", dtype);
        PrintF("    Interface:     %s\n", ifBuf);
        if (size > 0)
            PrintF("    Total Size:    %s\n", sizeBuf);

        {
            char sBuf[256];
            w2a(serial, sBuf, 256);
            char *s = sBuf;
            while (*s == ' ') s++;
            if (s[0])
                PrintF("    Serial:        %s\n", s);
        }

        pObj->lpVtbl->Release(pObj);
    }
    pEnum->lpVtbl->Release(pEnum);

    /* Logical drives */
    pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT * FROM Win32_LogicalDisk", &pEnum)) {
        PrintC(C_CYAN, "\n  Volumes:\n");
        BOOL volFound = FALSE;
        while (WmiNext(pEnum, &pObj)) {
            wchar_t drive[8] = L"", fs[32] = L"";
            ULONGLONG totalS = 0, freeS = 0;

            WmiGetStr(pObj, L"DeviceID", drive, 8);
            WmiGetStr(pObj, L"FileSystem", fs, 32);
            WmiGetLL(pObj, L"Size", &totalS);
            WmiGetLL(pObj, L"FreeSpace", &freeS);

            char dBuf[8], fsBuf[32], totalBuf[64], freeBuf[64], usedBuf[64];
            w2a(drive, dBuf, 8);
            w2a(fs, fsBuf, 32);
            fmtSize(totalS, totalBuf, 64);
            fmtSize(freeS, freeBuf, 64);

            ULONGLONG usedS = totalS - freeS;
            fmtSize(usedS, usedBuf, 64);

            int pct = totalS > 0 ? (int)(usedS * 100 / totalS) : 0;

            volFound = TRUE;
            PrintF("    %-4s  %-8s  Total: %-10s  Used: %-10s  Free: %-10s  %d%%\n",
                   dBuf, fsBuf, totalBuf, usedBuf, freeBuf, pct);

            pObj->lpVtbl->Release(pObj);
        }
        if (!volFound) PrintF("    No volumes found.\n");
        pEnum->lpVtbl->Release(pEnum);
    }

    if (diskNum == 0)
        PrintF("  No physical disks found.\n");
}

/* ─── Motherboard Info ────────────────────────────────────────────── */

static void PrintMB(IWbemServices *pSvc) {
    PrintHdr(C_PURPLE, "Motherboard Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    IEnumWbemClassObject *pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT * FROM Win32_BaseBoard", &pEnum)) {
        IWbemClassObject *pObj = NULL;
        if (WmiNext(pEnum, &pObj)) {
            wchar_t manufacturer[256] = L"", product[256] = L"";
            wchar_t version[256] = L"", serial[256] = L"";

            WmiGetStr(pObj, L"Manufacturer", manufacturer, 256);
            WmiGetStr(pObj, L"Product", product, 256);
            WmiGetStr(pObj, L"Version", version, 256);
            WmiGetStr(pObj, L"SerialNumber", serial, 256);

            char mbuf[256], pbuf[256], vbuf[256], sbuf[256];
            w2a(manufacturer, mbuf, 256);
            w2a(product, pbuf, 256);
            w2a(version, vbuf, 256);
            w2a(serial, sbuf, 256);

            PrintC(C_PURPLE, "  Manufacturer:  %s\n", mbuf);
            PrintF("  Model:         %s\n", pbuf);
            PrintF("  Version:       %s\n", vbuf);
            if (sbuf[0] && !strstr(sbuf, "Serial") && !strstr(sbuf, "To be"))
                PrintF("  Serial:        %s\n", sbuf);

            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    /* BIOS info */
    pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT * FROM Win32_BIOS", &pEnum)) {
        IWbemClassObject *pObj = NULL;
        if (WmiNext(pEnum, &pObj)) {
            wchar_t biosVer[256] = L"", biosDate[256] = L"";
            wchar_t biosMan[256] = L"";

            WmiGetStr(pObj, L"SMBIOSBIOSVersion", biosVer, 256);
            WmiGetStr(pObj, L"ReleaseDate", biosDate, 256);
            WmiGetStr(pObj, L"Manufacturer", biosMan, 256);

            char vbuf[256], dbuf[64], mbuf[256];
            w2a(biosVer, vbuf, 256);
            w2a(biosMan, mbuf, 256);

            PrintF("  BIOS Vendor:   %s\n", mbuf);
            PrintF("  BIOS Version:  %s\n", vbuf);
            if (biosDate[0]) {
                parseDmtfDate(biosDate, dbuf, 64);
                PrintF("  BIOS Date:     %s\n", dbuf);
            }

            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    /* Socket from Win32_Processor */
    pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT SocketDesignation FROM Win32_Processor", &pEnum)) {
        IWbemClassObject *pObj = NULL;
        if (WmiNext(pEnum, &pObj)) {
            wchar_t socket[256] = L"";
            WmiGetStr(pObj, L"SocketDesignation", socket, 256);
            char sbuf[256];
            w2a(socket, sbuf, 256);
            PrintF("  CPU Socket:    %s\n", sbuf);
            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    /* Form factor */
    pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT * FROM Win32_SystemEnclosure", &pEnum)) {
        IWbemClassObject *pObj = NULL;
        if (WmiNext(pEnum, &pObj)) {
            int chassisType = 0;
            if (WmiGetInt(pObj, L"ChassisTypes", &chassisType)) {
                const char *typeStr = "Unknown";
                switch (chassisType) {
                    case 3: case 4: case 5: case 6: typeStr = "Desktop"; break;
                    case 8: case 9: case 10: typeStr = "Laptop"; break;
                    case 23: typeStr = "Server"; break;
                    case 30: typeStr = "Tablet"; break;
                    case 31: typeStr = "Convertible"; break;
                    case 32: typeStr = "Detachable"; break;
                }
                PrintF("  Form Factor:   %s\n", typeStr);
            }
            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }
}

/* ─── OS Info ─────────────────────────────────────────────────────── */

static void PrintOS(IWbemServices *pSvc) {
    PrintHdr(C_WHITE, "Operating System Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    IEnumWbemClassObject *pEnum = NULL;
    if (!WmiQuery(pSvc, L"SELECT * FROM Win32_OperatingSystem", &pEnum)) {
        PrintF("  Failed to query OS info.\n");
        return;
    }

    IWbemClassObject *pObj = NULL;
    if (!WmiNext(pEnum, &pObj)) {
        PrintF("  No OS data found.\n");
        pEnum->lpVtbl->Release(pEnum);
        return;
    }

    wchar_t caption[512] = L"", version[64] = L"", build[64] = L"";
    wchar_t arch[32] = L"", osDir[512] = L"";
    int totalMem = 0, freeMem = 0;

    WmiGetStr(pObj, L"Caption", caption, 512);
    WmiGetStr(pObj, L"Version", version, 64);
    WmiGetStr(pObj, L"BuildNumber", build, 64);
    WmiGetStr(pObj, L"OSArchitecture", arch, 32);
    WmiGetStr(pObj, L"SystemDirectory", osDir, 512);
    WmiGetInt(pObj, L"TotalVisibleMemorySize", &totalMem);
    WmiGetInt(pObj, L"FreePhysicalMemory", &freeMem);

    char capBuf[512], verBuf[64], bldBuf[64], archBuf[32];
    w2a(caption, capBuf, 512);
    w2a(version, verBuf, 64);
    w2a(build, bldBuf, 64);
    w2a(arch, archBuf, 32);

    /* Clean caption */
    char *osName = capBuf;
    if (strstr(osName, "Microsoft ")) osName += 10;

    PrintC(C_WHITE, "  OS Name:        %s\n", osName);
    PrintF("  Version:        %s (Build %s)\n", verBuf, bldBuf);
    PrintF("  Architecture:   %s\n", archBuf);

    if (totalMem > 0) {
        char tb[64], fb[64];
        ULONGLONG t = (ULONGLONG)totalMem * 1024ULL;
        ULONGLONG f = (ULONGLONG)freeMem * 1024ULL;
        fmtSize(t, tb, 64);
        fmtSize(f, fb, 64);
        PrintF("  Total RAM:      %s\n", tb);
        PrintF("  Free RAM:       %s\n", fb);
    }

    /* System directory */
    PrintF("  System Dir:     %s\n", osDir);

    pObj->lpVtbl->Release(pObj);
    pEnum->lpVtbl->Release(pEnum);

    /* Computer name */
    wchar_t compName[MAX_COMPUTERNAME_LENGTH + 1] = L"";
    DWORD compSize = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(compName, &compSize)) {
        char cBuf[64];
        w2a(compName, cBuf, 64);
        PrintF("  Computer Name:  %s\n", cBuf);
    }

    /* User name */
    wchar_t userName[256] = L"";
    DWORD userSize = 256;
    if (GetUserNameW(userName, &userSize)) {
        char uBuf[256];
        w2a(userName, uBuf, 256);
        PrintF("  User Name:      %s\n", uBuf);
    }

    /* Boot time */
    pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT LastBootUpTime FROM Win32_OperatingSystem", &pEnum)) {
        if (WmiNext(pEnum, &pObj)) {
            wchar_t bootTime[64] = L"";
            if (WmiGetStr(pObj, L"LastBootUpTime", bootTime, 64)) {
                char bootBuf[64];
                parseDmtfDate(bootTime, bootBuf, 64);
                PrintF("  Last Boot:      %s\n", bootBuf);
            }
            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }
}

/* ─── Temperature Info ────────────────────────────────────────────── */

static void PrintTemp(IWbemServices *pSvc) {
    PrintHdr(C_RED, "Temperature Information");
    if (!pSvc) { PrintF("  WMI not available.\n"); return; }

    IEnumWbemClassObject *pEnum = NULL;
    BOOL found = FALSE;

    /* Try MSAcpi_ThermalZoneTemperature */
    if (WmiQuery(pSvc, L"SELECT * FROM MSAcpi_ThermalZoneTemperature", &pEnum)) {
        IWbemClassObject *pObj = NULL;
        while (WmiNext(pEnum, &pObj)) {
            wchar_t name[256] = L"";
            int temp = 0;

            WmiGetStr(pObj, L"InstanceName", name, 256);
            WmiGetInt(pObj, L"CurrentTemperature", &temp);

            char nbuf[256];
            w2a(name, nbuf, 256);

            /* Temperature is in tenths of Kelvin */
            if (temp > 0) {
                double celsius = (temp / 10.0) - 273.15;
                WORD clr = celsius > 70.0 ? C_RED : (celsius > 50.0 ? C_ORANGE : C_DEFAULT);
                PrintC(clr, "  Thermal Zone:   %s\n", nbuf);
                PrintF("  Temperature:    %.1f C\n", celsius);
                found = TRUE;
            }
            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    /* Try Win32_TemperatureProbe */
    pEnum = NULL;
    if (WmiQuery(pSvc, L"SELECT * FROM Win32_TemperatureProbe", &pEnum)) {
        IWbemClassObject *pObj = NULL;
        while (WmiNext(pEnum, &pObj)) {
            wchar_t name[256] = L"";
            int currentTemp = 0, maxTemp = 0, minTemp = 0;

            WmiGetStr(pObj, L"Name", name, 256);
            WmiGetInt(pObj, L"CurrentReading", &currentTemp);

            char nbuf[256];
            w2a(name, nbuf, 256);

            if (currentTemp > 0) {
                double celsius = currentTemp / 10.0;
                WORD clr = celsius > 70.0 ? C_RED : (celsius > 50.0 ? C_ORANGE : C_DEFAULT);
                PrintC(clr, "  Probe:          %s\n", nbuf);
                PrintF("  Temperature:    %.1f C\n", celsius);
                found = TRUE;
            }
            pObj->lpVtbl->Release(pObj);
        }
        pEnum->lpVtbl->Release(pEnum);
    }

    if (!found) {
        PrintC(C_RED, "  Temperature monitoring is not available.\n");
        PrintF("  This feature requires:\n");
        PrintF("    - ACPI thermal zone support in BIOS\n");
        PrintF("    - Or Open Hardware Monitor (running as administrator)\n");
        PrintF("    - Or compatible hardware sensors\n");
    }
}

/* ─── Benchmark ───────────────────────────────────────────────────── */

typedef struct {
    int limit;
    int start;
    int step;
    int count;
    double elapsed;
} BenchData;

static DWORD WINAPI BenchThread(LPVOID param) {
    BenchData *bd = (BenchData*)param;
    int cnt = 0;
    DWORD t0 = GetTickCount();
    for (int i = bd->start; i <= bd->limit; i += bd->step) {
        int isPrime = 1;
        if (i < 2) isPrime = 0;
        else {
            for (int j = 2; j * j <= i; j++) {
                if (i % j == 0) { isPrime = 0; break; }
            }
        }
        if (isPrime) cnt++;
    }
    bd->elapsed = (double)(GetTickCount() - t0) / 1000.0;
    bd->count = cnt;
    return 0;
}

static void RunBenchmark(void) {
    PrintHdr(C_WHITE, "CPU Benchmark");

    SYSTEM_INFO si;
    GetSystemInfo(&si);
    DWORD numThreads = si.dwNumberOfProcessors;
    if (numThreads < 1) numThreads = 1;

    PrintF("  Logical processors: %lu\n", numThreads);

    /* Single-thread benchmark */
    PrintF("\n  Running single-thread benchmark... ");
    BenchData bd;
    bd.limit = 800000;
    bd.start = 2;
    bd.step = 1;
    bd.count = 0;
    bd.elapsed = 0;

    DWORD t0 = GetTickCount();
    HANDLE hThread = CreateThread(NULL, 0, BenchThread, &bd, 0, NULL);
    if (hThread) {
        WaitForSingleObject(hThread, INFINITE);
        CloseHandle(hThread);
    }
    DWORD t1 = GetTickCount();
    double stTime = (double)(t1 - t0) / 1000.0;
    double stScore = stTime > 0 ? (double)bd.count / stTime : 0;

    PrintF("Done (%.3f sec)\n", stTime > 0 ? stTime : bd.elapsed);
    PrintF("  Single-core score:  %.0f primes/sec\n", stScore);
    PrintF("  Primes found:       %d (up to %d)\n", bd.count, bd.limit);

    /* Multi-thread benchmark */
    PrintF("\n  Running multi-thread benchmark (%lu threads)... ", numThreads);

    HANDLE *threads = (HANDLE*)malloc(sizeof(HANDLE) * numThreads);
    BenchData *data = (BenchData*)malloc(sizeof(BenchData) * numThreads);
    if (!threads || !data) {
        PrintF("Memory allocation failed.\n");
        free(threads); free(data);
        return;
    }

    int limit = 800000;
    t0 = GetTickCount();
    for (DWORD i = 0; i < numThreads; i++) {
        data[i].limit = limit;
        data[i].start = 2 + i;
        data[i].step = numThreads;
        data[i].count = 0;
        data[i].elapsed = 0;
        threads[i] = CreateThread(NULL, 0, BenchThread, &data[i], 0, NULL);
    }
    int totalPrimes = 0;
    double maxTime = 0;
    for (DWORD i = 0; i < numThreads; i++) {
        WaitForSingleObject(threads[i], INFINITE);
        CloseHandle(threads[i]);
        totalPrimes += data[i].count;
        if (data[i].elapsed > maxTime) maxTime = data[i].elapsed;
    }
    t1 = GetTickCount();
    double mtTime = (double)(t1 - t0) / 1000.0;
    double mtScore = mtTime > 0 ? (double)totalPrimes / mtTime : 0;

    PrintF("Done (%.3f sec)\n", mtTime);
    PrintF("  Multi-core score:   %.0f primes/sec\n", mtScore);
    PrintF("  Total primes:       %d (up to %d)\n", totalPrimes, limit);
    PrintF("  Speedup:            %.2fx\n", stScore > 0 ? mtScore / stScore : 0);
    PrintF("  Efficiency:         %.1f%%\n", stTime > 0 && mtTime > 0 ?
        (mtScore / (stScore * numThreads)) * 100.0 : 0);

    free(threads);
    free(data);
}

/* ─── All Info ────────────────────────────────────────────────────── */

static void PrintAll(IWbemServices *pSvc) {
    PrintCPU(pSvc);
    PrintGPU(pSvc);
    PrintRAM(pSvc);
    PrintROM(pSvc);
    PrintMB(pSvc);
    PrintOS(pSvc);
    PrintTemp(pSvc);
}

/* ─── Help & Version ──────────────────────────────────────────────── */

static void PrintVersion(void) {
    PrintF("CPU Information v0.1\n");
    PrintF("Developed by ExEintel\n");
    PrintF("Platform: Windows\n");
}

static void PrintHelp(void) {
    PrintF("CPU Information v0.1 - Console System Information Utility\n");
    PrintF("Developed by ExEintel\n");
    PrintF("\n");
    PrintF("Usage:\n");
    PrintF("  cpuinformation.exe [options]\n");
    PrintF("  cpuinformation.cmd [options]\n");
    PrintF("\n");
    PrintF("Options:\n");
    PrintF("  -i, -all         Show all system information\n");
    PrintF("  -cpu             CPU information\n");
    PrintF("  -gpu             GPU information\n");
    PrintF("  -ram             RAM information\n");
    PrintF("  -rom             Storage (disk) information\n");
    PrintF("  -mb              Motherboard information\n");
    PrintF("  -os              Operating system information\n");
    PrintF("  -temp            Component temperatures\n");
    PrintF("  -bench           CPU performance benchmark\n");
    PrintF("  -export <file>   Export output to text file\n");
    PrintF("  -h, --help       Show this help\n");
    PrintF("  -v, --version    Show version\n");
    PrintF("\n");
    PrintF("Examples:\n");
    PrintF("  cpuinformation.exe -i\n");
    PrintF("  cpuinformation.exe -cpu -gpu -ram\n");
    PrintF("  cpuinformation.exe -temp -bench\n");
    PrintF("  cpuinformation.exe -export report.txt\n");
}

/* ─── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    /* Enable UTF-8 console */
    SetConsoleOutputCP(CP_UTF8);

    /* Get console handle */
    g_hConsole = GetStdHandle(STD_OUTPUT_HANDLE);

    /* No args = show help */
    if (argc < 2) {
        PrintHelp();
        return 0;
    }

    /* Check for -export first */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-export") == 0) {
            if (i + 1 < argc) {
                strncpy(g_exportName, argv[i + 1], 259);
                g_exportName[259] = 0;
                g_exportFile = fopen(g_exportName, "w");
                if (g_exportFile) {
                    time_t t = time(NULL);
                    struct tm *tm = localtime(&t);
                    char ts[64];
                    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm);
                    fprintf(g_exportFile, "CPU Information Report\n");
                    fprintf(g_exportFile, "Generated: %s\n", ts);
                    fprintf(g_exportFile, "============================================================\n");
                    g_export = TRUE;
                } else {
                    printf("Warning: Could not open file '%s' for writing.\n", argv[i + 1]);
                }
            }
            break;
        }
    }

    /* Check for -v / --version */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            PrintVersion();
            goto cleanup;
        }
    }

    /* Check for -h / --help */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            PrintHelp();
            goto cleanup;
        }
    }

    /* Initialize WMI */
    IWbemServices *pSvc = WmiInit();
    if (!pSvc)
        PrintF("Warning: WMI initialization failed. Some features may not work.\n");

    /* Track if any action was performed */
    BOOL acted = FALSE;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "-all") == 0 ||
            strcmp(argv[i], "-all") == 0) {
            PrintAll(pSvc);
            acted = TRUE;
            break;  /* -i alone, don't process other flags */
        }
    }

    if (!acted) {
        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "-export") == 0) { i++; continue; }
            if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) continue;
            if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) continue;

            if (strcmp(argv[i], "-cpu") == 0)   { PrintCPU(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-gpu") == 0)  { PrintGPU(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-ram") == 0)  { PrintRAM(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-rom") == 0)  { PrintROM(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-mb") == 0)   { PrintMB(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-os") == 0)   { PrintOS(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-temp") == 0) { PrintTemp(pSvc); acted = TRUE; }
            else if (strcmp(argv[i], "-bench") == 0){ RunBenchmark(); acted = TRUE; }
        }
    }

    if (!acted) {
        PrintF("\nNo valid options specified. Use -h or --help for usage.\n");
    }

    WmiDone(pSvc);

cleanup:
    if (g_exportFile) {
        fprintf(g_exportFile, "\n============================================================\n");
        fprintf(g_exportFile, "End of report.\n");
        fclose(g_exportFile);
        g_exportFile = NULL;
        if (g_exportName[0])
            PrintF("\nReport saved to: %s\n", g_exportName);
    }

    return 0;
}
