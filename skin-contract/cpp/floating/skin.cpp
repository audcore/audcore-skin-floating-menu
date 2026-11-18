#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include "../reference_recovery.hpp"
#include "../reference_subscription.hpp"
#include "../skin_protocol_limits.hpp"
#include "../native_menu/session.hpp"
#include "version.h"

namespace floating {
using namespace native_menu;
constexpr char id[] = "cn.audcore.floating-menu";
constexpr char name[] = "声核悬浮菜单";
constexpr char version[] = AUDCORE_FLOATING_VERSION;
constexpr int logical_size = 56;

struct Skin : Session<Skin> {
    audcore_skin_host host{};
    HMODULE module{};
    HWND window{};
    wchar_t class_name[80]{};
    bool registered = false, shown = false, ready = false;
    std::uint64_t revision = 0, next_request = 0;
    ReferenceSubscription subscription;
    ReferenceRecovery recovery;
    ReferenceSkinState state;
    Locale locale;
    std::string feedback;
    std::optional<POINT> pressed;
    POINT origin{};
    bool moving = false;

    bool usable() const { return shown && ready && !recovery.active(); }

    void recover() {
        cancel_actions();
        if (subscription.active() && recovery.start_request() &&
            host.request_snapshot(host.ctx, subscription.token()) != 0) recovery.request_failed();
    }

    void send(const std::string& command, bool requires_state = true) {
        if (!shown || !subscription.active() || (requires_state && !usable()) ||
            command.empty() || command.size() > skin_max_request_bytes) return;
        auto request = Json::parse(command);
        if (request.at("method").get_ref<const std::string&>().starts_with("menu.")) {
            request["params"]["supplementalNativeMenu"] = true;
        }
        const auto payload = request.dump();
        const audcore_skin_bytes bytes{reinterpret_cast<const std::uint8_t*>(payload.data()), static_cast<std::uint32_t>(payload.size())};
        if (host.command(host.ctx, ++next_request, bytes) != 0) {
            feedback = locale("操作失败", "Action failed", "操作失敗");
            MessageBeep(MB_ICONWARNING);
        }
    }
};

bool paint_logo(const Skin& skin) noexcept {
    const int size = MulDiv(static_cast<int>(std::lround(logical_size * skin.state.scale())), GetDpiForWindow(skin.window), 96);
    if (size <= 0) return false;
    const auto icon = static_cast<HICON>(LoadImageW(skin.module, MAKEINTRESOURCEW(AUDCORE_FLOATING_ICON), IMAGE_ICON, size, size, 0));
    if (!icon) return false;
    auto dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
    void* pixels = nullptr;
    auto bitmap = dc ? CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0) : nullptr;
    bool painted = false;
    if (bitmap) {
        const auto previous = SelectObject(dc, bitmap);
        std::memset(pixels, 0, static_cast<std::size_t>(size) * size * 4);
        // DrawIconEx composites the alpha icon onto transparent black in this
        // 32-bit DIB. UpdateLayeredWindow consumes the resulting premultiplied BGRA.
        if (DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL)) {
            POINT source{};
            SIZE extent{size, size};
            BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
            painted = UpdateLayeredWindow(skin.window, nullptr, nullptr, &extent, dc, &source, 0, &blend, ULW_ALPHA) != FALSE;
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
    }
    if (dc) DeleteDC(dc);
    DestroyIcon(icon);
    return painted;
}

void AUDCORE_SKIN_CALL receive(void* user, const audcore_skin_event* event) noexcept {
    auto& skin = *static_cast<Skin*>(user);
    if (!event || event->size < sizeof(*event) || !skin.shown) return;
    try {
        if (event->kind == AUDCORE_SKIN_EVENT_ERROR) {
            skin.ready = false;
            skin.cancel_actions();
            skin.subscription.stop(skin.host);
            return;
        }
        if (event->payload.len > skin_max_event_bytes || (event->payload.len && !event->payload.data)) {
            if (event->kind == AUDCORE_SKIN_EVENT_SNAPSHOT && skin.recovery.active()) {
                skin.recovery.snapshot_received(false);
                return;
            }
            if (event->kind == AUDCORE_SKIN_EVENT_SNAPSHOT || event->kind == AUDCORE_SKIN_EVENT_DELTA) skin.recover();
            return;
        }
        if (event->kind == AUDCORE_SKIN_EVENT_TRANSIENT) return;
        const std::string bytes(event->payload.data ? reinterpret_cast<const char*>(event->payload.data) : "", event->payload.len);
        if (event->kind == AUDCORE_SKIN_EVENT_RESPONSE) {
            if (event->status) {
                skin.feedback = ReferenceSkinState::response_error(bytes).value_or(skin.locale("操作失败", "Action failed", "操作失敗"));
                MessageBeep(MB_ICONWARNING);
            }
            return;
        }
        const auto scale = skin.state.scale();
        Json i18n;
        if (event->kind == AUDCORE_SKIN_EVENT_SNAPSHOT) {
            const bool recovering = skin.recovery.active();
            const bool valid = (!skin.ready || event->revision >= skin.revision) && skin.state.snapshot(bytes);
            skin.recovery.snapshot_received(valid);
            if (!valid) {
                if (!recovering) skin.recover();
                return;
            }
            i18n = Json::parse(bytes).value("i18n", Json::object());
            skin.ready = true;
        } else if (event->kind == AUDCORE_SKIN_EVENT_DELTA) {
            if (!skin.usable() || event->revision != skin.revision + 1 || !skin.state.delta(bytes)) {
                skin.recover();
                return;
            }
            const auto delta = Json::parse(bytes);
            if (delta.value("key", "") == "i18n") i18n = delta.at("value");
            if (delta.contains("changes") && delta["changes"].contains("i18n")) i18n = delta["changes"]["i18n"];
        } else return;
        if (i18n.is_object() && i18n.contains("language")) skin.locale.language = i18n.at("language").get<std::string>();
        skin.revision = event->revision;
        // Keep the captured command IDs and state consistent, including when
        // the host changes a channel or replaces the project while a popup is open.
        skin.clear_menu();
        if (skin.state.scale() != scale) paint_logo(skin);
    } catch (...) {
        skin.recover();
    }
}

bool keyboard_menu(WPARAM key, bool shift) {
    return key == VK_APPS || (key == VK_F10 && shift);
}

void open_menu(Skin& skin, POINT screen) {
    if (!skin.shown || skin.dialog) return;
    std::vector<Item> items;
    if (!skin.usable()) {
        if (skin.recovery.active()) skin.recover();
        items = {action(skin.locale("正在等待声核状态…", "Waiting for Audcore…", "状態を取得中…"))};
    } else {
        items = menu(skin.state,skin.locale,screen.x,screen.y,window_topmost(skin.window));
    }
    if (!skin.feedback.empty())
        items.insert(items.begin(),action(std::exchange(skin.feedback,{})));
    skin.show_menu(skin.window,screen,std::move(items));
}

LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        return TRUE;
    }
    auto* skin = reinterpret_cast<Skin*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!skin) return DefWindowProcW(hwnd, message, wp, lp);
    try {
        if (skin->menu_message(hwnd,message,wp,lp)) return 0;
        switch (message) {
        case WM_CONTEXTMENU: {
            POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            if (lp == -1) {
                RECT rect{};
                GetWindowRect(hwnd, &rect);
                point = {rect.left, rect.bottom};
            }
            open_menu(*skin, point);
            return 0;
        }
        case WM_ACTIVATEAPP:
            if (!wp) {
                skin->clear_menu();
                skin->pending.reset();
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            if (keyboard_menu(wp, GetKeyState(VK_SHIFT) < 0)) {
                if (!(lp & (1L << 30))) PostMessageW(hwnd, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(hwnd), -1);
                return 0;
            }
            break;
        case WM_LBUTTONDOWN: {
            POINT cursor{};
            RECT rect{};
            if (!skin->dialog && GetCursorPos(&cursor) && GetWindowRect(hwnd, &rect)) {
                SetFocus(hwnd);
                skin->pressed = cursor;
                skin->origin = {rect.left, rect.top};
                skin->moving = false;
                SetCapture(hwnd);
            }
            return 0;
        }
        case WM_MOUSEMOVE:
            if (skin->pressed && GetCapture() == hwnd) {
                POINT cursor{};
                if (!GetCursorPos(&cursor)) return 0;
                const auto dx = cursor.x - skin->pressed->x, dy = cursor.y - skin->pressed->y;
                const auto dpi = GetDpiForWindow(hwnd);
                if (skin->moving || std::abs(dx) >= GetSystemMetricsForDpi(SM_CXDRAG, dpi) ||
                    std::abs(dy) >= GetSystemMetricsForDpi(SM_CYDRAG, dpi)) {
                    skin->moving = true;
                    SetWindowPos(hwnd, nullptr, skin->origin.x + dx, skin->origin.y + dy, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                }
            }
            return 0;
        case WM_LBUTTONUP:
        case WM_CANCELMODE:
            if (GetCapture() == hwnd) ReleaseCapture();
            skin->pressed.reset();
            break;
        case WM_CAPTURECHANGED:
            skin->pressed.reset();
            break;
        case WM_DPICHANGED: {
            const auto* rect = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
            paint_logo(*skin);
            return 0;
        }
        case WM_CLOSE:
            skin->send(request("window.close"), false);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            break;
        }
    } catch (...) { MessageBeep(MB_ICONWARNING); }
    return DefWindowProcW(hwnd, message, wp, lp);
}

void close_window(Skin& skin) noexcept {
    skin.cancel_actions();
    if (skin.registered) skin.host.unregister_window(skin.host.ctx, skin.window);
    skin.registered = false;
    if (skin.window) DestroyWindow(std::exchange(skin.window, nullptr));
    if (*skin.class_name) {
        UnregisterClassW(skin.class_name, skin.module);
        *skin.class_name = 0;
    }
}

void* AUDCORE_SKIN_CALL create(const audcore_skin_host* host) noexcept {
    if (!host || host->header.size < sizeof(*host) || host->header.major != AUDCORE_SKIN_ABI_MAJOR ||
        !host->register_window || !host->unregister_window || !host->command ||
        !host->subscribe || !host->unsubscribe || !host->request_snapshot) return nullptr;
    try {
        auto skin = std::make_unique<Skin>();
        skin->host = *host;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&window_proc), &skin->module)) return nullptr;
        return skin.release();
    } catch (...) { return nullptr; }
}

bool open_window(Skin& skin) {
    std::swprintf(skin.class_name, std::size(skin.class_name), L"AudcoreFloatingMenu-%p", static_cast<void*>(&skin));
    WNDCLASSW cls{};
    cls.hInstance = skin.module;
    cls.lpfnWndProc = window_proc;
    cls.lpszClassName = skin.class_name;
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassW(&cls)) { *skin.class_name = 0; return false; }
    RECT area{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
    skin.window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TOPMOST, skin.class_name, L"Audcore",
                                  WS_POPUP, area.right - 96, area.top + 48, logical_size, logical_size,
                                  nullptr, nullptr, skin.module, &skin);
    if (!skin.window) return false;
    const audcore_skin_window_spec spec{sizeof(audcore_skin_window_spec), logical_size, logical_size, 42, 42, 84, 84, 0, 0};
    skin.registered = skin.host.register_window(skin.host.ctx, skin.window, &spec) == 0;
    return skin.registered && paint_logo(skin);
}

int32_t AUDCORE_SKIN_CALL show(void* instance) noexcept {
    auto& skin = *static_cast<Skin*>(instance);
    if (!skin.subscription.start(skin.host, receive, &skin)) return -1;
    if (!skin.window && !open_window(skin)) {
        skin.subscription.stop(skin.host);
        close_window(skin);
        return -1;
    }
    skin.shown = true;
    ShowWindow(skin.window, SW_SHOW);
    return 0;
}

void AUDCORE_SKIN_CALL hide(void* instance) noexcept {
    auto& skin = *static_cast<Skin*>(instance);
    skin.shown = skin.ready = false;
    skin.recovery.reset();
    skin.cancel_actions();
    if (skin.window) {
        if (GetCapture() == skin.window) ReleaseCapture();
        ShowWindow(skin.window, SW_HIDE);
    }
}

void AUDCORE_SKIN_CALL destroy(void* instance) noexcept {
    std::unique_ptr<Skin> skin(static_cast<Skin*>(instance));
    skin->shown = false;
    skin->subscription.stop(skin->host);
    close_window(*skin);
}

const audcore_skin_descriptor descriptor{
    {sizeof(audcore_skin_descriptor), AUDCORE_SKIN_ABI_MAJOR, AUDCORE_SKIN_ABI_MINOR},
    {reinterpret_cast<const std::uint8_t*>(id), sizeof(id) - 1},
    {reinterpret_cast<const std::uint8_t*>(name), sizeof(name) - 1},
    {reinterpret_cast<const std::uint8_t*>(version), sizeof(version) - 1},
    create, show, hide, destroy, AUDCORE_SKIN_THEME_DARK | AUDCORE_SKIN_THEME_LIGHT, 0.75f, 1.5f, 1.0f
};
}

extern "C" __declspec(dllexport) const audcore_skin_descriptor* AUDCORE_SKIN_CALL
audcore_skin_get_descriptor(std::uint32_t major) noexcept {
    return major == AUDCORE_SKIN_ABI_MAJOR ? &floating::descriptor : nullptr;
}
