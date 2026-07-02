# Plan: App Monrei — bố cục trang trượt (iPad landscape)

## Context
App cũ là 1 danh sách phẳng (1 nút tổng + 9 toggle). Yêu cầu mới: quay lại mô hình **nhiều trang + slide trượt ngang** theo các tầng của tòa Monrei, dùng ảnh mặt bằng đã cung cấp để dễ hiểu nút điều khiển khu nào, tối ưu cho **iPad nằm ngang** (ảnh một bên, nút bên cạnh). Logic MQTT (9 topic `imodel/monrei/...`, payload `1`/`0`, retain, QoS 1) giữ nguyên — chỉ đổi render + điều hướng.

## Bản đồ trang → nhóm (6 trang)
| # | Trang | Ảnh | Nút (topic) |
|---|-------|-----|-------------|
| 1 | Mặt bằng | `matbang.png` | Cảnh quan → `landscape` |
| 2 | Tầng 20 | `tang20.png` | Tầng 20 → `floor_20` |
| 3 | Tầng 30 | `tang30.png` | Tầng 30 → `floor_30` |
| 4 | Tầng mái | `tangmai.png` | Tầng mái → `floor_roof` |
| 5 | Tầng thương mại | — | Tầng thương mại → `floor_commercial` |
| 6 | Loại phòng | — | Phòng A/B/C/D → `room_a/b/c/d` |

Trang không ảnh chỉ hiện nút (panel căn giữa). Nếu sau có `tangthuongmai.png`, set `image` cho trang 5.

## Đã thực hiện trong [app/www/index.html](app/www/index.html)
1. **Dữ liệu**: thay `GROUPS` phẳng bằng mảng `PAGES` (title/image/groups); `ALL_KEYS = PAGES.flatMap(...)` (9 key) dùng cho state/subscribe/master/message.
2. **Header**: thêm `.header-master` cố định ("Toàn bộ dự án" + `.toggle#masterToggle`), không trôi theo slide.
3. **Carousel**: `.carousel > .nav-arrow.prev | .track#track | .nav-arrow.next | .dots#dots`. `render()` đổ 6 `.slide` (`.slide-title` + `.slide-body` flex ngang: `.slide-image` khi có ảnh + `.slide-panel` chứa các `.ctrl-row`). Trang không ảnh dùng `.slide-body.no-image`.
4. **Điều hướng**: `goTo(i)`/`goSlide(d)` dịch `translateX(-idx*100%)`, cập nhật dots + disable nút mũi tên đầu/cuối; vuốt qua `touchstart/touchend` (ngưỡng 50px, ưu tiên trục X).
5. **Nút**: tái dùng `.ctrl-row`+`.toggle`, `onclick=groupToggle(key)`, `id="row-"+key`/`"tg-"+key`. `refreshMaster()`/`masterToggle()` duyệt `ALL_KEYS`.
6. **CSS**: xóa `.controls-scroll`/`.controls-list`/`.master-row`; thêm `.header-master`, `.carousel`/`.track`/`.slide`/`.slide-title`/`.slide-body`(+`.no-image`)/`.slide-image`/`.slide-panel`, `.nav-arrow`, `.dots`/`.dot`. Giữ toast, popover, loading, `.ctrl-row`, `.toggle`.

Giữ nguyên: BROKER/USER/PASS/PREFIX, `connectMQTT`, `publish`, handler `message`, `setConn`, `toast`, popover cài đặt, màn hình loading.

## Kiểm thử
1. Mở app (iPad landscape) → "Bắt đầu" → popover "Đã kết nối".
2. Vuốt/bấm ‹ › qua đủ 6 trang; dots cập nhật; 4 trang đầu có ảnh, 2 trang cuối chỉ nút.
3. Toggle "Tầng 20" ⇒ publish `imodel/monrei/floor_20`=`1`/`0`; trang Loại phòng publish `room_a..d`.
4. Master header ⇒ cả 9 topic `1`/`0`.
5. 2 tab đồng bộ (subscribe + retain).
6. (Tùy chọn) `cd app && npx cap sync android`.
