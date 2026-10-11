#include "sysprint.h"
#include <inspectable.h>
#include <winstring.h>
#include "EventToken.h"
#include <d3d11.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <wincodec.h>
#include <documenttarget.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <mutex>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace {

// ----- Interfejsy WinRT i COM, których nie ma w nagłówkach mingw (kolejność metod jak w Windows SDK) -----

constexpr IID IID_IPdfDocumentStatics = {0x433a0b5f, 0xc007, 0x4788, {0x90, 0xf2, 0x08, 0x14, 0x3d, 0x92, 0x25, 0x99}};
constexpr IID IID_IRandomAccessStream = {0x905a0fe1, 0xbc53, 0x11df, {0x8c, 0x49, 0x00, 0x1e, 0x4f, 0xc6, 0x86, 0xda}};
constexpr IID IID_IAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr IID IID_IPdfRendererNative = {0x7d9dcd91, 0xd277, 0x4947, {0x85, 0x27, 0x07, 0xa0, 0xda, 0xed, 0xa9, 0x4a}};
constexpr IID IID_IPrintManagerInterop = {0xc5435a42, 0x8d43, 0x4e7b, {0xa6, 0x8a, 0xef, 0x31, 0x1e, 0x39, 0x20, 0x87}};
constexpr IID IID_IPrintManager = {0xff2a9694, 0x8c99, 0x44fd, {0xae, 0x4a, 0x19, 0xd9, 0xaa, 0x9a, 0x0f, 0x0a}};
constexpr IID IID_IAsyncOperationBool = {0xcdb5efb3, 0x5788, 0x509d, {0x9b, 0xe1, 0x71, 0xcc, 0xb8, 0xa3, 0x36, 0x2a}};
constexpr IID IID_PrintTaskRequestedHandler = {0x8a8cb877, 0x70c5, 0x54ce, {0x8b, 0x42, 0xd7, 0x90, 0xe2, 0x91, 0x48, 0x59}};
constexpr IID IID_PrintTaskSourceRequestedHandler = {0x6c109fa8, 0x5cb6, 0x4b3a, {0x86, 0x63, 0xf3, 0x9c, 0xb0, 0x2d, 0xc9, 0xb4}};
constexpr IID IID_IPrintDocumentSource = {0xdedc0c30, 0xf1eb, 0x47df, {0xaa, 0xe6, 0xed, 0x54, 0x27, 0x51, 0x1f, 0x01}};
constexpr IID IID_IPrintDocumentPageSource = {0xa96bb1db, 0x172e, 0x4667, {0x82, 0xb5, 0xad, 0x97, 0xa2, 0x52, 0x31, 0x8f}};
constexpr IID IID_IPrintPreviewPageCollection = {0x0b31cc62, 0xd7ec, 0x4747, {0x9d, 0x6e, 0xf2, 0x53, 0x7d, 0x87, 0x0f, 0x2b}};
constexpr IID IID_IPrintPreviewDxgiPackageTarget = {0x1a6dd0ad, 0x1e2a, 0x4e99, {0xa5, 0xba, 0x91, 0xf1, 0x78, 0x18, 0x29, 0x0e}};
constexpr IID IID_IAgileObjectX = {0x94ea2b94, 0xe9cc, 0x49e0, {0xc0, 0xff, 0xee, 0x64, 0xca, 0x8f, 0x5b, 0x90}};
constexpr IID IID_IMarshalX = {0x00000003, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
constexpr CLSID CLSID_WicFactory = {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
constexpr IID IID_IWicFactory = {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};
constexpr IID IID_IInspectableX = {0xaf86e2e0, 0xb12d, 0x4c6a, {0x9c, 0x5a, 0xd7, 0xaa, 0x65, 0x10, 0x1e, 0x90}};
constexpr UINT32 kAppDefinedPage = 0xFFFFFFFF;   // JOB_PAGE_APPLICATION_DEFINED

struct SizeF { FLOAT Width, Height; };

struct IPdfPage : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE RenderToStreamAsync(IUnknown*, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderWithOptionsToStreamAsync(IUnknown*, IUnknown*, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE PreparePageAsync(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Index(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Size(SizeF*) = 0;
};

struct IPdfDocument : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetPage(UINT32, IPdfPage**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_PageCount(UINT32*) = 0;
};

struct IPdfDocumentStatics : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE LoadFromFileAsync(IUnknown*, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromFileWithPasswordAsync(IUnknown*, HSTRING, IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE LoadFromStreamAsync(IUnknown*, IUnknown**) = 0;
};

struct IAsyncInfoX : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Id(UINT32*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Status(int*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_ErrorCode(HRESULT*) = 0;
    virtual HRESULT STDMETHODCALLTYPE Cancel() = 0;
    virtual HRESULT STDMETHODCALLTYPE Close() = 0;
};

// IAsyncOperation<T>: wynik pobieramy bez znajomości IID (wskaźnik już mamy).
struct IAsyncOperationX : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE put_Completed(IUnknown*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Completed(IUnknown**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetResults(void*) = 0;
};

struct PdfRenderParams {
    D2D_RECT_F source;
    UINT32 width, height;
    D2D_COLOR_F background;
    BOOLEAN ignoreHighContrast;
};

struct IPdfRendererNative : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE RenderPageToSurface(IUnknown*, IDXGISurface*, POINT, PdfRenderParams*) = 0;
    virtual HRESULT STDMETHODCALLTYPE RenderPageToDeviceContext(IUnknown*, ID2D1DeviceContext*, PdfRenderParams*) = 0;
};

struct IPrintManagerInterop : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE GetForWindow(HWND, REFIID, void**) = 0;
    virtual HRESULT STDMETHODCALLTYPE ShowPrintUIForWindowAsync(HWND, REFIID, void**) = 0;
};

struct IPrintManagerX : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE add_PrintTaskRequested(IUnknown*, EventRegistrationToken*) = 0;
    virtual HRESULT STDMETHODCALLTYPE remove_PrintTaskRequested(EventRegistrationToken) = 0;
};

struct IPrintTaskRequestX : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Deadline(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE CreatePrintTask(HSTRING, IUnknown*, IInspectable**) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeferral(IInspectable**) = 0;
};

struct IPrintTaskRequestedEventArgsX : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Request(IPrintTaskRequestX**) = 0;
};

struct IPrintTaskSourceRequestedArgsX : IInspectable {
    virtual HRESULT STDMETHODCALLTYPE get_Deadline(INT64*) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetSource(IInspectable*) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeferral(IInspectable**) = 0;
};

struct IPrintPreviewDxgiPackageTarget : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE SetJobPageCount(int countType, UINT32 count) = 0;
    virtual HRESULT STDMETHODCALLTYPE DrawPage(UINT32 jobPageNumber, IDXGISurface* pageImage, FLOAT dpiX, FLOAT dpiY) = 0;
    virtual HRESULT STDMETHODCALLTYPE InvalidatePreview() = 0;
};

struct IPrintPreviewPageCollection : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE Paginate(UINT32 currentJobPage, IInspectable* printTaskOptions) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakePage(UINT32 desiredJobPage, FLOAT width, FLOAT height) = 0;
};

struct IPrintDocumentPageSource : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPreviewPageCollection(IPrintDocumentPackageTarget*, IPrintPreviewPageCollection**) = 0;
    virtual HRESULT STDMETHODCALLTYPE MakeDocument(IInspectable*, IPrintDocumentPackageTarget*) = 0;
};

struct IPrintDocumentSource : IInspectable {};

// ----- Funkcje systemowe ładowane w locie (bez dodatkowych bibliotek przy linkowaniu) -----

template <class F>
F api(const wchar_t* dll, const char* name) {
    HMODULE m = LoadLibraryW(dll);
    return m ? reinterpret_cast<F>(GetProcAddress(m, name)) : nullptr;
}

using RoGetActivationFactoryFn = HRESULT(WINAPI*)(HSTRING, REFIID, void**);
using WindowsCreateStringFn = HRESULT(WINAPI*)(PCNZWCH, UINT32, HSTRING*);
using WindowsDeleteStringFn = HRESULT(WINAPI*)(HSTRING);
using CreateStreamOnFileFn = HRESULT(WINAPI*)(PCWSTR, DWORD, REFIID, void**);
using PdfCreateRendererFn = HRESULT(WINAPI*)(IDXGIDevice*, IPdfRendererNative**);

struct Str {
    HSTRING h = nullptr;
    explicit Str(const std::wstring& s) {
        static auto create = api<WindowsCreateStringFn>(L"combase.dll", "WindowsCreateString");
        if (create) create(s.c_str(), (UINT32)s.size(), &h);
    }
    ~Str() {
        static auto del = api<WindowsDeleteStringFn>(L"combase.dll", "WindowsDeleteString");
        if (h && del) del(h);
    }
};

template <class T>
HRESULT factory(const wchar_t* cls, REFIID iid, T** out) {
    static auto get = api<RoGetActivationFactoryFn>(L"combase.dll", "RoGetActivationFactory");
    if (!get) return E_NOTIMPL;
    Str name(cls);
    return get(name.h, iid, reinterpret_cast<void**>(out));
}

// Czeka na zakończenie operacji asynchronicznej WinRT (wczytanie PDF trwa ułamek sekundy).
HRESULT waitAsync(IUnknown* op) {
    ComPtr<IAsyncInfoX> info;
    HRESULT hr = op->QueryInterface(IID_IAsyncInfo, reinterpret_cast<void**>(info.GetAddressOf()));
    if (FAILED(hr)) return hr;
    int status = 0;
    for (int i = 0; i < 1000 && SUCCEEDED(info->get_Status(&status)) && status == 0; i++) Sleep(5);
    if (status == 1) return S_OK;
    HRESULT err = E_FAIL;
    info->get_ErrorCode(&err);
    return FAILED(err) ? err : E_FAIL;
}

// ----- Dokument: strony PDF i rysowanie przez Direct2D -----

struct Job {
    ComPtr<IPdfDocument> doc;
    std::vector<SizeF> sizes;
    ComPtr<ID3D11Device> d3d;
    ComPtr<ID2D1Device> d2d;
    ComPtr<IPdfRendererNative> renderer;
    std::mutex lock;

    // Strona dopasowana do obszaru w DIP (z zachowaniem proporcji, na środku).
    HRESULT draw(ID2D1DeviceContext* dc, UINT32 index, float w, float h) {
        std::lock_guard<std::mutex> g(lock);
        ComPtr<IPdfPage> page;
        HRESULT hr = doc->GetPage(index, &page);
        if (FAILED(hr)) return hr;
        SizeF s = sizes[index];
        float k = std::min(w / s.Width, h / s.Height);
        D2D1_MATRIX_3X2_F m = {k, 0, 0, k, (w - s.Width * k) / 2, (h - s.Height * k) / 2};
        dc->SetTransform(&m);
        return renderer->RenderPageToDeviceContext(page.Get(), dc, nullptr);
    }
};

std::shared_ptr<Job> loadJob(const std::wstring& pdfPath) {
    auto job = std::make_shared<Job>();
    static auto openStream = api<CreateStreamOnFileFn>(L"shcore.dll", "CreateRandomAccessStreamOnFile");
    static auto createRenderer = api<PdfCreateRendererFn>(L"Windows.Data.Pdf.dll", "PdfCreateRenderer");
    if (!openStream || !createRenderer) return nullptr;

    ComPtr<IUnknown> stream;
    if (FAILED(openStream(pdfPath.c_str(), 0 /* FileAccessMode::Read */, IID_IRandomAccessStream, reinterpret_cast<void**>(stream.GetAddressOf())))) return nullptr;
    ComPtr<IPdfDocumentStatics> statics;
    if (FAILED(factory(L"Windows.Data.Pdf.PdfDocument", IID_IPdfDocumentStatics, statics.GetAddressOf()))) return nullptr;
    ComPtr<IUnknown> op;
    if (FAILED(statics->LoadFromStreamAsync(stream.Get(), &op)) || FAILED(waitAsync(op.Get()))) return nullptr;
    auto* result = reinterpret_cast<IAsyncOperationX*>(op.Get());   // to już jest IAsyncOperation<PdfDocument>
    if (FAILED(result->GetResults(job->doc.GetAddressOf())) || !job->doc) return nullptr;

    UINT32 count = 0;
    job->doc->get_PageCount(&count);
    for (UINT32 i = 0; i < count; i++) {
        ComPtr<IPdfPage> page;
        SizeF s{816, 1056};
        if (SUCCEEDED(job->doc->GetPage(i, &page))) page->get_Size(&s);
        job->sizes.push_back(s);
    }
    if (job->sizes.empty()) return nullptr;

    D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_9_3};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                                   D3D11_SDK_VERSION, &job->d3d, nullptr, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
                               D3D11_SDK_VERSION, &job->d3d, nullptr, nullptr);
    }
    if (FAILED(hr)) return nullptr;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<ID2D1Factory1> d2dFactory;
    D2D1_FACTORY_OPTIONS opts{D2D1_DEBUG_LEVEL_NONE};
    if (FAILED(job->d3d.As(&dxgi)) ||
        FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, __uuidof(ID2D1Factory1), &opts, reinterpret_cast<void**>(d2dFactory.GetAddressOf()))) ||
        FAILED(d2dFactory->CreateDevice(dxgi.Get(), &job->d2d)) || FAILED(createRenderer(dxgi.Get(), &job->renderer))) {
        return nullptr;
    }
    return job;
}

// ----- Obiekty COM przekazywane oknu drukowania (bez przywiązania do wątku: marshaler wolnowątkowy) -----

template <class Self>
struct Agile {
    std::atomic<ULONG> refs{1};
    ComPtr<IUnknown> ftm;
    bool agileQI(Self* self, REFIID riid, void** out) {
        if (IsEqualIID(riid, IID_IAgileObjectX)) { *out = static_cast<IUnknown*>(self->unknown()); self->unknown()->AddRef(); return true; }
        if (IsEqualIID(riid, IID_IMarshalX)) {
            if (!ftm) CoCreateFreeThreadedMarshaler(self->unknown(), &ftm);
            if (ftm && SUCCEEDED(ftm->QueryInterface(riid, out))) return true;
        }
        return false;
    }
};

class DocumentSource final : public IPrintDocumentSource, public IPrintDocumentPageSource, public IPrintPreviewPageCollection,
                             public Agile<DocumentSource> {
public:
    explicit DocumentSource(std::shared_ptr<Job> job) : job_(std::move(job)) {}
    IUnknown* unknown() { return static_cast<IPrintDocumentSource*>(this); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        *out = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, IID_IInspectableX) || IsEqualIID(riid, IID_IPrintDocumentSource)) {
            *out = static_cast<IPrintDocumentSource*>(this);
        } else if (IsEqualIID(riid, IID_IPrintDocumentPageSource)) {
            *out = static_cast<IPrintDocumentPageSource*>(this);
        } else if (IsEqualIID(riid, IID_IPrintPreviewPageCollection)) {
            *out = static_cast<IPrintPreviewPageCollection*>(this);
        } else if (agileQI(this, riid, out)) {
            return S_OK;
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE GetIids(ULONG* count, IID** iids) override {
        *count = 0;
        *iids = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeClassName(HSTRING* name) override {
        *name = nullptr;
        return E_NOTIMPL;
    }
    HRESULT STDMETHODCALLTYPE GetTrustLevel(TrustLevel* level) override {
        *level = BaseTrust;
        return S_OK;
    }

    // Podgląd: liczba stron i obraz każdej strony w rozmiarze papieru wybranego w oknie.
    HRESULT STDMETHODCALLTYPE GetPreviewPageCollection(IPrintDocumentPackageTarget* target, IPrintPreviewPageCollection** out) override {
        HRESULT hr = target->GetPackageTarget(IID_IPrintPreviewDxgiPackageTarget, IID_IPrintPreviewDxgiPackageTarget,
                                              reinterpret_cast<void**>(preview_.ReleaseAndGetAddressOf()));
        if (FAILED(hr)) return hr;
        *out = static_cast<IPrintPreviewPageCollection*>(this);
        AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Paginate(UINT32, IInspectable*) override {
        if (!preview_) return E_FAIL;
        return preview_->SetJobPageCount(0 /* FinalPageCount */, (UINT32)job_->sizes.size());
    }

    HRESULT STDMETHODCALLTYPE MakePage(UINT32 desired, FLOAT width, FLOAT height) override {
        if (!preview_) return E_FAIL;
        if (desired == kAppDefinedPage) desired = 1;
        if (desired < 1 || desired > job_->sizes.size() || width <= 0 || height <= 0) return S_OK;
        const float dpi = 192;   // ostrzejszy obraz niż 96 DPI
        UINT w = (UINT)std::ceil(width * dpi / 96), h = (UINT)std::ceil(height * dpi / 96);
        D3D11_TEXTURE2D_DESC td{};
        td.Width = w;
        td.Height = h;
        td.MipLevels = 1;
        td.ArraySize = 1;
        td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        td.SampleDesc.Count = 1;
        td.Usage = D3D11_USAGE_DEFAULT;
        td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> tex;
        ComPtr<IDXGISurface> surface;
        ComPtr<ID2D1DeviceContext> dc;
        ComPtr<ID2D1Bitmap1> bitmap;
        HRESULT hr = job_->d3d->CreateTexture2D(&td, nullptr, &tex);
        if (SUCCEEDED(hr)) hr = tex.As(&surface);
        if (SUCCEEDED(hr)) hr = job_->d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
        if (FAILED(hr)) return hr;
        D2D1_BITMAP_PROPERTIES1 bp{};
        bp.pixelFormat = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED};
        bp.dpiX = bp.dpiY = dpi;
        bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
        hr = dc->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &bitmap);
        if (FAILED(hr)) return hr;
        dc->SetTarget(bitmap.Get());
        dc->SetDpi(dpi, dpi);
        dc->BeginDraw();
        dc->Clear(D2D1_COLOR_F{1, 1, 1, 1});
        hr = job_->draw(dc.Get(), desired - 1, width, height);
        HRESULT end = dc->EndDraw();
        if (FAILED(hr) || FAILED(end)) return FAILED(hr) ? hr : end;
        return preview_->DrawPage(desired, surface.Get(), dpi, dpi);
    }

    // Wydruk: każda strona jako lista poleceń Direct2D (wektorowo, w rozmiarze strony PDF).
    HRESULT STDMETHODCALLTYPE MakeDocument(IInspectable*, IPrintDocumentPackageTarget* target) override {
        bool comInit = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        HRESULT hr = makeDocument(target);
        if (comInit) CoUninitialize();
        return hr;
    }

private:
    HRESULT makeDocument(IPrintDocumentPackageTarget* target) {
        ComPtr<IWICImagingFactory> wic;
        HRESULT hr = CoCreateInstance(CLSID_WicFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWicFactory, reinterpret_cast<void**>(wic.GetAddressOf()));
        if (FAILED(hr)) return hr;
        D2D1_PRINT_CONTROL_PROPERTIES props{D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT, 300.0f, D2D1_COLOR_SPACE_SRGB};
        ComPtr<ID2D1PrintControl> print;
        ComPtr<ID2D1DeviceContext> dc;
        hr = job_->d2d->CreatePrintControl(wic.Get(), target, &props, &print);
        if (SUCCEEDED(hr)) hr = job_->d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &dc);
        if (FAILED(hr)) return hr;
        for (UINT32 i = 0; i < job_->sizes.size() && SUCCEEDED(hr); i++) {
            ComPtr<ID2D1CommandList> list;
            hr = dc->CreateCommandList(&list);
            if (FAILED(hr)) break;
            dc->SetTarget(list.Get());
            dc->BeginDraw();
            SizeF s = job_->sizes[i];
            hr = job_->draw(dc.Get(), i, s.Width, s.Height);
            HRESULT end = dc->EndDraw();
            if (SUCCEEDED(hr)) hr = end;
            if (SUCCEEDED(hr)) hr = list->Close();
            if (SUCCEEDED(hr)) hr = print->AddPage(list.Get(), D2D1_SIZE_F{s.Width, s.Height}, nullptr, nullptr, nullptr);
        }
        HRESULT closed = print->Close();
        return FAILED(hr) ? hr : closed;
    }

    std::shared_ptr<Job> job_;
    ComPtr<IPrintPreviewDxgiPackageTarget> preview_;
};

// Delegat WinRT: jedna metoda Invoke, którą wywołuje system.
template <class Fn, class... Args>
class Delegate final : public IUnknown, public Agile<Delegate<Fn, Args...>> {
public:
    Delegate(REFIID iid, Fn fn) : iid_(iid), fn_(std::move(fn)) {}
    IUnknown* unknown() { return this; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        *out = nullptr;
        if (IsEqualIID(riid, IID_IUnknown) || IsEqualIID(riid, iid_)) {
            *out = this;
            AddRef();
            return S_OK;
        }
        return this->agileQI(this, riid, out) ? S_OK : E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++this->refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --this->refs;
        if (!r) delete this;
        return r;
    }
    virtual HRESULT STDMETHODCALLTYPE Invoke(Args... args) { return fn_(args...); }

private:
    IID iid_;
    Fn fn_;
};

template <class... Args, class Fn>
ComPtr<IUnknown> delegate(REFIID iid, Fn fn) {
    ComPtr<IUnknown> p;
    p.Attach(static_cast<IUnknown*>(new Delegate<Fn, Args...>(iid, std::move(fn))));
    return p;
}

// Stan bieżącego wydruku (jedno okno drukowania naraz).
struct Session {
    HWND owner = nullptr;
    ComPtr<IPrintManagerX> manager;
    EventRegistrationToken token{};
    bool registered = false;
    ComPtr<IUnknown> op;
    std::shared_ptr<Job> job;
    std::wstring title;
};
Session& session = *new Session;   // celowo bez niszczenia: obiekty COM nie są zwalniane przy zamykaniu programu

} // namespace

bool showSystemPrint(HWND owner, const std::wstring& pdfPath, const std::wstring& title) {
    auto job = loadJob(pdfPath);
    if (!job) return false;

    ComPtr<IPrintManagerInterop> interop;
    if (FAILED(factory(L"Windows.Graphics.Printing.PrintManager", IID_IPrintManagerInterop, interop.GetAddressOf()))) return false;

    if (session.registered) {
        session.manager->remove_PrintTaskRequested(session.token);
        session.registered = false;
    }
    if (session.owner != owner || !session.manager) {
        session.manager.Reset();
        if (FAILED(interop->GetForWindow(owner, IID_IPrintManager, reinterpret_cast<void**>(session.manager.GetAddressOf())))) return false;
        session.owner = owner;
    }
    session.job = job;
    session.title = title;

    // System prosi o zadanie wydruku, a potem o źródło stron (nasz dokument).
    auto onRequested = delegate<IInspectable*, IPrintTaskRequestedEventArgsX*>(
        IID_PrintTaskRequestedHandler, [](IInspectable*, IPrintTaskRequestedEventArgsX* args) -> HRESULT {
            ComPtr<IPrintTaskRequestX> request;
            HRESULT hr = args->get_Request(&request);
            if (FAILED(hr)) return hr;
            std::shared_ptr<Job> current = session.job;
            auto onSource = delegate<IPrintTaskSourceRequestedArgsX*>(
                IID_PrintTaskSourceRequestedHandler, [current](IPrintTaskSourceRequestedArgsX* source) -> HRESULT {
                    ComPtr<IPrintDocumentSource> doc;
                    doc.Attach(new DocumentSource(current));
                    return source->SetSource(doc.Get());
                });
            Str name(session.title);
            ComPtr<IInspectable> task;
            return request->CreatePrintTask(name.h, onSource.Get(), &task);
        });
    if (FAILED(session.manager->add_PrintTaskRequested(onRequested.Get(), &session.token))) return false;
    session.registered = true;

    session.op.Reset();
    return SUCCEEDED(interop->ShowPrintUIForWindowAsync(owner, IID_IAsyncOperationBool, reinterpret_cast<void**>(session.op.GetAddressOf())));
}
