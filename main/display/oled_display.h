#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>

// One face "pose": eye / eyebrow / mouth geometry in pixels.
// Screen is 128x64; the top 16px belong to the status text.
struct FacePose {
    int eye_w = 24, eye_h = 24, eye_y = 22, eye_r = 8, eye_gap = 16;
    int brow = 0;          // 0 none, 1 low & flat (angry/smug), 2 raised (sad), 3 high (surprised)
    int asym = 0;          // right eye height minus left eye height (confused/silly)
    bool wink = false;     // left eye closed
    int look_dx = 0;       // gaze offset in pixels
    int mouth_w = 26, mouth_h = 6, mouth_y = 52, mouth_r = 3, mouth_dx = 0;
    int mouth_style = 0;   // 0 plain, 1 smile (flat top), 2 frown (flat bottom)
};

class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    // lv_obj_t *emotion_label_ = nullptr;
    // lv_obj_t* chat_message_label_ = nullptr;

    // virtual bool Lock(int timeout_ms = 0) override;
    // virtual void Unlock() override;

    // void SetupUI_128x64();
    // void SetupUI_128x32();
    lv_obj_t* emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;

    // Robot face drawn from shapes (not glyphs)
    lv_obj_t* left_eye_ = nullptr;
    lv_obj_t* right_eye_ = nullptr;
    lv_obj_t* mouth_ = nullptr;

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();
    void SetupFace();
    void SetFaceShape(const char* emotion);

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
};

#endif // OLED_DISPLAY_H
