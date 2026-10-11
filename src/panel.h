#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <utility>
#include <vector>

struct ICoreWebView2;
struct ICoreWebView2Controller;
struct ICoreWebView2Environment;

// Okno-gospodarz WebView2, które Notepad++ dokuje jako panel.
class Panel {
public:
    std::function<void(const std::wstring& cmd, const std::wstring& arg)> onMessage;
    std::function<void(bool visible)> onVisibility;
    std::function<void()> onClose;
    std::function<void(UINT_PTR id)> onTimer;

    bool create(HINSTANCE inst, HWND parent, std::wstring pluginDir, std::wstring userDataDir);
    HWND hwnd() const { return hwnd_; }

    // Wiadomość JSON dla strony; do czasu gotowości strony trzymana w kolejce (nowsza o tym samym kluczu zastępuje starszą).
    void post(const std::wstring& key, const std::wstring& json);
    void mapDocFolder(const std::wstring& dir);

    bool ready() const { return ready_ && wv_; }

    // Podgląd jako PDF (A4 lub Letter wg regionu Windows, bez daty i adresu na marginesach).
    void printToPdf(const std::wstring& path, std::function<void(bool ok)> done);
    // Osobne okno z gotowymi stronami PDF i od razu oknem drukowania z podglądem.
    void showPrintPreview(const std::wstring& pdfPath, const std::wstring& title, HWND owner);
    void setPrintBackground(bool printing);
    void setBackground(COLORREF c);

    // Tryb prezentacji: podgląd przenosi się do okna na cały ekran (monitor z Notepad++) i wraca.
    void setFullscreen(bool on, HWND owner);
    bool fullscreen() const { return full_ != nullptr; }

private:
    static LRESULT CALLBACK wndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK fullProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK previewProc(HWND, UINT, WPARAM, LPARAM);
    void startWebView();
    void onControllerReady(ICoreWebView2Controller* ctl);
    void flush();
    void resize();

    HWND hwnd_ = nullptr, full_ = nullptr;
    HINSTANCE inst_ = nullptr;
    std::wstring pluginDir_, userDataDir_, docDir_;
    COLORREF bg_ = RGB(255, 255, 255);
    HBRUSH brush_ = nullptr;
    bool started_ = false, ready_ = false;
    std::vector<std::pair<std::wstring, std::wstring>> queue_;

    ICoreWebView2Environment* env_ = nullptr;
    ICoreWebView2Controller* ctl_ = nullptr;
    ICoreWebView2* wv_ = nullptr;

    // Okno podglądu wydruku (własny WebView2 z przeglądarką PDF).
    HWND preview_ = nullptr;
    ICoreWebView2Controller* previewCtl_ = nullptr;
    ICoreWebView2* previewWv_ = nullptr;
    bool previewPrintPending_ = false;
};
