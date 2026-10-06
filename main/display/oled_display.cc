#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_font.h"
#include "lvgl_theme.h"

#include <algorithm>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_lvgl_port.h>
#include <material_symbols.h>
#include <noto_emoji.h>

#include "application.h"
#include <esp_random.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_material_symbols_30_1);
LV_FONT_DECLARE(font_noto_emoji_30_1);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                         int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_material_symbols_30_1);
    auto emoji_font = std::make_shared<LvglBuiltInFont>(&font_noto_emoji_30_1);

    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);
    dark_theme->set_emoji_font(emoji_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation =
            {
                .swap_xy = false,
                .mirror_x = mirror_x,
                .mirror_y = mirror_y,
            },
        .flags =
            {
                .buff_dma = 1,
                .buff_spiram = 0,
                .sw_rotate = 0,
                .full_refresh = 0,
                .direct_mode = 0,
            },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() should be called by Application::Initialize(), not in constructor
    // to ensure lvgl objects are created after the display is fully initialized.
}

void OledDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    Display::SetupUI();  // Mark SetupUI as called
    if (height_ == 64) {
        SetupUI_128x64();
    } else {
        SetupUI_128x32();
    }
}

OledDisplay::~OledDisplay() {
    if (content_ != nullptr) {
        lv_obj_del(content_);
    }

    bool is_128x64_layout = (top_bar_ != nullptr);
    if (status_bar_ != nullptr && is_128x64_layout) {
        status_label_ = nullptr;
        notification_label_ = nullptr;
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        if (!is_128x64_layout) {
            status_label_ = nullptr;
            notification_label_ = nullptr;
            network_label_ = nullptr;
            mute_label_ = nullptr;
            battery_label_ = nullptr;
        }
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) { return lvgl_port_lock(timeout_ms); }

void OledDisplay::Unlock() { lvgl_port_unlock(); }

void OledDisplay::SetChatMessage(const char* role, const char* content) {
      // Robot-face layout: chat text is intentionally not shown.-----------------
    if (left_eye_ != nullptr) {
        return;
    }
  //----------------Added above block so that chat text won't cover the face--------
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    // Replace all newlines with spaces
    std::string content_str = content;
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');

    lv_anim_delete(chat_message_label_, nullptr);
    if (content_right_ == nullptr) {
        lv_label_set_text(chat_message_label_, content_str.c_str());
    } else {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(chat_message_label_, content_str.c_str());
            lv_obj_remove_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);        // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0);

    content_left_ = lv_obj_create(content_);
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    lv_obj_set_style_border_width(content_left_, 0, 0);

    emotion_label_ = lv_label_create(content_left_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);

    content_right_ = lv_obj_create(content_);
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_right_, 0, 0);
    lv_obj_set_style_border_width(content_right_, 0, 0);
    lv_obj_set_flex_grow(content_right_, 1);
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_right_);
    lv_label_set_text(chat_message_label_, "");
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(chat_message_label_, width_ - 32);
    lv_obj_set_style_pad_top(chat_message_label_, 14, 0);

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
  //--------Added face Setup-------------------------------------
    SetupFace();
}
// ===== Robot face =====
namespace {

struct FaceEntry {
    const char* name;
    FacePose pose;
};

// Fields are listed in struct order; anything omitted keeps the neutral default.
const FaceEntry kFaces[] = {
    {"neutral",     {}},
    {"happy",       {.eye_h = 18, .eye_y = 24, .eye_r = 9, .mouth_w = 34, .mouth_h = 10, .mouth_y = 47, .mouth_style = 1}},
    {"laughing",    {.eye_h = 8, .eye_y = 30, .eye_r = 4, .mouth_w = 42, .mouth_h = 14, .mouth_y = 45, .mouth_style = 1}},
    {"funny",       {.eye_h = 20, .eye_y = 23, .mouth_w = 38, .mouth_h = 12, .mouth_y = 46, .mouth_style = 1}},
    {"silly",       {.eye_h = 20, .eye_y = 22, .asym = 8, .mouth_w = 30, .mouth_h = 10, .mouth_y = 47, .mouth_dx = 4, .mouth_style = 1}},
    {"sad",         {.eye_w = 20, .eye_h = 18, .eye_y = 27, .eye_r = 9, .brow = 2, .mouth_w = 24, .mouth_h = 7, .mouth_y = 52, .mouth_style = 2}},
    {"crying",      {.eye_w = 22, .eye_h = 12, .eye_y = 29, .eye_r = 6, .brow = 2, .mouth_w = 20, .mouth_h = 12, .mouth_y = 49, .mouth_r = 6}},
    {"angry",       {.eye_h = 14, .eye_y = 27, .eye_r = 3, .brow = 1, .mouth_w = 28, .mouth_h = 7, .mouth_y = 52, .mouth_style = 2}},
    {"surprised",   {.eye_h = 26, .eye_y = 22, .eye_r = 13, .eye_gap = 14, .brow = 3, .mouth_w = 12, .mouth_h = 13, .mouth_y = 49, .mouth_r = 7}},
    {"shocked",     {.eye_w = 28, .eye_h = 28, .eye_y = 19, .eye_r = 14, .eye_gap = 12, .mouth_w = 16, .mouth_h = 14, .mouth_y = 48, .mouth_r = 8}},
    {"thinking",    {.eye_w = 22, .eye_h = 22, .eye_r = 11, .look_dx = 5, .mouth_w = 10, .mouth_h = 6, .mouth_y = 53, .mouth_dx = 8}},
    {"confused",    {.eye_h = 22, .eye_y = 24, .asym = -8, .mouth_w = 16, .mouth_h = 4, .mouth_y = 54, .mouth_r = 2, .mouth_dx = -6}},
    {"winking",     {.eye_h = 22, .eye_y = 23, .wink = true, .mouth_w = 30, .mouth_h = 9, .mouth_y = 48, .mouth_style = 1}},
    {"loving",      {.eye_w = 26, .eye_h = 26, .eye_y = 20, .eye_r = 13, .eye_gap = 12, .mouth_w = 26, .mouth_h = 8, .mouth_y = 50, .mouth_style = 1}},
    {"embarrassed", {.eye_w = 18, .eye_h = 18, .eye_y = 26, .eye_r = 9, .eye_gap = 22, .look_dx = -3, .mouth_w = 12, .mouth_h = 5, .mouth_y = 54, .mouth_r = 2, .mouth_dx = -3}},
    {"delicious",   {.eye_h = 10, .eye_y = 28, .eye_r = 5, .mouth_w = 36, .mouth_h = 14, .mouth_y = 46, .mouth_style = 1}},
    {"confident",   {.eye_h = 16, .eye_y = 27, .eye_r = 5, .brow = 1, .mouth_w = 26, .mouth_h = 6, .mouth_y = 51, .mouth_dx = 4, .mouth_style = 1}},
    {"cool",        {.eye_w = 28, .eye_h = 10, .eye_y = 27, .eye_r = 3, .eye_gap = 8, .mouth_w = 22, .mouth_h = 5, .mouth_y = 52, .mouth_dx = 4, .mouth_style = 1}},
    {"kissy",       {.eye_h = 10, .eye_y = 28, .eye_r = 5, .mouth_w = 9, .mouth_h = 9, .mouth_y = 51, .mouth_r = 5}},
    {"relaxed",     {.eye_h = 6, .eye_y = 31, .eye_r = 3, .mouth_w = 24, .mouth_h = 6, .mouth_y = 51, .mouth_style = 1}},
    {"sleepy",      {.eye_w = 22, .eye_h = 4, .eye_y = 33, .eye_r = 2, .mouth_w = 10, .mouth_h = 9, .mouth_y = 51, .mouth_r = 5}},
};

const FacePose* FindExact(const std::string& name) {
    for (const auto& f : kFaces) {
        if (name == f.name) return &f.pose;
    }
    return nullptr;
}

// Exact name first, then loose keyword match, otherwise neutral.
const FacePose& FindPose(const std::string& e) {
    if (auto p = FindExact(e)) return *p;

    struct Alias { const char* key; const char* target; };
    static const Alias kAliases[] = {
        {"laugh", "laughing"}, {"happ", "happy"}, {"joy", "happy"}, {"fun", "funny"},
        {"cry", "crying"}, {"sad", "sad"}, {"ang", "angry"}, {"mad", "angry"},
        {"surpris", "surprised"}, {"shock", "shocked"}, {"think", "thinking"},
        {"confus", "confused"}, {"wink", "winking"}, {"lov", "loving"},
        {"embarrass", "embarrassed"}, {"yum", "delicious"}, {"confiden", "confident"},
        {"kiss", "kissy"}, {"relax", "relaxed"}, {"sleep", "sleepy"}, {"tired", "sleepy"},
    };
    for (const auto& a : kAliases) {
        if (e.find(a.key) != std::string::npos) {
            if (auto p = FindExact(a.target)) return *p;
        }
    }
    return kFaces[0].pose;
}

}  // namespace

// Creates the face objects. The whole normal UI (icons, emoji, chat text)
// lives inside container_, so hiding that leaves only the status bar
// (Listening/Speaking) plus the face drawn here.
void OledDisplay::SetupFace() {
    auto screen = lv_screen_active();
    lv_obj_add_flag(container_, LV_OBJ_FLAG_HIDDEN);

    lv_color_t bg = lv_obj_get_style_bg_color(screen, LV_PART_MAIN);

    auto make_box = [screen](lv_color_t color) {
        lv_obj_t* o = lv_obj_create(screen);
        lv_obj_remove_style_all(o);
        lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(o, color, 0);
        lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
        return o;
    };

    left_eye_   = make_box(lv_color_black());
    right_eye_  = make_box(lv_color_black());
    left_brow_  = make_box(lv_color_black());
    right_brow_ = make_box(lv_color_black());
    mouth_      = make_box(lv_color_black());
    mouth_mask_ = make_box(bg);   // same colour as the background; hides half of the mouth ellipse

    lv_timer_create(FaceTimerCb, 60, this);   // animation tick every 60 ms
    SetFaceShape("neutral");
}

void OledDisplay::SetFaceShape(const char* emotion) {
    if (left_eye_ == nullptr) {
        return;
    }
    std::string e = (emotion == nullptr) ? "neutral" : emotion;
    ESP_LOGI(TAG, "Face emotion: %s", e.c_str());   // watch this in the serial log
    pose_ = FindPose(e);
    talk_h_ = 0;
    ApplyFace();
}

// Draws the current pose plus the live animation state (blink, glance, talking).
void OledDisplay::ApplyFace() {
    if (left_eye_ == nullptr) {
        return;
    }
    const FacePose& p = pose_;
    const int half_w = LV_HOR_RES / 2;
    const int dx = p.look_dx + glance_dx_;
    const bool blinking = blink_left_ > 0;

    // ---- eyes (kept vertically centred on one line so blinking looks natural)
    int lh = p.eye_h;
    int rh = std::max(3, p.eye_h + p.asym);
    if (p.wink || blinking) lh = 3;
    if (blinking) rh = 3;
    const int cy = p.eye_y + p.eye_h / 2;
    const int lx = half_w - p.eye_gap / 2 - p.eye_w + dx;
    const int rx = half_w + p.eye_gap / 2 + dx;

    lv_obj_set_size(left_eye_, p.eye_w, lh);
    lv_obj_set_pos(left_eye_, lx, cy - lh / 2);
    lv_obj_set_style_radius(left_eye_, p.eye_r, 0);
    lv_obj_set_size(right_eye_, p.eye_w, rh);
    lv_obj_set_pos(right_eye_, rx, cy - rh / 2);
    lv_obj_set_style_radius(right_eye_, p.eye_r, 0);

    // ---- eyebrows
    if (p.brow == 0) {
        lv_obj_add_flag(left_brow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(right_brow_, LV_OBJ_FLAG_HIDDEN);
    } else {
        const int by = p.eye_y - (p.brow == 1 ? 4 : (p.brow == 2 ? 7 : 6));
        lv_obj_remove_flag(left_brow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(right_brow_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(left_brow_, p.eye_w, 3);
        lv_obj_set_pos(left_brow_, lx, by);
        lv_obj_set_size(right_brow_, p.eye_w, 3);
        lv_obj_set_pos(right_brow_, rx, by);
    }

    // ---- mouth
    int mh = (talk_h_ > 0) ? talk_h_ : p.mouth_h;
    const int mw = p.mouth_w;
    const int mx = half_w - mw / 2 + p.mouth_dx;

    if (p.mouth_style == 0) {
        // Plain rounded bar / circle. Keep its centre fixed while it opens and closes.
        int my = (p.mouth_y + p.mouth_h / 2) - mh / 2;
        if (my + mh > 63) my = 63 - mh;
        lv_obj_set_size(mouth_, mw, mh);
        lv_obj_set_pos(mouth_, mx, my);
        lv_obj_set_style_radius(mouth_, p.mouth_r, 0);
        lv_obj_add_flag(mouth_mask_, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Half ellipse: draw a full ellipse, then cover one half with a background-coloured box.
        const int flat = (p.mouth_style == 1) ? p.mouth_y : (p.mouth_y + p.mouth_h);
        mh = std::min(mh, 63 - flat);
        if (mh < 2) mh = 2;
        lv_obj_set_size(mouth_, mw, mh * 2);
        lv_obj_set_pos(mouth_, mx, flat - mh);
        lv_obj_set_style_radius(mouth_, LV_RADIUS_CIRCLE, 0);

        lv_obj_remove_flag(mouth_mask_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(mouth_mask_, mw + 2, mh + 1);
        if (p.mouth_style == 1) {
            lv_obj_set_pos(mouth_mask_, mx - 1, flat - mh - 1);   // hide the top half -> smile
        } else {
            lv_obj_set_pos(mouth_mask_, mx - 1, flat);            // hide the bottom half -> frown
        }
    }
}

// Runs every 60 ms inside the LVGL task: blinking, eye glances, talking mouth.
void OledDisplay::TickFace() {
    if (left_eye_ == nullptr) {
        return;
    }
    bool changed = false;

    // Blink: eyes closed for ~180 ms, then wait 2.4-7 s for the next one
    if (blink_left_ > 0) {
        if (--blink_left_ == 0) changed = true;
    } else if (--next_blink_ <= 0) {
        blink_left_ = 3;
        next_blink_ = 40 + static_cast<int>(esp_random() % 80);
        changed = true;
    }

    // Talking: random mouth opening every 120 ms while the assistant speaks
    const bool speaking = Application::GetInstance().GetDeviceState() == kDeviceStateSpeaking;
    if (speaking) {
        if ((talk_tick_++ & 1) == 0) {
            const bool plain = (pose_.mouth_style == 0);
            const int range = plain ? 11 : (pose_.mouth_h + 4);
            talk_h_ = (plain ? 4 : 3) + static_cast<int>(esp_random() % range);
            changed = true;
        }
    } else if (talk_h_ != 0) {
        talk_h_ = 0;
        talk_tick_ = 0;
        changed = true;
    }

    // Glance: eyes drift a few pixels left/right now and then
    if (--next_glance_ <= 0) {
        static const int kGlance[3] = {-4, 0, 4};
        glance_dx_ = kGlance[esp_random() % 3];
        next_glance_ = 40 + static_cast<int>(esp_random() % 60);
        changed = true;
    }

    if (changed) {
        ApplyFace();
    }
}

void OledDisplay::FaceTimerCb(lv_timer_t* timer) {
    auto self = static_cast<OledDisplay*>(lv_timer_get_user_data(timer));
    if (self != nullptr) {
        self->TickFace();
    }
}

void OledDisplay::SetupUI_128x32() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, 32, 32);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, MATERIAL_SYMBOLS_ROBOT_2);
    lv_obj_center(emotion_label_);

    /* Right side */
    side_bar_ = lv_obj_create(container_);
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    lv_obj_set_style_radius(side_bar_, 0, 0);
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(side_bar_);
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    chat_message_label_ = lv_label_create(side_bar_);
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(chat_message_label_, "");

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000),
                                   LV_PART_MAIN);
}

void OledDisplay::SetEmotion(const char* emotion) {
      // Drawn robot face takes over when it exists (128x64 layout).
    if (left_eye_ != nullptr) {
        DisplayLockGuard lock(this);
        SetFaceShape(emotion);
        return;
    }
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    const char* utf8 = noto_emoji_get_utf8(emotion);
    const lv_font_t* emotion_font = lvgl_theme->emoji_font()->font();
    if (utf8 == nullptr) {
        utf8 = material_symbols_get_utf8(emotion);
        emotion_font = lvgl_theme->large_icon_font()->font();
    }
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    if (utf8 != nullptr) {
        lv_obj_set_style_text_font(emotion_label_, emotion_font, 0);
        lv_label_set_text(emotion_label_, utf8);
    } else {
        lv_obj_set_style_text_font(emotion_label_, lvgl_theme->emoji_font()->font(), 0);
        lv_label_set_text(emotion_label_, NOTO_EMOJI_NEUTRAL);
    }
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
}
