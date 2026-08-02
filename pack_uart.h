/* Scan 01 — pack status over the PC UART (task T7, spec §7)
 *
 * Copyright 2026 Vadim Petrov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Pure text builder — host-tested in tests/test_pack_uart.c; wired into the
 * k5prog UART dispatch in app/uart.c (command PACK_UART_STATUS_CMD).
 */

#ifndef PACK_UART_H
#define PACK_UART_H

#include <stdint.h>

#define PACK_UART_STATUS_CMD   0x05DF
#define PACK_UART_STATUS_REPLY 0x05E0

/* Build the status text block (magic/version/counts/seal/practice + one
 * line per captured entry). Returns the length written (<= max). */
uint16_t PACK_UART_BuildStatus(char *out, uint16_t max);

#endif /* PACK_UART_H */
