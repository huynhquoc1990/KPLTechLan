#include <WebServer.h>
#include <Settings.h>
#include <FlashFile.h>
#include <ESPAsyncWebServer.h>
// Trang HTML đăng nhập
const char* loginPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Đăng nhập ESP32</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f4f4f9;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }
    .container {
      background: #ffffff;
      padding: 20px;
      border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
      text-align: center;
      width: 90%;
      max-width: 400px;
    }
    h2 {
      color: #333;
    }
    form input {
      width: 100%;
      padding: 10px;
      margin: 10px 0;
      border: 1px solid #ddd;
      border-radius: 4px;
      font-size: 16px;
    }
    form input[type="submit"] {
      background: #007bff;
      color: #fff;
      border: none;
      cursor: pointer;
    }
    form input[type="submit"]:hover {
      background: #0056b3;
    }
    .error-message {
      color: red;
      font-size: 14px;
      margin-top: 10px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Đăng nhập</h2>
    <form action="/login" method="POST">
      <label for="username">Tên đăng nhập:</label><br>
      <input type="text" id="username" name="username" required><br>
      <label for="password">Mật khẩu:</label><br>
      <input type="password" id="password" name="password" required><br>
      <input type="submit" value="Đăng nhập">
    </form>
    <div class="error-message" id="errorMessage"></div>
  </div>
</body>
</html>
)rawliteral";

// Trang trả lại thông báo
const char* result = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Lưu thành công</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      text-align: center;
      background-color: #f4f4f9;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }
    .message-box {
      background: #ffffff;
      padding: 20px;
      border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
      width: 90%;
      max-width: 400px;
    }
    h2 {
      color: #4CAF50;
    }
    p {
      color: #555;
    }
    a {
      display: inline-block;
      margin-top: 20px;
      padding: 10px 20px;
      background: #007bff;
      color: white;
      text-decoration: none;
      border-radius: 4px;
    }
    a:hover {
      background: #0056b3;
    }
  </style>
</head>
<body>
  <div class="message-box">
    <h2>Lưu thành công!</h2>
    <p>ESP32 sẽ khởi động lại trong giây lát...</p>
    <a href="/">Quay lại trang chính</a>
  </div>
  <script>
    setTimeout(() => {
      window.location.href = '/';
    }, 5000); // Chuyển hướng về trang chủ sau 5 giây
  </script>
</body>
</html>
)rawliteral";

// Trang HTML cấu hình theo webservice phương án củ
const char* configPage = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cấu hình ESP32</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f4f4f9;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }
    .container {
      background: #ffffff;
      padding: 20px;
      border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
      text-align: center;
      width: 90%;
      max-width: 400px;
    }
    h2 {
      color: #333;
    }
    form input {
      width: 100%;
      padding: 10px;
      margin: 10px 0;
      border: 1px solid #ddd;
      border-radius: 4px;
      font-size: 16px;
    }
    form input[type="submit"] {
      background: #007bff;
      color: #fff;
      border: none;
      cursor: pointer;
    }
    form input[type="submit"]:hover {
      background: #0056b3;
    }
    a {
      text-decoration: none;
      color: #007bff;
      margin-top: 10px;
      display: inline-block;
    }
    a:hover {
      color: #0056b3;
    }
  </style>
</head>
<body>
  <div class="container">
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
  </div>
</body>
</html>
)rawliteral";

// kiểm tra kết nối internet chay chưa
const char* checkInternet = R"rawliteral(
        <!DOCTYPE html>
        <html lang="vi">
        <head>
          <meta charset="UTF-8">
          <meta name="viewport" content="width=device-width, initial-scale=1.0">
          <title>Trạng thái kết nối</title>
          <style>
            body { font-family: Arial, sans-serif; text-align: center; padding: 50px; background-color: #f4f4f9; }
            .status-box { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); display: inline-block; }
            h2 { color: green; }
            a { text-decoration: none; color: #007bff; display: block; margin-top: 10px; }
          </style>
        </head>
        <body>
          <div class="status-box">
            <h2>✅ Đã có kết nối Internet</h2>
            <a href="/">Quay lại trang chính</a>
          </div>
        </body>
        </html>)rawliteral";

const char* checkInternetNoConnect = R"rawliteral(
  <!DOCTYPE html>
  <html lang="vi">
  <head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Trạng thái kết nối</title>
    <style>
      body { font-family: Arial, sans-serif; text-align: center; padding: 50px; background-color: #f4f4f9; }
      .status-box { background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1); display: inline-block; }
      h2 { color: red; }
      a { text-decoration: none; color: #007bff; display: block; margin-top: 10px; }
    </style>
  </head>
  <body>
    <div class="status-box">
      <h2>❌ Chưa có kết nối Internet</h2>
      <a href="/">🔄 Quay lại cấu hình Wi-Fi</a>
      <a href="/check">🔁 Kiểm tra lại</a>
    </div>
  </body>
  </html>)rawliteral";

// Trang HTML cấu hình theo phương án mới
const char* configPage1 = R"rawliteral(
  <!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cấu hình Wi-Fi</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f4f4f9;
      margin: 0;
      padding: 0;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
    }
    .container {
      background: white;
      padding: 25px;
      border-radius: 10px;
      box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
      text-align: center;
      width: 90%;
      max-width: 400px;
    }
    h2 {
      color: #333;
      margin-bottom: 20px;
    }
    label {
      font-size: 16px;
      font-weight: bold;
      display: block;
      margin-top: 10px;
      text-align: left;
    }
    select, input {
      width: 100%;
      padding: 10px;
      margin: 8px 0;
      border: 2px solid #ddd;
      border-radius: 5px;
      font-size: 16px;
    }
    input[type="submit"], button {
      background: #007bff;
      color: white;
      font-weight: bold;
      border: none;
      cursor: pointer;
      padding: 12px;
      border-radius: 5px;
      width: 100%;
      margin-top: 10px;
    }
    input[type="submit"]:hover, button:hover {
      background: #0056b3;
    }
    a {
      text-decoration: none;
      color: #007bff;
      display: block;
      margin-top: 10px;
    }
    a:hover {
      color: #0056b3;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Cấu hình Wi-Fi</h2>
    <form action="/save" method="POST">
      <label for="ssid">Chọn Wi-Fi:</label>
      <select id="ssid" name="ssid">
        <option>Đang tải danh sách...</option>
      </select>
      
      <button type="button" onclick="loadWiFiList()">🔄 Quét lại Wi-Fi</button>

      <label for="password">Mật khẩu Wi-Fi:</label>
      <input type="password" id="password" name="password" required>

      <label for="id">ID Thiết Bị:</label>
      <input type="text" id="id" name="id" required>

      <input type="submit" value="Lưu">
    </form>
    <a href="/logout">Đăng xuất</a>
  </div>

  <script>
    function loadWiFiList() {
      let select = document.getElementById("ssid");
      select.innerHTML = "<option>Đang quét...</option>";

      fetch("/wifi_scan")
        .then(response => response.json())
        .then(data => {
          select.innerHTML = "";
          data.forEach(ssid => {
            let option = document.createElement("option");
            option.value = ssid;
            option.textContent = ssid;
            select.appendChild(option);
          });
        })
        .catch(err => {
          console.error("Lỗi tải danh sách WiFi:", err);
          select.innerHTML = "<option>Lỗi tải danh sách</option>";
        });
    }

    window.onload = loadWiFiList;
  </script>
</body>
</html>

  )rawliteral";

// Page Logout
const char* logout = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Đăng xuất</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background-color: #f4f4f9;
      display: flex;
      justify-content: center;
      align-items: center;
      height: 100vh;
      margin: 0;
    }
    .message-box {
      background: #ffffff;
      padding: 20px;
      border-radius: 8px;
      box-shadow: 0 2px 4px rgba(0, 0, 0, 0.1);
      text-align: center;
      width: 90%;
      max-width: 400px;
    }
    h2 {
      color: #ff5722;
      margin-bottom: 10px;
    }
    p {
      color: #555;
      margin-bottom: 20px;
    }
    a {
      display: inline-block;
      padding: 10px 20px;
      background: #007bff;
      color: white;
      text-decoration: none;
      border-radius: 4px;
    }
    a:hover {
      background: #0056b3;
    }
  </style>
</head>
<body>
  <div class="message-box">
    <h2>Đã đăng xuất!</h2>
    <p>Bạn đã đăng xuất khỏi hệ thống.</p>
    <a href="/">Đăng nhập lại</a>
  </div>
</body>
</html>
)rawliteral";

// Đăng nhập wifi lỗi
const char* loginFail =  R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Đăng nhập thất bại</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background-color: #f8f9fa;
            margin: 0;
            padding: 0;
        }
        .container {
            max-width: 400px;
            margin: 100px auto;
            padding: 20px;
            background: #ffffff;
            box-shadow: 0px 4px 6px rgba(0, 0, 0, 0.1);
            border-radius: 8px;
        }
        h2 {
            color: #dc3545;
        }
        a {
            display: inline-block;
            margin-top: 20px;
            text-decoration: none;
            color: #007bff;
            font-weight: bold;
        }
        a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Đăng nhập thất bại!</h2>
        <p>Tên đăng nhập hoặc mật khẩu không đúng.</p>
        <a href="/">Thử lại</a>
    </div>
</body>
</html>
)rawliteral";

//  Dữ liệu server lỗi
const char* serverFail = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Thông báo lỗi</title>
    <style>
        body {
            font-family: Arial, sans-serif;
            text-align: center;
            background-color: #f8f9fa;
            margin: 0;
            padding: 0;
        }
        .container {
            max-width: 400px;
            margin: 100px auto;
            padding: 20px;
            background: #ffffff;
            box-shadow: 0px 4px 6px rgba(0, 0, 0, 0.1);
            border-radius: 8px;
        }
        h2 {
            color: #ffc107;
        }
        p {
            color: #6c757d;
        }
        a {
            display: inline-block;
            margin-top: 20px;
            text-decoration: none;
            color: #007bff;
            font-weight: bold;
        }
        a:hover {
            text-decoration: underline;
        }
    </style>
</head>
<body>
    <div class="container">
        <h2>Vui lòng điền đầy đủ thông tin</h2>
        <p>Chúng tôi cần thêm thông tin để tiếp tục xử lý yêu cầu của bạn.</p>
        <a href="/">Quay lại</a>
    </div>
</body>
</html>
)rawliteral";
// Hàm xử lý yêu cầu GET tại "/"
void handleRoot(WebServer *server, bool &isLoggedIn) {
  if (!isLoggedIn) {
    server->send(200, "text/html", loginPage);
  } else {
    server->send(200, "text/html", configPage);
  }
}
// Hàm xử lý yêu cầu GET tại "/"
void handleRootAsyn(AsyncWebServerRequest *request, bool &isLoggedIn) {
  if (!isLoggedIn) {
    request->send(200, "text/html", loginPage);
  } else {
    request->send(200, "text/html", configPage);
  }
}

// Hàm xử lý đăng nhập
void handleLogin(WebServer *server, bool &isLoggedIn) {
  String username = server->arg("username");
  String password = server->arg("password");

  if (username == adminUser && password == adminPass) {
    isLoggedIn = true;
    server->send(200, "text/html", configPage);
  } else {
    server->send(401, "text/html", loginFail);
  }
}
// Hàm xử lý đăng nhập
void handleLoginAsyn(AsyncWebServerRequest *request, bool &isLoggedIn) {
  String username = request->arg("username");
  String password = request->arg("password");

  if (username == adminUser && password == adminPass) {
    isLoggedIn = true;
    request->send(200, "text/html", configPage);
  } else {
    request->send(401, "text/html", loginFail);
  }
}

// Hàm xử lý đăng xuất
void handleLogout(WebServer *server, bool &isLoggedIn) {
  isLoggedIn = false;
  server->send(200, "text/html", logout);
}

// void handleLogoutAsyn(AsyncWebServerRequest *request, bool &isLoggedIn) {
//   isLoggedIn = false;
//   server->send(200, "text/html", logout);
// }

// Hàm xử lý lưu cấu hình
void handleSave(WebServer *server, bool &isLoggedIn) {
  String ssid = server->arg("ssid");
  String password = server->arg("password");
  String id = server->arg("id");

  if (ssid.length() > 0 && password.length() > 0 && id.length() > 0) {
    String data = ssid + "\n" + password + "\n" + id;
    Serial.println(data);
    writeFileConfig("/config.txt", data);
    server->send(200, "text/html", result);
    delay(2000);
    ESP.restart();
  } else {
    server->send(400, "text/html", serverFail);
  }
}

// Hàm xử lý lưu cấu hình
void handleSaveAyss(AsyncWebServerRequest *server, bool &isLoggedIn) {
  String ssid = server->arg("ssid");
  String password = server->arg("password");
  String id = server->arg("id");

  if (ssid.length() > 0 && password.length() > 0 && id.length() > 0) {
    String data = ssid + "\n" + password + "\n" + id;
    Serial.println(data);
    writeFileConfig("/config.txt", data);
    server->send(200, "text/html", result);
    delay(2000);
    ESP.restart();
  } else {
    server->send(400, "text/html", serverFail);
  }
}