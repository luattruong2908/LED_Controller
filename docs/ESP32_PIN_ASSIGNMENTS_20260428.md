# ESP32 Pin Assignments

> Reference: [ESP32 NodeMCU LuaNode32 38-pin](https://www.thegioiic.com/esp32-nodemcu-luanode32-module-thu-phat-wifi-38-chan)

---

## Left Connector

| No. | Name  | Type | Function                                        | Assignment      | Description                                          |
|-----|-------|------|-------------------------------------------------|-----------------|------------------------------------------------------|
| 1   | 3V3   | P    | 3.3 V power supply                              | P3V3_ESP        |                                                      |
| 2   | EN    | I    | CHIP_PU, Reset                                  |                 |                                                      |
| 3   | VP    | I    | GPIO36, ADC1_CH0, S_VP                          |                 |                                                      |
| 4   | VN    | I    | GPIO39, ADC1_CH3, S_VN                          |                 |                                                      |
| 5   | IO34  | I    | GPIO34, ADC1_CH6, VDET_1                        | ADDR_BIT_0      | ESP read the address status to know the Block A B C D |
| 6   | IO35  | I    | GPIO35, ADC1_CH7, VDET_2                        | ADDR_BIT_1      | ESP read the address status to know the Block A B C D |
| 7   | IO32  | I/O  | GPIO32, ADC1_CH4, TOUCH_CH9, XTAL_32K_P        | ADDR_BIT_2      | ESP read the address status to know the Block A B C D |
| 8   | IO33  | I/O  | GPIO33, ADC1_CH5, TOUCH_CH8, XTAL_32K_N        | ADDR_BIT_3      | ESP read the address status to know the Block A B C D |
| 9   | IO25  | I/O  | GPIO25, ADC2_CH8, DAC_1                         | ADDR_BIT_4      | ESP read the address status to know the Block A B C D |
| 10  | IO26  | I/O  | GPIO26, ADC2_CH9, DAC_2                         | FACTORY_JUMP_L  | Factory test, the output will blink and sequence ON/OFF |
| 11  | IO27  | I/O  | GPIO27, ADC2_CH7, TOUCH_CH7                     | ESP_HEARTBEAT   | Let user know the ESP die or still fine              |
| 12  | IO14  | I/O  | GPIO14, ADC2_CH6, TOUCH_CH6, MTMS              | HSPI_CLK        | Clock for bit 128 - bit 255                          |
| 13  | IO12  | I/O  | GPIO12, ADC2_CH5, TOUCH_CH5, MTDI              |                 |                                                      |
| 14  | GND   | G    | Ground                                          | GND             |                                                      |
| 15  | IO13  | I/O  | GPIO13, ADC2_CH4, TOUCH_CH4, MTCK              | HSPI_MOSI       | Data for bit 128 - bit 255                           |
| 16  | D2    | I/O  | GPIO9, D2                                       |                 |                                                      |
| 17  | D3    | I/O  | GPIO10, D3                                      |                 |                                                      |
| 18  | CMD   | I/O  | GPIO11, CMD                                     |                 |                                                      |
| 19  | 5V    | P    | 5 V power supply                                | P5V_ESP         |                                                      |

---

## Right Connector

| No. | Name  | Type | Function                                        | Assignment      | Description                                          |
|-----|-------|------|-------------------------------------------------|-----------------|------------------------------------------------------|
| 1   | GND   | G    | Ground                                          | GND             |                                                      |
| 2   | IO23  | I/O  | GPIO23                                          | VSPI_MOSI       | Data for bit 0 to bit 127                            |
| 3   | IO22  | I/O  | GPIO22                                          |                 |                                                      |
| 4   | TX    | I/O  | GPIO1, U0TXD                                    |                 |                                                      |
| 5   | RX    | I/O  | GPIO3, U0RXD                                    |                 |                                                      |
| 6   | IO21  | I/O  | GPIO21                                          |                 |                                                      |
| 7   | GND   | G    | Ground                                          | GND             |                                                      |
| 8   | IO19  | I/O  | GPIO19                                          |                 |                                                      |
| 9   | IO18  | I/O  | GPIO18                                          | VSPI_CLK        | Clock for bit 0 to bit 127                           |
| 10  | IO5   | I/O  | GPIO5                                           | VSPI_LATCH      | Latch for bit 0 to bit 127                           |
| 11  | IO17  | I/O  | GPIO17                                          |                 |                                                      |
| 12  | IO16  | I/O  | GPIO16                                          |                 |                                                      |
| 13  | IO4   | I/O  | GPIO4, ADC2_CH0, TOUCH_CH0                      |                 |                                                      |
| 14  | IO0   | I/O  | GPIO0, ADC2_CH1, TOUCH_CH1, Boot               |                 |                                                      |
| 15  | IO2   | I/O  | GPIO2, ADC2_CH2, TOUCH_CH2                      |                 |                                                      |
| 16  | IO15  | I/O  | GPIO15, ADC2_CH3, TOUCH_CH3, MTDO              | HSPI_LATCH      | Latch for bit 128 - bit 255                          |
| 17  | D1    | I/O  | GPIO8, D1                                       |                 |                                                      |
| 18  | D0    | I/O  | GPIO7, D0                                       |                 |                                                      |
| 19  | CLK   | I/O  | GPIO6, CLK                                      |                 |                                                      |

---

## Legend

| Type | Meaning        |
|------|----------------|
| P    | Power          |
| I    | Input only     |
| I/O  | Input / Output |
| G    | Ground         |

---

## Diorama Assignments Summary

| Assignment     | Pin  | GPIO   | Description                                          |
|----------------|------|--------|------------------------------------------------------|
| ADDR_BIT_0     | IO34 | GPIO34 | ESP read the address status to know the Block A B C D |
| ADDR_BIT_1     | IO35 | GPIO35 | ESP read the address status to know the Block A B C D |
| ADDR_BIT_2     | IO32 | GPIO32 | ESP read the address status to know the Block A B C D |
| ADDR_BIT_3     | IO33 | GPIO33 | ESP read the address status to know the Block A B C D |
| ADDR_BIT_4     | IO25 | GPIO25 | ESP read the address status to know the Block A B C D |
| FACTORY_JUMP_L | IO26 | GPIO26 | Factory test, the output will blink and sequence ON/OFF |
| ESP_HEARTBEAT  | IO27 | GPIO27 | Let user know the ESP die or still fine              |
| HSPI_CLK       | IO14 | GPIO14 | Clock for bit 128 - bit 255                          |
| HSPI_MOSI      | IO13 | GPIO13 | Data for bit 128 - bit 255                           |
| HSPI_LATCH     | IO15 | GPIO15 | Latch for bit 128 - bit 255                          |
| VSPI_MOSI      | IO23 | GPIO23 | Data for bit 0 to bit 127                            |
| VSPI_CLK       | IO18 | GPIO18 | Clock for bit 0 to bit 127                           |
| VSPI_LATCH     | IO5  | GPIO5  | Latch for bit 0 to bit 127                           |
