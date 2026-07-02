# Plan: Chuyển firmware sang điều khiển chung cư "Monrei"

## Context

Dự án mới tái dùng đúng phần cứng mạch 40-bit hiện tại (ESP32 + VSPI shift register, 40 chân FET — xem [docs/ESP32_PIN_ASSIGNMENTS_20260428.md](docs/ESP32_PIN_ASSIGNMENTS_20260428.md)), nhưng thay vì "1 bit = 1 đèn của 1 căn nhà diorama", giờ mỗi chân FET gánh **nhiều đèn mắc song song** để điều khiển từng tầng/loại phòng của một tòa chung cư. Để tránh quá tải FET, mỗi nhóm đèn được chia đều ra nhiều chân FET. 40 chân FET được phân bổ vừa khít thành 9 nhóm logic, mỗi nhóm là một topic MQTT bật/tắt độc lập.

Mục tiêu: sửa [src/main.cpp](src/main.cpp) để:
- Đăng ký 9 topic mới dạng `imodel/monrei/...`, mỗi topic bật/tắt một nhóm chân FET.
- Giữ nguyên toàn bộ luồng vận hành hiện có (WiFiManager provisioning, MQTT reconnect/timeout, heartbeat, watchdog, factory test, xuất shift register 40-bit).
- Bỏ đọc DIP switch, gán cứng địa chỉ mạch = 3.

## Quyết định thiết kế (đã chốt với user)
- **Payload mỗi topic = 0/1** (khác 0 ⇒ bật cả nhóm; 0 ⇒ tắt cả nhóm). Toàn bộ FET trong nhóm sáng/tắt cùng lúc.
- **Topic tầng:** `floor_20`, `floor_30`, `floor_roof`, `floor_commercial`.
- **Topic phòng:** `room_a`, `room_b`, `room_c`, `room_d`.
- **DIP:** bỏ đọc switch, hardcode `boardAddr = 3` (vẫn dùng cho MQTT client ID & tên AP WiFi).
- Prefix topic: `imodel/monrei/`.

## Bản đồ nhóm → bit (40 bit, lấp đầy 0–39)

| Topic | Số FET | Bit | Mask |
|-------|--------|-----|------|
| `imodel/monrei/landscape`        | 4 | 0–3   | `0x0000000F` |
| `imodel/monrei/floor_20`         | 2 | 4–5   | `0x00000030` |
| `imodel/monrei/floor_30`         | 2 | 6–7   | `0x000000C0` |
| `imodel/monrei/floor_roof`       | 2 | 8–9   | `0x00000300` |
| `imodel/monrei/floor_commercial` | 2 | 10–11 | `0x00000C00` |
| `imodel/monrei/room_a`           | 7 | 12–18 | `0x0007F000` |
| `imodel/monrei/room_b`           | 7 | 19–25 | `0x03F80000` |
| `imodel/monrei/room_c`           | 7 | 26–32 | `0x1FC000000` |
| `imodel/monrei/room_d`           | 7 | 33–39 | `0xFE00000000` |

> Ghi chú phần cứng: thứ tự bit → chân FET vật lý tùy dây nối shift register. Mặc định coi bit 0 = FET#1. Nếu wiring khác, chỉ cần đổi thứ tự/mask trong bảng `GROUPS` mà không đụng logic.

## Thay đổi trong [src/main.cpp](src/main.cpp)

1. **Banner & hằng số** ([src/main.cpp:28-36](src/main.cpp#L28), [:229](src/main.cpp#L229))
   - Đổi dòng banner setup sang "Monrei Apartment Controller".
   - Giữ `BITS_PER_HOUSE=40`, `BYTES_PER_HOUSE=5`, `ALL_BITS_ON` (factory test & shift output vẫn dùng).

2. **Bảng nhóm topic** (thêm vùng Config)
   - Thêm `struct LedGroup { const char* topic; uint64_t mask; };` và mảng `GROUPS[]` 9 phần tử theo bảng trên (dùng macro tạo mask từ dải bit cho dễ đọc), kèm `NUM_GROUPS`.
   - Bỏ biến `char myTopic[40]`.

3. **Bỏ DIP** ([src/main.cpp:21-25](src/main.cpp#L21), [:167-174](src/main.cpp#L167), [:237-241](src/main.cpp#L237), [:256-265](src/main.cpp#L256))
   - Xóa các `#define DIP_BITx`, hàm `readBoardAddress()`, các `pinMode(DIP_*, ...)`.
   - Trong `setup()`: bỏ đọc DIP + khối validate 1–5; gán `boardAddr = 3` (định nghĩa hằng `BOARD_ADDR 3`).
   - Giữ `boardAddr` cho `mqtt_reconnect()` client ID và `setup_wifi()` tên AP.

4. **`mqtt_callback()`** ([src/main.cpp:75-96](src/main.cpp#L75))
   - Parse payload thành số; coi `!= 0` là ON.
   - Duyệt `GROUPS[]`, khớp `topic`: ON ⇒ `houseState |= mask`, OFF ⇒ `houseState &= ~mask`.
   - Gọi `updateShiftRegisters()` + `printBuffer()` (giữ nguyên 2 hàm này), log `[<topic>] = ON/OFF`.

5. **`mqtt_reconnect()`** ([src/main.cpp:98-115](src/main.cpp#L98))
   - Sau khi `client.connect(...)` thành công: `for` qua `GROUPS[]` gọi `client.subscribe(GROUPS[i].topic)`. Nhờ app publish `retain`, board nhận lại trạng thái mới nhất từng nhóm khi reconnect.

6. **Giữ nguyên:** `updateShiftRegisters()`, `printBuffer()`, `setup_wifi()`, `wifi_check()`, `heartbeat_update()`, `factory_test()`/`factory_wait()`, watchdog, vòng `loop()`.

## Ngoài phạm vi (cần làm riêng nếu muốn chạy thực tế)
- [app/www/index.html](app/www/index.html) hiện publish 1 số 40-bit tới `diorama/imodel/house_N`. App **chưa khớp** topic/payload mới. Nếu cần điều khiển từ app, phải cập nhật app sang 9 topic `imodel/monrei/...` với payload 0/1 (việc này user chưa yêu cầu — sẽ làm khi được xác nhận).

## Kiểm thử
1. **Biên dịch:** `pio run -e esp32dev` — không lỗi.
2. **Nạp + serial:** `pio run -e esp32dev -t upload` rồi `pio device monitor`; kiểm tra log in 9 topic subscribe và `boardAddr = 3`.
3. **Test publish thủ công** (mosquitto/MQTTX qua broker TLS):
   - `imodel/monrei/landscape` = `1` ⇒ 4 FET bit 0–3 bật; `0` ⇒ tắt.
   - `imodel/monrei/room_d` = `1` ⇒ 7 FET bit 33–39 bật.
   - Quan sát dòng `>> DATA [H3]:` của `printBuffer()` phản ánh đúng mask.
4. **Factory test:** giữ GPIO26 LOW khi boot ⇒ vẫn chạy blink-all + quét tuần tự 40 bit.
