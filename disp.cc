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

Display::Display(uint8_t sda, uint8_t scl, i2c_inst_t *i2c)
    : sda_(sda), scl_(scl), i2c_(i2c)
{
    mutex_init(&mutex_);
}

void Display::init() {
    u8g2_SetUserPtr(U8G2, this);
    u8g2_Setup_ssd1306_i2c_128x32_univision_f(
        U8G2, U8G2_R2, pico_u8g2_byte_i2c, pico_u8g2_delay_cb);

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

void Display::draw()
{
    static uint8_t thing = 0;
    DisplayState state;

    // Take a copy of the state
    mutex_enter_blocking(&mutex_);
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

    thing++;
    u8g2_DrawBox(U8G2, thing % 128, 31, 1, 1);

    if (state.waking_desk)
    {
        u8g2_DrawStr(U8G2, 0, 20, "Waking desk...");
    }

    else if (!state.desk_state.initialized())
    {
        u8g2_DrawStr(U8G2, 0, 20, "Initializing...");
    }

    else {
        u8g2_SetFont(U8G2, u8g2_font_maniac_tn);
        auto height_str = string_format("%d", state.desk_state.height_cm()).value_or("");
        u8g2_DrawStr(U8G2, 12, 32, height_str.c_str());

        u8g2_SetFont(U8G2, u8g2_font_t0_11_mr);
        u8g2_DrawStr(U8G2, 60, 12, MoveStateMachine::state_str(state.move_state));
        if (state.move_dir.has_value()) {
            u8g2_DrawStr(U8G2, 100, 12, MoveStateMachine::dir_str(state.move_dir.value()));
        }
        /*
        if (state.desk_state.is_store_completed())
        {
            u8g2_DrawStr(U8G2, 64, 32, "Store OK!");
        }
        else if (state.desk_state.is_waiting_store())
        {
            u8g2_DrawStr(U8G2, 64, 32, "Store where?");
        }*/
    }

    u8g2_SendBuffer(U8G2);
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