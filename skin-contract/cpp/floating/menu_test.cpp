#include "skin.cpp"
#include "../native_skin_loader.hpp"
#include <cassert>
#include <filesystem>

using namespace floating;

const Item& find(const std::vector<Item>& rows, std::string_view label) {
    const auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& item) { return item.label == label; });
    assert(row != rows.end());
    return *row;
}

void model_and_win32_menu() {
    ReferenceSkinState state;
    assert(state.snapshot(R"({
      "transport":{"playing":true},
      "channels":[
        {"id":7,"name":"A & B","hidden":true,"muted":true,"chain_uuid":"first",
         "sends":[{"id":"route","dest":{"kind":"chain","id":3},"tap":1,"level_db":-3.5}],
         "rack":[{"instance":99,"path":"effect.vst3","alias":"EQ","effect_uuid":"eq","offline":true}]},
        {"id":3,"name":"主唱","chain_uuid":"second"},
        {"id":42,"name":"超出独奏范围"}
      ]
    })"));
    const auto rows = menu(state, Locale{}, -100, -40, true);
    const auto& channels = find(rows, "所有通道").children;
    assert(channels[0].label == "A & B" && channels[1].label == "主唱" && channels[2].label == "超出独奏范围");
    const auto& mute = find(channels[0].children, "静音");
    assert(mute.checked && mute.mark == Mark::check);
    const auto mute_params = Json::parse(mute.command)["params"];
    assert(mute_params["id"] == 7 && mute_params["muted"] == false && mute_params["expectedChannel"]["chain_uuid"] == "first");
    assert(find(channels[2].children, "独奏").command.empty());
    const auto& gain = find(channels[0].children, "增益 0 dB…");
    assert(Json::parse(*edited_command(gain, " +6.25 "))["params"]["db"] == 6.25);
    for (const char* invalid : {"", "6 dB", "nan", "inf", "-61", "13"}) assert(!edited_command(gain, invalid));
    const auto& send = find(find(channels[0].children, "发送").children, "主唱");
    const auto& taps = find(send.children, "取样点").children;
    assert(taps.size() == 3 && !taps[0].checked && taps[1].checked && !taps[2].checked);
    assert(Json::parse(taps[2].command)["params"]["expected"]["id"] == "route");
    const auto& effect = find(find(channels[0].children, "效果器").children, "EQ");
    assert(find(effect.children, "打开界面…").command.empty());
    assert(Json::parse(find(effect.children, "移除").command)["params"]["expectedEffect"]["effect_uuid"] == "eq");
    assert(find(find(rows, "传输").children, "播放").checked);
    assert(menu_label("A & B\tC\nD") == L"A && B C D");
    assert(Json::parse(find(channels[0].children, "快捷键…").command)["params"]["anchor"]["x"] == -100);
    assert(find(menu(state, Locale{"ja"}, 0, 0, false), "全通路").children.size() == 4);

    // Use the real Win32 menu API without creating or operating any window.
    auto popup = make_popup(rows);
    assert(popup);
    const auto& topmost = find(rows, "保持最前");
    assert(topmost.toggle_topmost && topmost.mark == Mark::check);
    const auto flags = GetMenuState(popup.get(), static_cast<UINT>(&topmost - rows.data()), MF_BYPOSITION);
    assert(flags != UINT(-1) && (flags & MF_CHECKED) && !(flags & MF_GRAYED));
    const auto channel_list = GetSubMenu(popup.get(), 1);
    assert(GetMenuItemCount(channel_list) == 1);
    assert(populate_popup(channel_list) && GetMenuItemCount(channel_list) == 4);
    HMENU channel_menu = GetSubMenu(channel_list, 0);
    assert(populate_popup(channel_menu));
    wchar_t title[32]{};
    assert(GetMenuStringW(GetSubMenu(popup.get(), 1), 0, title, 32, MF_BYPOSITION));
    assert(std::wstring_view(title) == L"A && B");
    MENUITEMINFOW item{};
    item.cbSize = sizeof(item);
    item.fMask = MIIM_STATE | MIIM_DATA;
    assert(GetMenuItemInfoW(channel_menu, 2, TRUE, &item));
    assert(item.fState & MFS_CHECKED);
    assert(reinterpret_cast<const Item*>(item.dwItemData) == &mute);
    const auto send_position = std::find_if(channels[0].children.begin(), channels[0].children.end(), [](const auto& row) { return row.label == "发送"; }) - channels[0].children.begin();
    const auto send_list = GetSubMenu(channel_menu, static_cast<int>(send_position));
    assert(populate_popup(send_list));
    const auto send_menu = GetSubMenu(send_list, 1);
    assert(populate_popup(send_menu));
    HMENU tap_menu = GetSubMenu(send_menu, 2);
    assert(populate_popup(tap_menu));
    item.fMask = MIIM_STATE | MIIM_FTYPE;
    assert(GetMenuItemInfoW(tap_menu, 1, TRUE, &item) && (item.fType & MFT_RADIOCHECK) && (item.fState & MFS_CHECKED));
    const auto raw = popup.get();
    popup.reset();
    assert(!IsMenu(raw) && !IsMenu(tap_menu));
}

void topmost_toggle() {
    const HWND window = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 10, 20, 60, 60, nullptr, nullptr, nullptr, nullptr);
    assert(window);
    const HWND panel = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 10, 80, 60, 40, window, nullptr, nullptr, nullptr);
    assert(panel);
    RECT before{}, after{};
    assert(GetWindowRect(window, &before));
    Skin skin;
    skin.shown = skin.ready = true;
    skin.menu_owner = panel;
    for (const bool enabled : {true, false}) {
        const auto rows = menu(skin.state, skin.locale, 0, 0, window_topmost(panel));
        skin.pending = find(rows, "保持最前");
        assert(skin.pending->checked != enabled);
        assert(skin.menu_message(panel, invoke_item, skin.menu_serial, 0));
        assert(window_topmost(window) == enabled && window_topmost(panel) == enabled);
        assert(((GetWindowLongPtrW(panel, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0) == enabled);
    }
    assert(GetWindowRect(window, &after) && EqualRect(&before, &after));
    DestroyWindow(window);
}

void state_and_lifetime() {
    assert(keyboard_menu(VK_APPS, false) && keyboard_menu(VK_F10, true));
    assert(!keyboard_menu(VK_F10, false) && !keyboard_menu('A', true));
    Skin skin;
    skin.shown = true;
    const std::string snapshot = R"({"channels":[{"id":5,"name":"A","chain_uuid":"old"}]})";
    audcore_skin_event event{sizeof(event), AUDCORE_SKIN_EVENT_SNAPSHOT, 1, 0, 0,
        {reinterpret_cast<const std::uint8_t*>(snapshot.data()), static_cast<std::uint32_t>(snapshot.size())}};
    receive(&skin, &event);
    assert(skin.usable());
    skin.rows = menu(skin.state, skin.locale, 0, 0, false);
    skin.popup = make_popup(skin.rows);
    skin.pending = find(skin.rows, "关闭悬浮窗");
    const auto raw = skin.popup.get();
    const std::string replacement = R"({"channels":[{"id":5,"name":"B","chain_uuid":"new"}]})";
    event.revision = 2;
    event.payload = {reinterpret_cast<const std::uint8_t*>(replacement.data()), static_cast<std::uint32_t>(replacement.size())};
    receive(&skin, &event);
    assert(!IsMenu(raw) && skin.revision == 2 && skin.action_revision != skin.revision);
    hide(&skin);
    assert(!skin.usable() && !skin.pending);
    receive(&skin, &event);
    assert(!skin.usable());
}

void icon_alpha(HMODULE module) {
    // Exercise the same GDI path as the layered logo at two sizes.
    for (const int size : {42, 84}) {
        const auto icon = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(AUDCORE_FLOATING_ICON), IMAGE_ICON, size, size, 0));
        assert(icon);
        const auto dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), size, -size, 1, 32, BI_RGB};
        void* data = nullptr;
        const auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        assert(bitmap && data);
        const auto previous = SelectObject(dc, bitmap);
        std::memset(data, 0, size * size * 4);
        assert(DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL));
        GdiFlush();
        bool transparent = false, partial = false, opaque = false;
        const auto pixels = static_cast<const std::uint32_t*>(data);
        for (int i = 0; i < size * size; ++i) {
            const auto pixel = pixels[i], alpha = pixel >> 24;
            transparent |= alpha == 0;
            partial |= alpha > 0 && alpha < 255;
            opaque |= alpha == 255;
            assert((pixel & 255) <= alpha && ((pixel >> 8) & 255) <= alpha && ((pixel >> 16) & 255) <= alpha);
        }
        assert(transparent && partial && opaque);
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
        DestroyIcon(icon);
    }
}

int wmain(int argc, wchar_t** argv) {
    assert(argc == 2);
    model_and_win32_menu();
    topmost_toggle();
    state_and_lifetime();
    const auto module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(module);
    const auto get = reinterpret_cast<audcore_skin_get_descriptor_fn>(GetProcAddress(module, "audcore_skin_get_descriptor"));
    assert(get && get(1) == nullptr);
    const auto* metadata = get(0);
    assert(NativeSkinModule::valid_descriptor(metadata));
    assert(std::string_view(reinterpret_cast<const char*>(metadata->version.data), metadata->version.len) == version);
    icon_alpha(module);
    FreeLibrary(module);
    return 0;
}
