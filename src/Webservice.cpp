#include "Webservice.h"

const char* loginPage = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Đăng nhập</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
        .container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 100%; max-width: 400px; text-align: center; box-sizing: border-box; }
        h2 { color: #1c1e21; margin-bottom: 25px; }
        input[type="text"], input[type="password"] { width: 100%; padding: 12px; border: 1px solid #dddfe2; border-radius: 6px; font-size: 16px; margin-bottom: 15px; box-sizing: border-box; }
        .btn { border: none; border-radius: 6px; padding: 12px; font-size: 16px; font-weight: bold; cursor: pointer; text-align: center; width: 100%; transition: background-color 0.2s; }
        .btn-primary { background-color: #1877f2; color: #fff; }
        .btn-primary:hover { background-color: #166fe5; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Đăng nhập thiết bị</h2>
        <form action="/login" method="POST">
            <input type="text" id="username" name="username" placeholder="Tên đăng nhập" required>
            <input type="password" id="password" name="password" placeholder="Mật khẩu" required>
            <button type="submit" class="btn btn-primary">Đăng nhập</button>
        </form>
    </div>
</body>
</html>
)rawliteral";

const char* result = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Thông báo</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; text-align: center; }
        .container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }
        h2 { color: #4CAF50; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Lưu thành công!</h2>
        <p>Thiết bị sẽ khởi động lại trong giây lát...</p>
    </div>
</body>
</html>
)rawliteral";

const char* configPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cấu hình ESP32</title>
</head>
<body>
  <h2>Cấu hình Wi-Fi</h2>
  <form action="/save" method="POST">
    <label for="ssid">Tên Wi-Fi:</label><br>
    <input type="text" id="ssid" name="ssid" required><br>
    <label for="password">Mật khẩu Wi-Fi:</label><br>
    <input type="password" id="password" name="password" required><br>
    <label for="id">ID:</label><br>
    <input type="text" id="id" name="id" required><br>
    <input type="submit" value="Lưu">
  </form>
  <a href="/logout">Đăng xuất</a>
</body>
</html>
)rawliteral";

const char* checkInternet = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Trạng thái kết nối</title>
</head>
<body>
  <h2>✅ Đã có kết nối Internet</h2>
</body>
</html>)rawliteral";

const char* checkInternetNoConnect = R"rawliteral(
  <!DOCTYPE html>
  <html lang="vi">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Trạng thái kết nối</title>
  </head>
  <body>
    <h2>❌ Chưa có kết nối Internet</h2>
  </body>
  </html>)rawliteral";

const char* configPage1 = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cấu hình Wi-Fi</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; }
        .container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 100%; max-width: 450px; box-sizing: border-box; }
        h2 { color: #1c1e21; text-align: center; margin-bottom: 25px; }
        label { font-weight: 600; color: #4b4f56; display: block; margin-bottom: 8px; }
        input[type="text"], input[type="password"], select { width: 100%; padding: 12px; border: 1px solid #dddfe2; border-radius: 6px; font-size: 16px; margin-bottom: 15px; box-sizing: border-box; }
        .btn-group { display: flex; gap: 10px; margin-top: 10px; }
        .btn { border: none; border-radius: 6px; padding: 12px; font-size: 16px; font-weight: bold; cursor: pointer; text-align: center; width: 100%; transition: background-color 0.2s; }
        .btn-primary { background-color: #1877f2; color: #fff; }
        .btn-primary:hover { background-color: #166fe5; }
        .btn-secondary { background-color: #e4e6eb; color: #4b4f56; }
        .btn-secondary:hover { background-color: #d8dbdf; }
        .btn-danger { background-color: #fa383e; color: #fff; }
        .btn-danger:hover { background-color: #e33136; }
        .btn-info { background-color: #17a2b8; color: #fff; }
        .btn-info:hover { background-color: #148a9c; }
        #toast { position: fixed; top: 20px; right: 20px; background-color: #333; color: #fff; padding: 15px; border-radius: 5px; z-index: 1000; visibility: hidden; opacity: 0; transition: visibility 0s, opacity 0.5s linear; }
        #toast.show { visibility: visible; opacity: 1; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Cấu hình Wi-Fi</h2>
        <form id="wifi-form" action="/save" method="POST">
            <label for="ssid">Chọn Wi-Fi:</label>
            <div class="btn-group">
                <select id="ssid" name="ssid" required>
                    <option>Đang tải...</option>
                </select>
                <button type="button" class="btn btn-secondary" onclick="loadWiFiList()">Quét</button>
            </div>
            
            <label for="password">Mật khẩu Wi-Fi:</label>
            <input type="password" id="password" name="password">
            
            <label for="id">ID Thiết Bị:</label>
            <input type="text" id="id" name="id" required placeholder="VD: QA-T01-V01">
            
            <button type="submit" class="btn btn-primary">Lưu</button>
        </form>
        <div class="btn-group">
            <button onclick="viewConfig()" class="btn btn-info">Xem Cấu hình</button>
            <button onclick="resetConfig()" class="btn btn-danger">Reset Cấu hình</button>
        </div>
    </div>
    <div id="toast"></div>

    <script>
        function showToast(message, duration = 3000) {
            const toast = document.getElementById('toast');
            toast.textContent = message;
            toast.className = "show";
            setTimeout(() => { toast.className = toast.className.replace("show", ""); }, duration);
        }

        function loadWiFiList() {
            const select = document.getElementById("ssid");
            select.innerHTML = "<option>Đang quét...</option>";
            showToast("Đang quét mạng Wi-Fi...");

            fetch("/wifi_scan")
                .then(response => response.json())
                .then(data => {
                    select.innerHTML = "";
                    if (data.length === 0) {
                        select.innerHTML = "<option>Không tìm thấy Wi-Fi</option>";
                        showToast("Không tìm thấy mạng Wi-Fi nào.", 4000);
                        return;
                    }
                    data.forEach(ssid => {
                        let option = document.createElement("option");
                        option.value = ssid;
                        option.textContent = ssid;
                        select.appendChild(option);
                    });
                    showToast("Quét thành công!", 2000);
                })
                .catch(err => {
                    console.error("Lỗi tải danh sách WiFi:", err);
                    select.innerHTML = "<option>Lỗi khi quét</option>";
                    showToast("Lỗi khi quét Wi-Fi.", 4000);
                });
        }
        
        document.getElementById('wifi-form').addEventListener('submit', function(e) {
            const saveButton = this.querySelector('button[type="submit"]');
            saveButton.disabled = true;
            saveButton.textContent = 'Đang lưu...';
            showToast("Đang lưu cấu hình, thiết bị sẽ khởi động lại...", 5000);
        });

        function viewConfig() {
            showToast("Chức năng đang được phát triển.");
        }

        function resetConfig() {
            if (confirm('Bạn có chắc chắn muốn reset cấu hình Wi-Fi không? Thao tác này sẽ xóa toàn bộ cài đặt hiện tại.')) {
                showToast("Đang thực hiện reset...", 4000);
                fetch("/reset_config")
                    .then(res => {
                        showToast("Reset thành công! Tải lại trang...", 4000);
                        setTimeout(() => window.location.reload(), 4000);
                    })
                    .catch(err => {
                        showToast("Có lỗi xảy ra khi reset.", 4000);
                    });
            }
        }

        window.onload = loadWiFiList;
    </script>
</body>
</html>
)rawliteral";

const char* logout = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Thông báo</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; text-align: center; }
        .container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }
        h2 { color: #1877f2; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Đã đăng xuất!</h2>
        <p>Bạn đã đăng xuất thành công.</p>
    </div>
</body>
</html>
)rawliteral";

const char* loginFail =  R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Thông báo</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; text-align: center; }
        .container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }
        h2 { color: #fa383e; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Đăng nhập thất bại!</h2>
        <p>Tên đăng nhập hoặc mật khẩu không đúng.</p>
        <a href="/login">Thử lại</a>
    </div>
</body>
</html>
)rawliteral";

const char* serverFail = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Thông báo</title>
    <style>
        body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif; background-color: #f0f2f5; margin: 0; padding: 20px; display: flex; justify-content: center; align-items: center; min-height: 100vh; text-align: center; }
        .container { background: #fff; padding: 30px; border-radius: 12px; box-shadow: 0 8px 24px rgba(0,0,0,0.1); width: 100%; max-width: 400px; }
        h2 { color: #ffc107; }
    </style>
</head>
<body>
    <div class="container">
        <h2>Lỗi!</h2>
        <p>Vui lòng điền đầy đủ thông tin và thử lại.</p>
        <a href="/config">Quay lại</a>
    </div>
</body>
</html>
)rawliteral";

void printHello()
{
  Serial.println("Hello from Webservice.cpp!");
}

void checkConnect(
    bool eth_connected,
    bool wifi_connected,
    const char *&result,
    const char *&color
)
{
  if (eth_connected || wifi_connected)
  {
    result = "Da co ket noi Internet";
    color = "green";
  }
  else
  {
    result = "Chua co ket noi Internet";
    color = "red";
  }
}
