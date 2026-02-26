#include <cstdio>
#include <iostream>
#include <string>
#include <array>
#include "pico/stdlib.h"
#include "pico/time.h"
#include "pico/sync.h"
#include "pico/flash.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/clocks.h"

#include "keys.pio.h"
#include "deskcmd.hh"
#include "deskstate.hh"
#include "disp.hh"
#include "log.hh"
#include "movestatemachine.hh"
#include "buttonsm.hh"
#include "memslots.hh"

#define GPIO_DESK_RX 5
#define GPIO_DESK_TX 4
#define GPIO_DESK_SLEEP 28

#define GPIO_KEY_0 18

#define GPIO_LED 25

#define DESK_UART uart1
#define DESK_UART_IRQ UART1_IRQ
#define DESK_BAUD 9600

#define DESK_CMD_UP 0x02
#define DESK_CMD_DOWN 0x01
#define DESK_CMD_STOP 0x00

static int keys_sm = 0;

static DeskCmd desk_rx_cmd;
static critical_section_t desk_rx_cmd_crit_section;
static volatile uint8_t last_read_keys = 0;
static Display display;

enum class MainLoopState : uint8_t
{
    Init,
    WaitingDeskInit,
    Main
};

enum Buttons {
    Mem1 = 0,
    Mem0 = 1,
    Down = 2,
    Up = 3,
};
static const int BUTTONS_COUNT = 4;

void desk_serial_rx_isr()
{
    while (uart_is_readable(DESK_UART))
    {
        gpio_put(GPIO_LED, true);
        uint8_t c;
        uart_read_blocking(DESK_UART, &c, 1);
        desk_rx_cmd.receive(c);
    }
}

void desk_uart_init()
{
    uart_init(DESK_UART, DESK_BAUD);

    gpio_set_function(GPIO_DESK_RX, GPIO_FUNC_UART);
    gpio_pull_up(GPIO_DESK_RX);

    gpio_init(GPIO_LED);
    gpio_set_dir(GPIO_LED, GPIO_OUT);
    gpio_put(GPIO_LED, false);

    gpio_set_function(GPIO_DESK_TX, GPIO_FUNC_UART);

    critical_section_init(&desk_rx_cmd_crit_section);

    // Interrupt when rx has data
    irq_set_exclusive_handler(DESK_UART_IRQ, desk_serial_rx_isr);
    uart_set_irq_enables(DESK_UART, /* rx_has_data */ true, /* tx_needs_data */ false);
    irq_set_enabled(DESK_UART_IRQ, true);
}

void desk_waker_init()
{
    gpio_init(GPIO_DESK_SLEEP);
    gpio_set_dir(GPIO_DESK_SLEEP, GPIO_OUT);
    gpio_put(GPIO_DESK_SLEEP, true);
}

void keys_isr()
{
    if (!pio_sm_is_rx_fifo_empty(pio0, keys_sm))
    {
        uint16_t data = (uint16_t)pio_sm_get_blocking(pio0, keys_sm);
        last_read_keys = (uint8_t)(~data & 0x000f);
    }
}

void key_debounce_init()
{
    uint32_t sys_freq = clock_get_hz(clk_sys);
    uint32_t total_debounce_instructions =
        keys_DEBOUNCE_CYCLE_INSTRUCTIONS * keys_DEBOUNCE_CYCLES;
    const uint32_t target_debounce_time_ms = 5;
    uint32_t target_clk_div =
        (target_debounce_time_ms * (sys_freq / 1000)) /* instructions during the target time */
        / total_debounce_instructions;
    uint16_t clk_div = target_clk_div > UINT16_MAX ? UINT16_MAX : target_clk_div;

    uint offset = pio_add_program(pio0, &keys_program);
    keys_sm = pio_claim_unused_sm(pio0, true);
    keys_pio_init(
        pio0, keys_sm, offset, GPIO_KEY_0, clk_div);

    irq_set_enabled(PIO0_IRQ_0, true);
    pio_set_irq0_source_enabled(
        pio0,
        static_cast<pio_interrupt_source>(pis_sm0_rx_fifo_not_empty + keys_sm),
        true);
    irq_set_exclusive_handler(PIO0_IRQ_0, keys_isr);
}

void core1_entry()
{
    flash_safe_execute_core_init();
    display.run();
}

void wake_desk()
{
    gpio_put(GPIO_DESK_SLEEP, false);
    for (int i = 0; i < 14; i++) {
        if (desk_rx_cmd.is_ready()) {
            break;
        }
        sleep_ms(100);
    }
    
    gpio_put(GPIO_DESK_SLEEP, true);
}

void desk_send_cmd(uint8_t cmd)
{
    static uint8_t zeros_sent = 0;
    if (cmd == 0x00)
    {
        if (zeros_sent >= 10)
        {
            return;
        }
        else
        {
            zeros_sent++;
        }
    }
    else
    {
        zeros_sent = 0;
    }

    uint8_t msg[5] = {0xD8, 0xD8, 0x66, cmd, cmd};
    LOGD("Sending %02x", cmd);
    uart_write_blocking(DESK_UART, msg, 5); 
    uart_tx_wait_blocking(DESK_UART);
}

int main()
{
    stdio_init_all();

    key_debounce_init();
    desk_uart_init();
    desk_waker_init();
    display.init();

    // Wait a tiny bit so that serial comms / console have time to wake up
    sleep_ms(1000);
    LOGI("## Pico Desk Controller ##");

    multicore_launch_core1(core1_entry);

    if (memslots_validate()) {
        memslots_log();
    } else {
        LOGW("Mem slot data in flash invalid");
        memslots_reset();
    }

    DeskState desk_state;

    MainLoopState main_loop_state = MainLoopState::Init, prev_main_loop_state = MainLoopState::Init;
    absolute_time_t init_time;
    MoveStateMachine move_state_machine;
    std::array<ButtonSm, BUTTONS_COUNT> buttons;
    bool ignore_key_events_until_all_keys_idle = false;
    bool showing_error = false;

    for (;;)
    {
        auto main_loop_start = get_absolute_time();
        if (main_loop_state != prev_main_loop_state)
        {
            LOGD("State transition: %02x -> %02x", (uint8_t)prev_main_loop_state, (uint8_t)main_loop_state);
        }
        prev_main_loop_state = main_loop_state;

        std::optional<Display::Notification> timed_notif = std::nullopt;

        switch (main_loop_state)
        {
        case MainLoopState::Init:
        {
            init_time = get_absolute_time();
            wake_desk();
            main_loop_state = MainLoopState::WaitingDeskInit;
            break;
        }

        case MainLoopState::WaitingDeskInit:
        {
            bool ready = false;

            critical_section_enter_blocking(&desk_rx_cmd_crit_section);
            if (desk_rx_cmd.is_ready())
            {
                desk_state.update(desk_rx_cmd); // This will reset the cmd
                main_loop_state = MainLoopState::Main;
                LOGI("Got initial message from desk, starting main loop");
                ready = true;
            } else if (auto err = desk_rx_cmd.last_error()) {
                LOGW("Desk rx error: %d", (uint8_t) *err);
            }
            gpio_put(GPIO_LED, false);
            critical_section_exit(&desk_rx_cmd_crit_section);

            if (!ready && time_reached(delayed_by_ms(init_time, 20'000)))
            {
                LOGW("Waited for desk init over 20s, resetting...");
                main_loop_state = MainLoopState::Init;
            }

            break;
        }

        case MainLoopState::Main:
        {
            uint8_t keys_now = last_read_keys;

            // True if any button is currently pressed. This is determined separately from
            // key events so that we get smaller latencies for stopping the desk from moving
            bool any_button_down = false;

            for (size_t i = 0; i < buttons.size(); i++)
            {
                auto key_down = static_cast<bool>(keys_now & 0x01);
                buttons[i].tick(key_down);
                keys_now >>= 1;
                any_button_down |= key_down;
            }

            if (ignore_key_events_until_all_keys_idle)
            {
                // First clear all state machines. This handles this situation
                // - a button was shortly pressed to wake up the desk
                // - when wakeup was requested, ignore_key_events_until_all_keys_idle was set
                // - the button state machine went non-idle
                // - the first time we hit this if block after wake, the physical button
                //   is no longer pressed but the state machine is not idle --> a press is detected
                for (auto& btn : buttons)
                {
                    btn.force_idle();
                }

                if (any_button_down)
                {
                    any_button_down = false;
                }
                else
                {
                    // All keys are idle. Unset the ignore flag and proceed with normal operations
                    ignore_key_events_until_all_keys_idle = false;
                }
            }

            critical_section_enter_blocking(&desk_rx_cmd_crit_section);
            if (desk_rx_cmd.is_ready())
            {
                desk_state.update(desk_rx_cmd);
            }
            gpio_put(GPIO_LED, false);
            critical_section_exit(&desk_rx_cmd_crit_section);

            if (any_button_down && !desk_state.is_awake())
            {
                LOGD("Trying to wake desk");
                ignore_key_events_until_all_keys_idle = true;
                wake_desk();
                // Don't do anything on the same iteration that the desk was woken up
                // Waking might have taken a long time and the button state could be different.
                // It's best to just run the loop again.
                LOGD("Desk is awakened");
                break;
            }

            if (desk_state.is_awake())
            {
                move_state_machine.update_desk_state(desk_state);
                auto up_event = buttons[Buttons::Up].event();
                auto down_event = buttons[Buttons::Down].event();
                auto m0_event = buttons[Buttons::Mem0].event();
                auto m1_event = buttons[Buttons::Mem1].event();
                
                auto manual_moving_dir = move_state_machine.is_manual_moving();

                auto up_down_long_press_active = down_event == ButtonSm::ButtonEvent::LongPressDown && up_event == ButtonSm::ButtonEvent::LongPressDown;

                // Stop if currently manually moving up and up key not pressed
                if (up_event == ButtonSm::ButtonEvent::Idle && manual_moving_dir && *manual_moving_dir == MoveStateMachine::Dir::Up)
                {
                    move_state_machine.stop();
                }
                // Stop if currently manually moving down and down key not pressed
                else if (down_event == ButtonSm::ButtonEvent::Idle && manual_moving_dir && *manual_moving_dir == MoveStateMachine::Dir::Down)
                {
                    move_state_machine.stop();
                }
                // Stop if calibration moving and either of up/down is not pressed
                else if (!up_down_long_press_active && manual_moving_dir && *manual_moving_dir == MoveStateMachine::Dir::Calibration)
                {
                    move_state_machine.stop();
                }
                // If both up/down are pressed and desk is idle or manual moving, start calibration.
                // Note that start_calibration_move() does some further status checks
                else if (up_down_long_press_active &&
                    (move_state_machine.state() == MoveStateMachine::State::Idle || (manual_moving_dir && *manual_moving_dir != MoveStateMachine::Dir::Calibration)))
                {
                    move_state_machine.start_calibration_move();
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && down_event == ButtonSm::ButtonEvent::LongPressDown)
                {
                    move_state_machine.start_manual_move(MoveStateMachine::Dir::Down);
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && up_event == ButtonSm::ButtonEvent::LongPressDown)
                {
                    move_state_machine.start_manual_move(MoveStateMachine::Dir::Up);
                }
                else if (move_state_machine.is_auto_moving() && any_button_down)
                {
                    ignore_key_events_until_all_keys_idle = true;
                    move_state_machine.stop();
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && m0_event == ButtonSm::ButtonEvent::ShortPress)
                {
                    if (auto h = memslots_get(0)) {
                        move_state_machine.start_auto_move(*h, desk_state.height_cm());
                    }
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && m1_event == ButtonSm::ButtonEvent::ShortPress)
                {
                    if (auto h = memslots_get(1)) {
                        move_state_machine.start_auto_move(*h, desk_state.height_cm());
                    }
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && m0_event == ButtonSm::ButtonEvent::LongPressStart) {
                    memslots_set(0, desk_state.height_cm());
                    LOGI("Stored height %d to slot 0", desk_state.height_cm());
                    timed_notif = Display::Notification(Display::Notification::Type::Stored, 0);
                    memslots_log();
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && m1_event == ButtonSm::ButtonEvent::LongPressStart) {
                    memslots_set(1, desk_state.height_cm());
                    LOGI("Stored height %d to slot 1", desk_state.height_cm());
                    timed_notif = Display::Notification(Display::Notification::Type::Stored, 1);
                    memslots_log();
                }
                else if (move_state_machine.is_error() && !showing_error) {
                    // First error. Start waiting for a new key press
                    showing_error = true;
                    ignore_key_events_until_all_keys_idle = true;
                }
                else if (move_state_machine.is_error() && any_button_down && showing_error) {
                    ignore_key_events_until_all_keys_idle = true;
                    showing_error = false;
                    move_state_machine.clear_error();
                }

                for (auto& btn : buttons) {
                    btn.consume();
                }
                desk_send_cmd(move_state_machine.get_send_msg());
            }
        }
        }


        if (!timed_notif) {
            timed_notif = display.current_display_state().timed_notification;
        }
        auto move_dir = move_state_machine.is_manual_moving();
        if (!move_dir) {
            move_dir = move_state_machine.is_auto_moving();
        }
        display.post_display_state(Display::DisplayState{
            desk_state,
            move_state_machine.state(),
            move_dir,
            move_state_machine.target_height(),
            (main_loop_state == MainLoopState::Init || main_loop_state == MainLoopState::WaitingDeskInit),
            timed_notif,    
        });

        auto next_iter = delayed_by_ms(main_loop_start, 50);
        sleep_until(next_iter);
    }

    return 0;
}
