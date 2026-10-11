#include "panel.h"
#include <objbase.h>
#include <algorithm>
#include <shlwapi.h>
#include <wrl/client.h>
#include "WebView2Uuids.h"
#include "text.h"

namespace {

constexpr wchar_t kClass[] = L"MarkdownLivePanel";
constexpr wchar_t kAssetsHost[] = L"mdlive.assets";
constexpr wchar_t kDocHost[] = L"mdlive.doc";
constexpr UINT kDmnClose = 1050 + 1;   // DMN_CLOSE

// Adapter COM: lambda jako uchwyt zdarzenia WebView2.
template <class Iface, class... Args>
class Handler final : public Iface {
public:
    explicit Handler(std::function<HRESULT(Args...)> fn) : fn_(std::move(fn)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (IsEqualIID(riid, __uuidof(Iface)) || IsEqualIID(riid, __uuidof(IUnknown))) {
            *out = static_cast<Iface*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs_;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(Args... args) override { return fn_(args...); }

private:
    std::function<HRESULT(Args...)> fn_;
    ULONG refs_ = 1;
};

template <class Iface, class... Args, class F>
Iface* handler(F&& f) { return new Handler<Iface, Args...>(std::forward<F>(f)); }

using CreateEnvFn = HRESULT(STDAPICALLTYPE*)(PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
                                             ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

} // namespace

bool Panel::create(HINSTANCE inst, HWND parent, std::wstring pluginDir, std::wstring userDataDir) {
    inst_ = inst;
    pluginDir_ = std::move(pluginDir);
    userDataDir_ = std::move(userDataDir);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc{};
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassW(&wc);

    brush_ = CreateSolidBrush(bg_);
    hwnd_ = CreateWindowExW(0, kClass, L"Markdown", WS_CHILD | WS_CLIPCHILDREN, 0, 0, 100, 100, parent, nullptr, inst, this);
    return hwnd_ != nullptr;
}

void Panel::setBackground(COLORREF c) {
    if (c == bg_) return;
    bg_ = c;
    if (brush_) DeleteObject(brush_);
    brush_ = CreateSolidBrush(c);
    if (ctl_) {
        Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctl2;
        if (SUCCEEDED(ctl_->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
            ctl2->put_DefaultBackgroundColor({255, GetRValue(c), GetGValue(c), GetBValue(c)});
        }
    }
    InvalidateRect(hwnd_, nullptr, TRUE);
}

void Panel::post(const std::wstring& key, const std::wstring& json) {
    if (ready_ && wv_) {
        wv_->PostWebMessageAsJson(json.c_str());
        return;
    }
    for (auto& item : queue_) {
        if (item.first == key) { item.second = json; return; }
    }
    queue_.emplace_back(key, json);
}

void Panel::flush() {
    for (auto& item : queue_) wv_->PostWebMessageAsJson(item.second.c_str());
    queue_.clear();
}

void Panel::mapDocFolder(const std::wstring& dir) {
    if (dir == docDir_) return;
    docDir_ = dir;
    Microsoft::WRL::ComPtr<ICoreWebView2_3> wv3;
    if (!wv_ || FAILED(wv_->QueryInterface(IID_PPV_ARGS(&wv3)))) return;
    wv3->ClearVirtualHostNameToFolderMapping(kDocHost);
    if (!dir.empty()) {
        wv3->SetVirtualHostNameToFolderMapping(kDocHost, dir.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
    }
}

// Tło samego WebView (pod stroną) - na czas druku białe, inaczej ciemny motyw przebija na marginesach stron.
void Panel::setPrintBackground(bool printing) {
    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctl2;
    if (!ctl_ || FAILED(ctl_->QueryInterface(IID_PPV_ARGS(&ctl2)))) return;
    COLORREF c = printing ? RGB(255, 255, 255) : bg_;
    ctl2->put_DefaultBackgroundColor({255, GetRValue(c), GetGValue(c), GetBValue(c)});
}

void Panel::printToPdf(const std::wstring& path, std::function<void(bool)> done) {
    Microsoft::WRL::ComPtr<ICoreWebView2_7> wv7;
    if (!wv_ || FAILED(wv_->QueryInterface(IID_PPV_ARGS(&wv7)))) { done(false); return; }

    // Marginesy, tła (kolory bloków), bez daty i adresu w nagłówku i stopce; A4 albo Letter wg regionu Windows.
    Microsoft::WRL::ComPtr<ICoreWebView2PrintSettings> settings;
    Microsoft::WRL::ComPtr<ICoreWebView2Environment6> env6;
    if (env_ && SUCCEEDED(env_->QueryInterface(IID_PPV_ARGS(&env6))) && SUCCEEDED(env6->CreatePrintSettings(&settings))) {
        DWORD paper = 9;   // 1 = Letter, 9 = A4
        GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IPAPERSIZE | LOCALE_RETURN_NUMBER, (LPWSTR)&paper, sizeof(paper) / sizeof(wchar_t));
        bool letter = paper == 1;
        settings->put_PageWidth(letter ? 8.5 : 8.27);
        settings->put_PageHeight(letter ? 11.0 : 11.69);
        settings->put_MarginTop(0.6);
        settings->put_MarginBottom(0.6);
        settings->put_MarginLeft(0.6);
        settings->put_MarginRight(0.6);
        settings->put_ShouldPrintBackgrounds(TRUE);
        settings->put_ShouldPrintHeaderAndFooter(FALSE);
    }
    setPrintBackground(true);
    wv7->PrintToPdf(path.c_str(), settings.Get(),
                    handler<ICoreWebView2PrintToPdfCompletedHandler, HRESULT, BOOL>([this, done](HRESULT hr, BOOL ok) -> HRESULT {
                        setPrintBackground(false);
                        done(SUCCEEDED(hr) && ok);
                        return S_OK;
                    }));
}

void Panel::startWebView() {
    if (started_) return;
    started_ = true;

    HMODULE loader = LoadLibraryW((pluginDir_ + L"\\WebView2Loader.dll").c_str());
    auto create = loader ? (CreateEnvFn)GetProcAddress(loader, "CreateCoreWebView2EnvironmentWithOptions") : nullptr;
    if (!create) return;

    auto onEnv = [this](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
        if (FAILED(hr) || !env) return hr;
        env_ = env;
        env->AddRef();
        return env->CreateCoreWebView2Controller(hwnd_, handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
            [this](HRESULT hr2, ICoreWebView2Controller* ctl) -> HRESULT {
                if (SUCCEEDED(hr2) && ctl) onControllerReady(ctl);
                return S_OK;
            }));
    };
    create(nullptr, userDataDir_.c_str(), nullptr,
           handler<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, HRESULT, ICoreWebView2Environment*>(onEnv));
}

void Panel::onControllerReady(ICoreWebView2Controller* ctl) {
    ctl_ = ctl;
    ctl->AddRef();
    ctl->get_CoreWebView2(&wv_);

    Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
    wv_->get_Settings(&settings);
    settings->put_AreDevToolsEnabled(FALSE);
    settings->put_IsStatusBarEnabled(FALSE);
    settings->put_IsZoomControlEnabled(FALSE);   // Ctrl+kółko powiększa tylko treść (obsługa w preview.js)
    Microsoft::WRL::ComPtr<ICoreWebView2Settings3> s3;
    if (SUCCEEDED(settings.As(&s3))) s3->put_AreBrowserAcceleratorKeysEnabled(FALSE);

    Microsoft::WRL::ComPtr<ICoreWebView2Controller2> ctl2;
    if (SUCCEEDED(ctl->QueryInterface(IID_PPV_ARGS(&ctl2)))) {
        ctl2->put_DefaultBackgroundColor({255, GetRValue(bg_), GetGValue(bg_), GetBValue(bg_)});
    }

    Microsoft::WRL::ComPtr<ICoreWebView2_3> wv3;
    if (SUCCEEDED(wv_->QueryInterface(IID_PPV_ARGS(&wv3)))) {
        wv3->SetVirtualHostNameToFolderMapping(kAssetsHost, (pluginDir_ + L"\\assets").c_str(),
                                               COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        if (!docDir_.empty()) {
            wv3->SetVirtualHostNameToFolderMapping(kDocHost, docDir_.c_str(), COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
        // Obrazki z pełnymi ścieżkami (C:\...) - każdy dysk jako drive-c.mdlive itd.
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 26; i++) {
            if (!(drives & (1u << i))) continue;
            wchar_t hostName[] = L"drive-a.mdlive", folder[] = L"A:\\";
            hostName[6] = (wchar_t)(L'a' + i);
            folder[0] = (wchar_t)(L'A' + i);
            wv3->SetVirtualHostNameToFolderMapping(hostName, folder, COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS);
        }
    }

    EventRegistrationToken tok;
    wv_->add_WebMessageReceived(
        handler<ICoreWebView2WebMessageReceivedEventHandler, ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*>(
            [this](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                LPWSTR raw = nullptr;
                if (FAILED(args->TryGetWebMessageAsString(&raw)) || !raw) return S_OK;
                std::wstring msg = raw;
                CoTaskMemFree(raw);
                size_t tab = msg.find(L'\t');
                std::wstring cmd = msg.substr(0, tab);
                std::wstring arg = tab == std::wstring::npos ? L"" : msg.substr(tab + 1);
                if (cmd == L"ready") {
                    ready_ = true;
                    flush();
                }
                if (onMessage) onMessage(cmd, arg);
                return S_OK;
            }),
        &tok);

    // Strona ma się nie nawigować; linki obsługuje wtyczka.
    wv_->add_NavigationStarting(
        handler<ICoreWebView2NavigationStartingEventHandler, ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
                LPWSTR uri = nullptr;
                args->get_Uri(&uri);
                bool own = uri && std::wstring(uri).rfind(L"https://mdlive.assets/", 0) == 0;
                if (uri) CoTaskMemFree(uri);
                if (!own) args->put_Cancel(TRUE);
                return S_OK;
            }),
        &tok);
    wv_->add_NewWindowRequested(
        handler<ICoreWebView2NewWindowRequestedEventHandler, ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs*>(
            [](ICoreWebView2*, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
                args->put_Handled(TRUE);
                return S_OK;
            }),
        &tok);

    resize();
    ctl->put_IsVisible(IsWindowVisible(hwnd_));
    wv_->Navigate(L"https://mdlive.assets/shell.html");
}

void Panel::resize() {
    if (!ctl_) return;
    RECT rc;
    GetClientRect(full_ ? full_ : hwnd_, &rc);
    ctl_->put_Bounds(rc);
}

constexpr UINT_PTR kPrintTimer = 1;

void Panel::showPrintPreview(const std::wstring& pdfPath, const std::wstring& title, HWND owner) {
    if (!env_) return;
    wchar_t url[MAX_PATH * 3];
    DWORD len = MAX_PATH * 3;
    if (FAILED(UrlCreateFromPathW(pdfPath.c_str(), url, &len, 0))) return;
    std::wstring target = std::wstring(url) + L"#toolbar=0&navpanes=0";   // bez paska przeglądarki PDF za oknem drukowania
    previewPrintPending_ = true;

    if (preview_) {
        SetWindowTextW(preview_, title.c_str());
        if (IsIconic(preview_)) ShowWindow(preview_, SW_RESTORE);
        SetForegroundWindow(preview_);
        if (previewWv_) previewWv_->Navigate(target.c_str());
        return;
    }

    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = previewProc;
        wc.hInstance = inst_;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)GetClassLongPtrW(owner, GCLP_HICON);
        wc.hbrBackground = (HBRUSH)GetStockObject(GRAY_BRUSH);
        wc.lpszClassName = L"MarkdownLivePrintPreview";
        RegisterClassW(&wc);
        registered = true;
    }
    // Duże okno na środku monitora z Notepad++.
    MONITORINFO mi{sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
    RECT wa = mi.rcWork;
    int w = std::min<int>(1200, (wa.right - wa.left) * 85 / 100), h = (wa.bottom - wa.top) * 88 / 100;
    preview_ = CreateWindowExW(0, L"MarkdownLivePrintPreview", title.c_str(), WS_OVERLAPPEDWINDOW, wa.left + (wa.right - wa.left - w) / 2,
                               wa.top + (wa.bottom - wa.top - h) / 2, w, h, owner, nullptr, inst_, this);
    if (!preview_) return;
    ShowWindow(preview_, SW_SHOW);

    HWND win = preview_;
    env_->CreateCoreWebView2Controller(win, handler<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, HRESULT, ICoreWebView2Controller*>(
        [this, win, target](HRESULT hr, ICoreWebView2Controller* ctl) -> HRESULT {
            if (FAILED(hr) || !ctl) return S_OK;
            if (preview_ != win) { ctl->Close(); return S_OK; }   // okno zamknięte, zanim WebView2 był gotowy
            previewCtl_ = ctl;
            ctl->AddRef();
            ctl->get_CoreWebView2(&previewWv_);
            Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
            previewWv_->get_Settings(&settings);
            settings->put_AreDevToolsEnabled(FALSE);
            settings->put_IsStatusBarEnabled(FALSE);
            RECT rc;
            GetClientRect(win, &rc);
            ctl->put_Bounds(rc);
            EventRegistrationToken tok;
            // Po wczytaniu stron od razu okno drukowania (z podglądem, wyborem drukarki i stron).
            previewWv_->add_NavigationCompleted(
                handler<ICoreWebView2NavigationCompletedEventHandler, ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs*>(
                    [this](ICoreWebView2*, ICoreWebView2NavigationCompletedEventArgs* args) -> HRESULT {
                        BOOL ok = FALSE;
                        args->get_IsSuccess(&ok);
                        if (ok && previewPrintPending_ && preview_) SetTimer(preview_, kPrintTimer, 500, nullptr);
                        return S_OK;
                    }),
                &tok);
            previewWv_->Navigate(target.c_str());
            return S_OK;
        }));
}

LRESULT CALLBACK Panel::previewProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    }
    auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && self->preview_ == h) {
        switch (msg) {
            case WM_SIZE:
                if (self->previewCtl_) {
                    RECT rc;
                    GetClientRect(h, &rc);
                    self->previewCtl_->put_Bounds(rc);
                }
                return 0;
            case WM_TIMER:
                KillTimer(h, wp);
                if (wp == kPrintTimer && self->previewWv_ && self->previewPrintPending_) {
                    self->previewPrintPending_ = false;
                    Microsoft::WRL::ComPtr<ICoreWebView2_16> wv16;
                    if (SUCCEEDED(self->previewWv_->QueryInterface(IID_PPV_ARGS(&wv16)))) wv16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER);
                }
                return 0;
            case WM_SETFOCUS:
                if (self->previewCtl_) self->previewCtl_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                return 0;
            case WM_DESTROY:
                if (self->previewCtl_) { self->previewCtl_->Close(); self->previewCtl_->Release(); self->previewCtl_ = nullptr; }
                if (self->previewWv_) { self->previewWv_->Release(); self->previewWv_ = nullptr; }
                self->preview_ = nullptr;
                return 0;
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Panel::setFullscreen(bool on, HWND owner) {
    if (!ctl_ || on == (full_ != nullptr)) return;
    if (on) {
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = fullProc;
            wc.hInstance = inst_;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = L"MarkdownLiveFullscreen";
            RegisterClassW(&wc);
            registered = true;
        }
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfoW(MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST), &mi);
        RECT r = mi.rcMonitor;
        full_ = CreateWindowExW(0, L"MarkdownLiveFullscreen", L"Markdown Live", WS_POPUP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                                owner, nullptr, inst_, this);
        if (!full_) return;
        ctl_->put_ParentWindow(full_);
        ctl_->put_IsVisible(TRUE);
        resize();
        ShowWindow(full_, SW_SHOW);
        SetForegroundWindow(full_);
        ctl_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
    } else {
        HWND w = full_;
        full_ = nullptr;
        ctl_->put_ParentWindow(hwnd_);
        ctl_->put_IsVisible(IsWindowVisible(hwnd_));
        resize();
        DestroyWindow(w);
        SetForegroundWindow(owner);
    }
}

LRESULT CALLBACK Panel::fullProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    }
    auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self && self->full_ == h) {
        switch (msg) {
            case WM_SIZE:
                self->resize();
                return 0;
            case WM_SETFOCUS:
                if (self->ctl_) self->ctl_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
                return 0;
            case WM_CLOSE:   // Alt+F4: koniec prezentacji, nie zamykanie podglądu
                if (self->onMessage) self->onMessage(L"present-end", L"");
                return 0;
            case WM_ERASEBKGND: {
                RECT rc;
                GetClientRect(h, &rc);
                FillRect((HDC)wp, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
                return 1;
            }
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK Panel::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
    }
    auto* self = reinterpret_cast<Panel*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (!self) return DefWindowProcW(h, msg, wp, lp);

    switch (msg) {
        case WM_SIZE:
            self->resize();
            return 0;
        case WM_SHOWWINDOW:
            if (wp) self->startWebView();
            if (self->ctl_ && !self->full_) self->ctl_->put_IsVisible(wp ? TRUE : FALSE);
            if (self->onVisibility) self->onVisibility(wp != FALSE);
            return 0;
        case WM_ERASEBKGND: {
            RECT rc;
            GetClientRect(h, &rc);
            FillRect((HDC)wp, &rc, self->brush_);
            return 1;
        }
        case WM_TIMER:
            if (self->onTimer) self->onTimer((UINT_PTR)wp);
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lp)->code == kDmnClose && self->onClose) self->onClose();
            return 0;
        case WM_SETFOCUS:
            if (self->ctl_) self->ctl_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
            return 0;
        case WM_DESTROY:
            if (self->preview_) DestroyWindow(self->preview_);
            if (self->ctl_) { self->ctl_->Close(); self->ctl_->Release(); self->ctl_ = nullptr; }
            if (self->wv_) { self->wv_->Release(); self->wv_ = nullptr; }
            if (self->env_) { self->env_->Release(); self->env_ = nullptr; }
            if (self->brush_) DeleteObject(self->brush_);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
