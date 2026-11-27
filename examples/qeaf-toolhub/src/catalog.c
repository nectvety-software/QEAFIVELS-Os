#include "catalog.h"

static const uint8_t tool_categories[TOOLHUB_TOOL_COUNT] = {
    0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7,
    8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15,
    16, 16
};

static const uint8_t tool_statuses[TOOLHUB_TOOL_COUNT] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
    2, 2, 0, 0, 0, 0, 2, 1, 2, 2, 0, 0, 0, 0, 0, 0,
    2, 1
};

const char *toolhub_category_name(uint8_t index)
{
    switch(index) {
    case 0: return "Ngôn ngữ & Runtime";
    case 1: return "Python";
    case 2: return "Node.js";
    case 3: return "Phát triển Web";
    case 4: return "Cơ sở dữ liệu";
    case 5: return "Máy chủ";
    case 6: return "Công cụ mạng";
    case 7: return "Công cụ biên dịch";
    case 8: return "Trình soạn thảo";
    case 9: return "Tiện ích hệ thống";
    case 10: return "Công cụ media";
    case 11: return "Thư viện";
    case 12: return "Hệ thống QEAF";
    case 13: return "Bảo mật & Mật mã";
    case 14: return "Công cụ tiện ích";
    case 15: return "Gói tùy chỉnh";
    case 16: return "Thông tin nhà phát triển";
    default: return "";
    }
}

const char *toolhub_category_description(uint8_t index)
{
    switch(index) {
    case 0: return "Môi trường và ngôn ngữ lập trình";
    case 1: return "Python và hệ sinh thái gói";
    case 2: return "JavaScript phía máy chủ";
    case 3: return "Công cụ tạo ứng dụng web";
    case 4: return "Dữ liệu và hệ quản trị";
    case 5: return "Dịch vụ và cổng lắng nghe";
    case 6: return "Kết nối và chẩn đoán mạng";
    case 7: return "Trình dịch, liên kết và build";
    case 8: return "Soạn thảo mã và văn bản";
    case 9: return "Thông tin và quyền cục bộ";
    case 10: return "Âm thanh, hình ảnh và video";
    case 11: return "Thành phần dùng chung";
    case 12: return "Nền tảng, ABI và sandbox";
    case 13: return "Kiểm tra và bảo vệ có phạm vi";
    case 14: return "Tiện ích nhỏ hằng ngày";
    case 15: return "Gói do người dùng xác định";
    case 16: return "Phiên bản, giới hạn và bằng chứng";
    default: return "";
    }
}

uint8_t toolhub_category_first_tool(uint8_t index)
{
    return index < TOOLHUB_CATEGORY_COUNT ? (uint8_t)(index * 2u) : 0u;
}

uint8_t toolhub_category_tool_count(uint8_t index)
{
    return index < TOOLHUB_CATEGORY_COUNT ? 2u : 0u;
}

const char *toolhub_tool_name(uint8_t index)
{
    switch(index) {
    case 0: return "Môi trường Python";
    case 1: return "Trình quản lý gói Python";
    case 2: return "Node.js Runtime";
    case 3: return "npm và gói JavaScript";
    case 4: return "Máy chủ web cục bộ";
    case 5: return "Không gian phát triển Web";
    case 6: return "SQLite cục bộ";
    case 7: return "MySQL / MariaDB";
    case 8: return "HTTP Server";
    case 9: return "Dịch vụ nền";
    case 10: return "Kiểm tra kết nối mạng";
    case 11: return "Chẩn đoán socket";
    case 12: return "Trình biên dịch C";
    case 13: return "Công cụ build";
    case 14: return "QEAF Code Editor";
    case 15: return "Console có giới hạn";
    case 16: return "Kiểm tra quyền";
    case 17: return "Phiên bản ABI";
    case 18: return "Chuyển đổi media";
    case 19: return "Âm thanh và video";
    case 20: return "Thư viện đồ họa";
    case 21: return "Thư viện mạng";
    case 22: return "Thông tin nền tảng";
    case 23: return "Chính sách sandbox";
    case 24: return "Trợ giúp lệnh";
    case 25: return "Quyền ứng dụng";
    case 26: return "Mật mã và chứng chỉ";
    case 27: return "Công cụ bảo mật";
    case 28: return "Tính toán nhanh";
    case 29: return "Chuyển đổi đơn vị";
    case 30: return "Gói tùy chỉnh";
    case 31: return "Nguồn gói QEAF";
    case 32: return "Về QEAF Tool Hub";
    case 33: return "Giới hạn ABI v5";
    default: return "";
    }
}

const char *toolhub_tool_description(uint8_t index)
{
    switch(index) {
    case 0: return "Thông tin môi trường Python minh họa";
    case 1: return "Cài đặt và quản lý thư viện Python";
    case 2: return "Thông tin runtime JavaScript";
    case 3: return "Cài đặt dependency từ npm";
    case 4: return "Phục vụ tệp trong một thư mục";
    case 5: return "Mẫu dự án web và lệnh chạy";
    case 6: return "Cơ sở dữ liệu SQL nhúng";
    case 7: return "Hệ quản trị cơ sở dữ liệu";
    case 8: return "Lắng nghe và phản hồi HTTP";
    case 9: return "Chạy dịch vụ sau khi khởi động";
    case 10: return "Khảo sát đường đi và kết nối";
    case 11: return "Quan sát cổng và trạng thái";
    case 12: return "Biên dịch mã nguồn C";
    case 13: return "Tự động hóa các bước build";
    case 14: return "Ứng dụng soạn thảo riêng của QEAF";
    case 15: return "Lệnh do ứng dụng xử lý, không phải shell";
    case 16: return "Truy vấn quyết định quyền thật";
    case 17: return "Đọc phiên bản API từ runtime";
    case 18: return "Xử lý âm thanh, ảnh và video";
    case 19: return "Phát và ghi nội dung media";
    case 20: return "Vẽ và xử lý hình ảnh";
    case 21: return "Kết nối có kiểm soát";
    case 22: return "Giới thiệu nền tảng và phạm vi app";
    case 23: return "Ranh giới quyền và tài nguyên";
    case 24: return "Danh sách lệnh console hợp lệ";
    case 25: return "Hiển thị quyền hiệu lực";
    case 26: return "Tạo khóa, chứng chỉ và checksum";
    case 27: return "Kiểm tra an toàn có phạm vi";
    case 28: return "Phép tính nhanh trong phiên";
    case 29: return "Đổi đơn vị thông dụng";
    case 30: return "Cài gói do người dùng cung cấp";
    case 31: return "Kho gói có kiểm soát";
    case 32: return "Danh tính và nguyên tắc ứng dụng";
    case 33: return "Những gì ABI v5 cung cấp";
    default: return "";
    }
}

const char *toolhub_tool_detail(uint8_t index)
{
    switch(index) {
    case 0:
    case 1:
        return "ABI native không có API chạy tiến trình hoặc quản lý gói. Không có Python thật trong phiên này; ví dụ chỉ giải thích lệnh.";
    case 2:
    case 3:
        return "Không có JavaScript engine, npm hay dịch vụ gói được cấp cho ứng dụng. Không tải hoặc chạy mã từ bên ngoài.";
    case 4:
    case 5:
        return "Không có quyền mạng, ghi tệp hay chạy tiến trình. Ứng dụng không mở cổng và không giả lập tiến trình tải xuống.";
    case 6:
    case 7:
        return "Runtime chưa cung cấp API lưu trữ cho QEAPP native. Ví dụ SQL không được thực thi và không tạo tệp cơ sở dữ liệu.";
    case 8:
    case 9:
        return "Sandbox chặn socket và tạo tiến trình. Không có máy chủ hay dịch vụ nền thật nào được khởi động.";
    case 10:
    case 11:
        return "Không có quyền mạng. Ứng dụng không gửi gói, không quét thiết bị và không hiển thị trạng thái mạng giả.";
    case 12:
    case 13:
        return "ABI không cho phép thực thi compiler, make hay tiến trình con. Ví dụ là tài liệu, không phải hoạt động build.";
    case 14:
        return "Đây là ứng dụng hệ thống riêng, không phải dependency của Tool Hub. Mở nó từ màn hình Ứng dụng của QEAF.";
    case 15:
        return "Console của Tool Hub chỉ nhận các lệnh cục bộ được liệt kê. Không có shell, biến môi trường, pipe hay ký tự điều khiển.";
    case 16:
    case 25:
        return "Hành động cục bộ thật: gọi request_permission và hiển thị kết quả quyết định của broker cho ui.display và input.receive.";
    case 17:
        return "Hành động cục bộ thật: đọc abi_version trong bảng API do runtime truyền vào create().";
    case 18:
    case 19:
        return "ABI v5 chưa có API audio, camera hay codec media. Không có quyền truy cập thiết bị media được yêu cầu.";
    case 20:
        return "Tool Hub tự vẽ pixel bằng RGB565. Đây không phải một thư viện đồ họa hệ thống và không truy cập GPU.";
    case 21:
        return "Không có API mạng cho QEAPP native. Không mở kết nối, không tải thư viện và không thực thi mã tải về.";
    case 22:
        return "Hành động cục bộ thật: mở màn hình Giới thiệu với danh tính QEAF, ABI, quyền và giới hạn của ứng dụng.";
    case 23:
        return "Ứng dụng chỉ yêu cầu ui.display và input.receive. Runtime áp dụng no_new_privs, UID không đặc quyền, đóng descriptor và seccomp.";
    case 24:
        return "Hành động cục bộ thật: mở console và chạy lệnh help do chính ứng dụng xử lý.";
    case 26:
        return "Không có dịch vụ mật mã được cấp. Không tạo khóa, chứng chỉ hay checksum giả; ví dụ OpenSSL không được chạy.";
    case 27:
        return "Chỉ hỗ trợ thông tin và kiểm tra quyền cục bộ. Không quét mạng, khai thác lỗ hổng hoặc tấn công thiết bị.";
    case 28:
    case 29:
        return "Chưa có dịch vụ tính toán được cung cấp cho ứng dụng. Nút hành động bị vô hiệu hóa thay vì trả kết quả giả.";
    case 30:
    case 31:
        return "Không có API tải gói, xác minh installer hay thực thi mã bên thứ ba. Tool Hub không chứa updater hay shell installer.";
    case 32:
        return "Hành động cục bộ thật: mở màn hình Giới thiệu. Ứng dụng không sao chép tên, logo hoặc tài sản của bên thứ ba.";
    case 33:
        return "ABI v5 bổ sung layout_split_weighted. App dùng blit_pixels, request_permission và on_input; không dùng draw_text thiếu tiếng Việt.";
    default:
        return "Không có thông tin cho mục này.";
    }
}

const char *toolhub_tool_example(uint8_t index)
{
    switch(index) {
    case 0: return "python3 --version";
    case 1: return "pip --version";
    case 2: return "node --version";
    case 3: return "npm --version";
    case 4: return "python3 -m http.server 8080";
    case 5: return "npm run dev";
    case 6: return "sqlite3 app.db";
    case 7: return "mysql --version";
    case 8: return "node server.js";
    case 9: return "service example start";
    case 10: return "ping 127.0.0.1";
    case 11: return "ss -lnt";
    case 12: return "cc --version";
    case 13: return "make --version";
    case 14: return "Mở QEAF Code Editor";
    case 15: return "help";
    case 16:
    case 25: return "perms";
    case 17:
    case 22:
    case 32:
    case 33: return "about";
    case 18: return "ffmpeg -version";
    case 19: return "ffplay example.mp4";
    case 20: return "Không có lệnh shell";
    case 21: return "Không có API mạng";
    case 23: return "perms";
    case 24: return "help";
    case 26: return "openssl version";
    case 27: return "Không quét mạng";
    case 28: return "Chưa có dịch vụ tính toán";
    case 29: return "Chưa có dịch vụ đổi đơn vị";
    case 30: return "Không cài gói bên thứ ba";
    case 31: return "Không có nguồn gói";
    default: return "";
    }
}

uint8_t toolhub_tool_category(uint8_t index)
{
    return index < TOOLHUB_TOOL_COUNT ? tool_categories[index] : 0u;
}

uint8_t toolhub_tool_status(uint8_t index)
{
    return index < TOOLHUB_TOOL_COUNT ? tool_statuses[index] : TOOLHUB_UNAVAILABLE;
}
