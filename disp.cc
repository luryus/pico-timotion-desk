#include "disp.hh"

#include <format>
#include <string>

#include "pico/time.h"
#include "pico/stdlib.h"

#include "log.hh"
#include "string_format.hh"

#define FPS 20
#define FRAME_DURATION_US (1'000'000 / FPS)

#define U8G2 (&u8g2_)

/*
  Fontname: DeskFontNum32
  Copyright: Designer of this font retains full rights under the law
  Glyphs: 16/16
  BBX Build Mode: 0
*/
static const uint8_t desk_font_32n[241] U8G2_FONT_SECTION("desk_font_32n") = 
  "\20\0\4\6\4\6\5\6\6\21 \0\340\0\0\0\0\0\0\0\0\0\324+\17\314H\245%\2\22#"
  "\30\21\220\30\1\0,\7D\310\340\4\10-\7\71H\247\205\15.\7D\310\340\4\10/\6\0@\260"
  "\5\60\17\17J`\206\37\4\16\371\377\377\335\17\2\61\11\4v`\206\77\20\0\62\21\17J`\206\37"
  "\60\226\370\333\37P\226\370[\36\63\21\17J`\6\336\22\377IsK\374\333\37\4\0\64\20\17J`"
  "\6\302!\377\357~\20X\342\377\3\65\21\17J`\206\37\4\226\370\267\274%\376\355\17\2\66\21\17J"
  "`\206\37\17\377\370\7\201C\376\335\17\2\67\14\17J`\6\336\22\377\377\377\7\70\23\17J`\206\37"
  "\4\16\371w\77(\34\362\357~\20\0\71\23\17J`\206\37\4\16\371w\77\10,\361o\177\20\0:"
  "\12\4\311\343\4\310\3\11\4\0\0\0\4\377\377\0";

static const uint8_t ARROW_WIDTH = 16;
static const uint8_t ARROW_HEIGHT = 10;
// XBM bitmap
static const uint8_t UP_ARROW[20] = {
   0x80, 0x01, 
   0xc0, 0x03, 
   0xe0, 0x07, 
   0xf0, 0x0f, 
   0xf8, 0x1f,
   0xfc, 0x3f,
   0x3e, 0x7c, 
   0x03, 0xc0, 
   0x00, 0x00, 
   0x00, 0x00,
};
static const uint8_t DOWN_ARROW[20] = {
   0x00, 0x00, 
   0x00, 0x00,
   0x03, 0xc0, 
   0x3e, 0x7c, 
   0xfc, 0x3f,
   0xf8, 0x1f,
   0xf0, 0x0f, 
   0xe0, 0x07, 
   0xc0, 0x03, 
   0x80, 0x01, 
};


Display::Display(uint8_t sda, uint8_t scl, i2c_inst_t *i2c)
    : sda_(sda), scl_(scl), i2c_(i2c)
{
    mutex_init(&mutex_);
}

void Display::init() {
    u8g2_SetUserPtr(U8G2, this);
    u8g2_Setup_ssd1306_i2c_128x32_univision_f(
        U8G2, U8G2_R0, pico_u8g2_byte_i2c, pico_u8g2_delay_cb);

    u8g2_InitDisplay(U8G2);
    u8g2_SetPowerSave(U8G2, false);

    u8g2_ClearDisplay(U8G2);
    initialized_ = true;
}

void Display::post_display_state(DisplayState state)
{
    if (!initialized_) {
        return;
    }

    mutex_enter_blocking(&mutex_);
    state_ = state;
    mutex_exit(&mutex_);
}

void Display::run()
{
    if (!initialized_) {
        return;
    }

    absolute_time_t frame_ts = get_absolute_time();
    for (;;)
    {
        draw();

        while (time_reached(frame_ts))
        {
            frame_ts = delayed_by_us(frame_ts, FRAME_DURATION_US);
        }
        sleep_until(frame_ts);
    }
}

Display::DisplayState const &Display::current_display_state() const
{
    return state_;
}

void Display::draw()
{
    DisplayState state;
    std::optional<Notification> valid_notification = std::nullopt;

    // Take a copy of the state
    mutex_enter_blocking(&mutex_);
    // Advance the notification timer while inside the mutex
    if (state_.timed_notification && state_.timed_notification->advance()) {
        valid_notification = state_.timed_notification;
    } else {
        state_.timed_notification.reset();
    }
    state = state_;
    mutex_exit(&mutex_);

    bool should_be_on = state.desk_state.state_age_ms() < 400 || !state.desk_state.initialized() || state.waking_desk;
    if (!should_be_on)
    {
        set_sleep_state(true);
        return;
    }

    set_sleep_state(false);

    u8g2_ClearBuffer(U8G2);
    u8g2_SetFont(U8G2, u8g2_font_t0_14_mr);
    u8g2_SetDrawColor(U8G2, 1);

    if (state.waking_desk)
    {
        u8g2_DrawStr(U8G2, 0, 20, "Waking desk...");
    }

    else if (!state.desk_state.initialized())
    {
        u8g2_DrawStr(U8G2, 0, 20, "Initializing...");
    }

    else {
        u8g2_SetFont(U8G2, desk_font_32n);
        auto height_str = string_format("%d", state.desk_state.height_cm()).value_or("");
        u8g2_DrawStr(U8G2, 0, 0, height_str.c_str());

        u8g2_SetFont(U8G2, u8g2_font_t0_11_mr);

        if (state.move_state != MoveStateMachine::State::Idle) {
            u8g2_DrawStr(U8G2, 60, 12, MoveStateMachine::state_str(state.move_state));
            if (state.target_height.has_value())
            {
                auto target_height_str = string_format("to %d", *state.target_height).value_or("");
                u8g2_DrawStr(U8G2, 60, 26, target_height_str.c_str());
            }
            if (state.move_dir.has_value())
            {
                if (!state.target_height.has_value())
                {
                    u8g2_DrawStr(U8G2, 60, 26, MoveStateMachine::dir_str(state.move_dir.value()));
                }

                switch (*state.move_dir) {
                case MoveStateMachine::Dir::Up:
                    draw_up_anim();
                    break;
                case MoveStateMachine::Dir::Down:
                case MoveStateMachine::Dir::Calibration:
                    draw_down_anim();
                    break;
                }
            } 
        } else {
            // The desk is idle. Show notification if present
            if (valid_notification) {
                switch (valid_notification->type()) {
                    case Notification::Type::Stored:
                        uint8_t slot = valid_notification->param();
                        u8g2_DrawStr(U8G2, 60, 12, "Stored to");
                        auto slot_str = string_format("slot %d", slot).value_or("");
                        u8g2_DrawStr(U8G2, 60, 26, slot_str.c_str());
                        break;
                }
            }
        }

    }

    u8g2_SendBuffer(U8G2);
}

void Display::draw_anim(int8_t anim_dir, const uint8_t* bitmap)
{
    static uint8_t frame = 0;

    const uint8_t x = 128 - ARROW_WIDTH - 8;
    const uint8_t h = 32; // Display height

    for (uint8_t i = 0; i < h; i += ARROW_HEIGHT) {
        int8_t y = i - frame;
        uint8_t arrow_height = ARROW_HEIGHT;
        uint8_t bitmap_start_offset = 0;
        while (y < 0) {
            y++;
            arrow_height--;
            bitmap_start_offset += 2;
        }

        u8g2_DrawXBMP(U8G2, x, y, ARROW_WIDTH, arrow_height, bitmap + bitmap_start_offset);
    }

    frame = (frame + 10 + anim_dir) % 10;
}

void Display::draw_up_anim()
{
    draw_anim(1, UP_ARROW);
}

void Display::draw_down_anim()
{
    draw_anim(-1, DOWN_ARROW);
}

void Display::set_sleep_state(bool sleep_state)
{
    if (sleep_state == in_sleep_)
    {
        return;
    }

    in_sleep_ = sleep_state;
    u8g2_SetPowerSave(U8G2, sleep_state);
}

uint8_t Display::pico_u8g2_delay_cb(
    u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    // Only implements the necessary messages for using Pico's built-in i2c

    switch (msg)
    {
    case U8X8_MSG_DELAY_MILLI:
        // arg_int * 1 ms delay
        sleep_ms(arg_int);
        break;
    default:
        LOGW("pico_u8g2_delay_cb called with unimplemented msg %u", msg);
        u8x8_SetGPIOResult(u8x8, 1); // default return value
        break;
    }
    return 1;
}

void Display::init_i2c() const
{
    i2c_init(i2c_, 400 * 1000);
    gpio_set_function(sda_, GPIO_FUNC_I2C);
    gpio_set_function(scl_, GPIO_FUNC_I2C);
    gpio_pull_up(sda_);
    gpio_pull_up(scl_);
}

uint8_t Display::pico_u8g2_byte_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    Display *self = (Display *)u8g2_GetUserPtr(u8x8);
    static uint8_t buffer[32]; // At most 32 bytes sent between start/end transfer
    static uint8_t buffer_size = 0;

    switch (msg)
    {
    case U8X8_MSG_BYTE_INIT:
        self->init_i2c();
        break;

    case U8X8_MSG_BYTE_SEND:
    {
        // Store the data to the buffer, later sent when transfer ends

        // arg_ptr: pointer to first byte in data array
        // arg_int: size of the data array
        uint8_t *data_ptr = (uint8_t *)arg_ptr;
        while (arg_int-- > 0)
        {
            assert(buffer_size < 32);
            buffer[buffer_size++] = *(data_ptr++);
        }
        break;
    }

    case U8X8_MSG_BYTE_START_TRANSFER:
        // Reset buffer
        buffer_size = 0;
        break;

    case U8X8_MSG_BYTE_END_TRANSFER:
    {
        // Send the data
        assert(buffer_size > 0);
        uint8_t address = u8x8_GetI2CAddress(u8x8) >> 1;
        int written = i2c_write_blocking(self->i2c_, address, buffer, buffer_size, false);
        if (written != buffer_size)
        {
            LOGE("Error writing i2c data: %d", written);
            return 0;
        }
        break;
    }

    default:
        LOGW("pico_u8g2_byte_i2c called with unimplemented message: %d", msg);
        return 0;
    }
    return 1;
}