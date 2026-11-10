// SPDX-License-Identifier: proprietary
//
// Hardware profiling macros for the LTC6811 Open-Wire Detection (ADOW) cycle.
// Allows precision timing measurements with an oscilloscope or logic analyzer
// by toggling a debug GPIO pin via single-cycle atomic BSRR register writes.
//
// Profiled phases:
//   1. ADC Conversion: covers the two ADOW conversion passes (PUP settling + read pass)
//   2. isoSPI Transfer: covers RDCFGA warm-up and RDCVA..D register group reads
//   3. MCU Computation: covers cell decoding and open_wire algorithm evaluation

#pragma once

#include "ams_config.hpp"
#include <cstdint>

#if defined(ENABLE_ADOW_PROFILING) && (ENABLE_ADOW_PROFILING != 0) && !defined(UNIT_TEST)
#include "main.h"

#ifndef ADOW_PROF_PORT
#define ADOW_PROF_PORT       GPIOB
#endif
#ifndef ADOW_PROF_PIN
#define ADOW_PROF_PIN        GPIO_PIN_1
#endif

namespace ams::bms_profiling {
inline void init_profiling_gpio() noexcept {
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g{};
    g.Pin = ADOW_PROF_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(ADOW_PROF_PORT, &g);
    ADOW_PROF_PORT->BSRR = (static_cast<std::uint32_t>(ADOW_PROF_PIN) << 16U);
}
}  // namespace ams::bms_profiling

// Set pin high using atomic BSRR write
#define ADOW_PROF_PIN_HIGH() do { ADOW_PROF_PORT->BSRR = ADOW_PROF_PIN; } while (0)
// Set pin low using atomic BSRR write (upper 16 bits reset the pin)
#define ADOW_PROF_PIN_LOW()  do { ADOW_PROF_PORT->BSRR = (static_cast<std::uint32_t>(ADOW_PROF_PIN) << 16U); } while (0)

#define ADOW_PROF_ADC_START()       ADOW_PROF_PIN_HIGH()
#define ADOW_PROF_ADC_STOP()        ADOW_PROF_PIN_LOW()

#define ADOW_PROF_ISOSPI_START()    ADOW_PROF_PIN_HIGH()
#define ADOW_PROF_ISOSPI_STOP()     ADOW_PROF_PIN_LOW()

#define ADOW_PROF_MCU_START()       ADOW_PROF_PIN_HIGH()
#define ADOW_PROF_MCU_STOP()        ADOW_PROF_PIN_LOW()

#else

namespace ams::bms_profiling {
inline void init_profiling_gpio() noexcept {}
}  // namespace ams::bms_profiling

#define ADOW_PROF_PIN_HIGH()        do {} while (0)
#define ADOW_PROF_PIN_LOW()         do {} while (0)
#define ADOW_PROF_ADC_START()       do {} while (0)
#define ADOW_PROF_ADC_STOP()        do {} while (0)
#define ADOW_PROF_ISOSPI_START()    do {} while (0)
#define ADOW_PROF_ISOSPI_STOP()     do {} while (0)
#define ADOW_PROF_MCU_START()       do {} while (0)
#define ADOW_PROF_MCU_STOP()        do {} while (0)

#endif
