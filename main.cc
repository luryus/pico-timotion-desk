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
#define GPIO_DESK_SLEEP 2

#define GPIO_KEY_0 16

#define GPIO_LED 25

#define DESK_UART uart1
#define DESK_UART_IRQ UART1_IRQ
#define DESK_BAUD 9600

#define DESK_CMD_UP 0x02
#define DESK_CMD_DOWN 0x01
#define DESK_CMD_STOP 0x00

#define KEY_MASK_UP 0x01
#define KEY_MASK_DOWN 0x02
#define KEY_MASK_MEM_1 0x04
#define KEY_MASK_MEM_2 0x08

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

void desk_serial_rx_isr()
{
    while (uart_is_readable(DESK_UART))
    {
        gpio_put(GPIO_LED, true);
        uint8_t c;
        uart_read_blocking(DESK_UART, &c, 1);
        desk_rx_cmd.update(c);
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
    sleep_ms(1500);
    gpio_put(GPIO_DESK_SLEEP, true);
    sleep_ms(500);
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
        auto memslots_data = memslots_get_all();
        LOGI("Stored mem slots: [0]: %d, [1]: %d, [2]: %d, [3]: %d",
            memslots_data[0].value_or(0), memslots_data[1].value_or(0),
            memslots_data[2].value_or(0), memslots_data[3].value_or(0));
    } else {
        LOGW("Mem slot data in flash invalid");
        memslots_reset();
    }

    DeskState desk_state;

    MainLoopState main_loop_state = MainLoopState::Init, prev_main_loop_state = MainLoopState::Init;
    absolute_time_t init_time;
    MoveStateMachine move_state_machine;
    std::array<ButtonSm, 4> buttons;

    for (;;)
    {
        auto main_loop_start = get_absolute_time();
        if (main_loop_state != prev_main_loop_state)
        {
            LOGD("State transition: %02x -> %02x", (uint8_t)prev_main_loop_state, (uint8_t)main_loop_state);
        }
        prev_main_loop_state = main_loop_state;

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
            bool any_button_down = false;
            for (size_t i = 0; i < buttons.size(); i++)
            {
                buttons[i].tick(static_cast<bool>(keys_now & 0x01));
                keys_now >>= 1;

                auto event = buttons[i].event();
                any_button_down |= (event != ButtonSm::ButtonEvent::Idle);

                if (event != ButtonSm::ButtonEvent::Idle)
                {
                    LOGD("Button %u: %u", i, (uint8_t)event);
                };
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
                wake_desk();
            }

            if (desk_state.is_awake())
            {
                move_state_machine.update_desk_state(desk_state);
                auto up_event = buttons[0].event();
                auto down_event = buttons[1].event();
                auto m1_event = buttons[2].event();
                if (up_event == ButtonSm::ButtonEvent::Idle && down_event == ButtonSm::ButtonEvent::Idle && move_state_machine.is_manual_moving())
                {
                    move_state_machine.stop();
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
                    move_state_machine.stop();
                }
                else if (move_state_machine.state() == MoveStateMachine::State::Idle && m1_event == ButtonSm::ButtonEvent::ShortPress)
                {
                    move_state_machine.start_auto_move(100);
                }

                desk_send_cmd(move_state_machine.get_send_msg());
            }
        }
        }

        display.post_display_state(Display::DisplayState{
            desk_state,
            move_state_machine.state(),
            move_state_machine.is_manual_moving(),
            (main_loop_state == MainLoopState::Init || main_loop_state == MainLoopState::WaitingDeskInit)});

        auto next_iter = delayed_by_ms(main_loop_start, 50);
        sleep_until(next_iter);
    }

    return 0;
}
