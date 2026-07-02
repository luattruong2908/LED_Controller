# Plan: Sửa app web (index.html) cho firmware Monrei

## Context

Firmware đã chuyển sang điều khiển chung cư Monrei: 9 nhóm đèn, mỗi nhóm là 1 topic MQTT **bật/tắt** (payload `1`/`0`, retain, QoS 1) dạng `imodel/monrei/...`. App cũ [app/www/index.html](app/www/index.html) theo mô hình carousel "nhà" + lưới 40 phòng, publish số 40-bit tới `diorama/imodel/house_N` nên không còn điều khiển được board.

Mục tiêu: viết lại logic + markup thành **1 trang danh sách phẳng** gồm 1 nút tổng + 9 nút bật/tắt, publish đúng topic/payload mới; giữ nguyên theme, header, popover cài đặt, toast, màn hình loading và luồng MQTT.

## Yêu cầu (đã chốt)
- Bố cục danh sách phẳng: master trên cùng, rồi 9 hàng toggle.
- Nút: Toàn bộ dự án (master) · Landscape · Tầng 20/30/mái/thương mại · Phòng A/B/C/D.
- Mỗi nút → 1 topic, payload `1`/`0`, `retain:true`, `qos:1`.

## Bản đồ nhóm (khớp [src/main.cpp:45-55](src/main.cpp#L45))
`landscape`, `floor_20`, `floor_30`, `floor_roof`, `floor_commercial`, `room_a`, `room_b`, `room_c`, `room_d` — prefix `imodel/monrei/`.

## Đã thực hiện trong [app/www/index.html](app/www/index.html)
1. `<title>`/brand → "Monrei".
2. Config: `PREFIX='imodel/monrei/'`; mảng `GROUPS[{key,name}]`; `state` keyed theo key → boolean.
3. `render()` đổ 9 `.ctrl-row` vào `#controls` (nhãn + `.toggle`, onclick `groupToggle(key)`).
4. `groupToggle` / `refreshGroup` / `refreshMaster` / `masterToggle` (master = mọi group bật).
5. `publish(key)` gửi `'1'/'0'` retain; subscribe toàn bộ; `message` parse suffix → khác 0 = bật.
6. Markup: master label "Bật/tắt toàn bộ dự án"; thay carousel/dots bằng `#controls`.
7. CSS: bỏ carousel/slide/dots/rooms/house, thêm `.controls-scroll`/`.ctrl-row`/`.ctrl-icon`/`.ctrl-name`.

## Kiểm thử
1. Mở app trên trình duyệt → "Bắt đầu" → popover báo "Đã kết nối".
2. Toggle "Tầng 20" ⇒ publish `imodel/monrei/floor_20`=`1`; tắt ⇒ `0`. Master ⇒ cả 9 topic.
3. Board: serial `>> [imodel/monrei/floor_20] = ON`, `>> DATA [H3]:` đúng bit.
4. 2 thiết bị đồng bộ qua subscribe + retain.
5. (Tùy chọn) `cd app && npx cap sync android`.
