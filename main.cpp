// language: C++17, file: main.cpp, app: yieldboard — defi APY dashboard
// Windows 11, MSVC, WinAPI + WinHTTP. No external dependencies.
// Fetches top yield pools from the public DefiLlama API (no key) and shows
// them ranked by APY with chain and TVL. Refresh button re-queries.
#include <windows.h>
#include <winhttp.h>
#include <commctrl.h>
#include <string>
#include <vector>
#include <algorithm>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "comctl32.lib")

struct Pool { std::wstring project, symbol, chain; double apy, tvl; };

static HWND g_list, g_note;
static std::vector<Pool> g_pools;

// Tiny JSON scanner — enough for llama.fi pools (big, but fields repeat).
static void ParsePools(const std::string& json, std::vector<Pool>& out) {
    size_t p = 0;
    while ((p = json.find("\"project\":\"", p)) != std::string::npos) {
        Pool pool;
        p += 11;
        size_t e = json.find('"', p);
        pool.project.assign(json.begin() + p, json.begin() + e);
        auto field = [&](const char* key, size_t from) -> std::string {
            size_t k = json.find(key, from);
            if (k == std::string::npos) return "";
            k += strlen(key);
            return json.substr(k, json.find('"', k) - k);
        };
        std::string sym = field("\"symbol\":\"", e);
        std::string ch  = field("\"chain\":\"", e);
        pool.symbol.assign(sym.begin(), sym.end());
        pool.chain.assign(ch.begin(), ch.end());
        size_t a = json.find("\"apy\":", e);
        if (a != std::string::npos) pool.apy = std::stod(json.substr(a + 6, json.find_first_of(",}", a + 6) - (a + 6)));
        size_t t = json.find("\"tvlUsd\":", e);
        if (t != std::string::npos) pool.tvl = std::stod(json.substr(t + 9, json.find_first_of(",}", t + 9) - (t + 9)));
        out.push_back(pool);
        p = e;
        if (out.size() >= 200) break;
    }
}

static std::string HttpsGet(const std::wstring& host, const std::wstring& path) {
    std::string out;
    HINTERNET sess = WinHttpOpen(L"yieldboard/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                 WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!sess) return out;
    HINTERNET conn = WinHttpConnect(sess, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (conn) {
        HINTERNET req = WinHttpOpenRequest(conn, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
        if (req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                      WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
                && WinHttpReceiveResponse(req, nullptr)) {
            DWORD bytes = 0; char buf[65536];
            for (;;) {
                if (!WinHttpReadData(req, buf, sizeof(buf), &bytes) || bytes == 0) break;
                out.append(buf, bytes);
            }
        }
        if (req) WinHttpCloseHandle(req);
        WinHttpCloseHandle(conn);
    }
    WinHttpCloseHandle(sess);
    return out;
}

static DWORD WINAPI Fetch(LPVOID) {
    std::string body = HttpsGet(L"yields.llama.fi", L"/pools");
    if (!body.empty()) {
        std::vector<Pool> pools;
        ParsePools(body, pools);
        // keep only healthy TVL, sort by APY desc
        pools.erase(std::remove_if(pools.begin(), pools.end(),
                      [](const Pool& x){ return x.tvl < 1000000 || x.apy <= 0; }), pools.end());
        for (size_t i = 1; i < pools.size(); ++i)
            for (size_t j = i; j > 0 && pools[j].apy > pools[j-1].apy; --j)
                std::swap(pools[j], pools[j-1]);
        if (pools.size() > 50) pools.resize(50);
        g_pools = pools;
        PostMessage(g_list, WM_APP, 0, 0);
    }
    return 0;
}

static LRESULT CALLBACK WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES };
        InitCommonControlsEx(&icc);
        g_list = CreateWindowExW(0, WC_LISTVIEWW, nullptr,
                                 WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL,
                                 16, 16, 740, 400, w, (HMENU)1, nullptr, nullptr);
        LVCOLUMNW col{}; col.mask = LVCF_TEXT | LVCF_WIDTH;
        const wchar_t* heads[] = { L"Chain", L"Project", L"Asset", L"APY %", L"TVL $" };
        int widths[] = { 120, 200, 120, 100, 180 };
        for (int i = 0; i < 5; ++i) {
            col.pszText = (LPWSTR)heads[i]; col.cx = widths[i];
            ListView_InsertColumn(g_list, i, &col);
        }
        g_note = CreateWindowW(L"STATIC", L"source: DefiLlama public API | no key needed",
                               WS_CHILD | WS_VISIBLE, 16, 424, 500, 22, w, nullptr, nullptr, nullptr);
        CreateThread(nullptr, 0, Fetch, nullptr, 0, nullptr);
        return 0;
    }
    case WM_APP: {
        ListView_DeleteAllItems(g_list);
        LVITEMW it{}; it.mask = LVIF_TEXT;
        wchar_t buf[128];
        for (size_t i = 0; i < g_pools.size(); ++i) {
            it.iItem = (int)i; it.pszText = (LPWSTR)g_pools[i].chain.c_str();
            it.iItem = ListView_InsertItem(g_list, &it);
            ListView_SetItemText(g_list, it.iItem, 1, (LPWSTR)g_pools[i].project.c_str());
            ListView_SetItemText(g_list, it.iItem, 2, (LPWSTR)g_pools[i].symbol.c_str());
            swprintf(buf, 128, L"%.2f", g_pools[i].apy);
            ListView_SetItemText(g_list, it.iItem, 3, buf);
            swprintf(buf, 128, L"%.0f", g_pools[i].tvl);
            ListView_SetItemText(g_list, it.iItem, 4, buf);
        }
        return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(w, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc; wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(13, 17, 23));
    wc.lpszClassName = L"YieldBoardWnd";
    RegisterClassW(&wc);
    HWND w = CreateWindowExW(0, L"YieldBoardWnd", L"YieldBoard — defi APY dashboard",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 790, 500,
                             nullptr, nullptr, inst, nullptr);
    ShowWindow(w, show);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    return 0;
}
