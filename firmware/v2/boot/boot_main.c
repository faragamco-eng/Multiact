/*
 * boot_main.c - Cooler drive v2 bootloader (16 KB at 0x08000000).
 *
 * At reset: if the staging slot holds a valid image that differs from the
 * application, copy it over the application (an interrupted copy is
 * repeated at the next reset because the staging image stays valid).
 * Then start the application if it is valid. With neither image valid the
 * bootloader keeps all power outputs off and blinks the LED fast; the
 * board is then reprogrammed with SWD (factory).
 *
 * Runs on HSI16 without PLL; every relay, PFC and gate output stays in
 * its reset (safe) state: gate drivers have pull-downs, relays are off.
 */
#include "stm32g4xx.h"
#include "fwu.h"
#include <string.h>

#define APP_ADDR  0x08004000u
#define STG_ADDR  0x08011800u
#define SLOT_SIZE (54u * 1024u)
#define PAGE      2048u

static void flash_unlock(void)
{
    if (FLASH->CR & FLASH_CR_LOCK) { FLASH->KEYR = 0x45670123u; FLASH->KEYR = 0xCDEF89ABu; }
}
static bool flash_wait(void)
{
    while (FLASH->SR & FLASH_SR_BSY) { IWDG->KR = 0xAAAAu; }
    uint32_t err = FLASH->SR & 0xC3FAu;            /* all error flags */
    FLASH->SR = err | FLASH_SR_EOP;
    return err == 0u;
}
static bool erase(uint32_t addr)
{
    flash_wait();
    FLASH->CR = (FLASH->CR & ~FLASH_CR_PNB) | FLASH_CR_PER | (((addr - FLASH_BASE) / PAGE) << FLASH_CR_PNB_Pos);
    FLASH->CR |= FLASH_CR_STRT;
    bool ok = flash_wait();
    FLASH->CR &= ~FLASH_CR_PER;
    return ok;
}
static bool program(uint32_t dst, const uint8_t *src, uint32_t len)
{
    bool ok = true;
    FLASH->CR |= FLASH_CR_PG;
    for (uint32_t i = 0; i < len && ok; i += 8u) {
        uint32_t w0, w1;
        memcpy(&w0, src + i, 4);
        memcpy(&w1, src + i + 4, 4);
        *(volatile uint32_t *)(dst + i) = w0;
        __ISB();
        *(volatile uint32_t *)(dst + i + 4u) = w1;
        ok = flash_wait();
    }
    FLASH->CR &= ~FLASH_CR_PG;
    return ok;
}

static bool copy_staging(void)
{
    img_info_t h;
    if (!img_check((const uint8_t *)STG_ADDR, SLOT_SIZE, &h)) return false;
    uint32_t len = (h.size + 7u) & ~7u;
    flash_unlock();
    bool ok = true;
    for (uint32_t a = 0; a < len && ok; a += PAGE) ok = erase(APP_ADDR + a);
    for (uint32_t a = 0; a < len && ok; a += 256u) {
        uint32_t n = (len - a) < 256u ? (len - a) : 256u;
        ok = program(APP_ADDR + a, (const uint8_t *)(STG_ADDR + a), n);
    }
    FLASH->CR |= FLASH_CR_LOCK;
    return ok;
}

static void jump_to_app(void)
{
    uint32_t sp = *(const uint32_t *)APP_ADDR;
    uint32_t pc = *(const uint32_t *)(APP_ADDR + 4u);
    if ((sp & 0xFFF00000u) != 0x20000000u) return;
    __disable_irq();
    SysTick->CTRL = 0;
    SCB->VTOR = APP_ADDR;
    __DSB(); __ISB();
    __set_MSP(sp);
    __enable_irq();
    ((void (*)(void))pc)();
}

int main(void)
{
    boot_action_t act = boot_decide((const uint8_t *)APP_ADDR, (const uint8_t *)STG_ADDR, SLOT_SIZE);
    if (act == BOOT_COPY) {
        copy_staging();
        act = boot_decide((const uint8_t *)APP_ADDR, (const uint8_t *)STG_ADDR, SLOT_SIZE);
    }
    if (act == BOOT_APP) jump_to_app();
    /* no valid application: LED PA12 blinks at 5 Hz, everything else stays off */
    RCC->AHB2ENR |= RCC_AHB2ENR_GPIOAEN;
    (void)RCC->AHB2ENR;
    GPIOA->MODER = (GPIOA->MODER & ~(3u << 24)) | (1u << 24);
    for (;;) {
        GPIOA->ODR ^= 1u << 12;
        for (volatile uint32_t i = 0; i < 160000u; i++) { IWDG->KR = 0xAAAAu; }
    }
}
