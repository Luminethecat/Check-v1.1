#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <EEPROM.h>  // 新增：引入EEPROM库，用于永久存储配置

// ===================== 【唯一需要修改的初始配置】必改！2.4G WiFi（ESP8266不支持5G）=====================
#define DEFAULT_WIFI_SSID "TP-LINK_5A00"  // 改为你的默认2.4G WiFi账号（首次烧录使用）
#define DEFAULT_WIFI_PWD  "13783049779"   // 改为你的默认2.4G WiFi密码（首次烧录使用）
#define DEFAULT_WORK_TIME "09:00"         // 默认上班时间（首次烧录使用）
#define DEFAULT_OFF_TIME  "18:00"         // 默认下班时间（首次烧录使用）
const char* MQTT_USER = "user";          // EMQX用户名（无需改，和后端一致）
const char* MQTT_PWD  = "123456";        // EMQX密码（无需改，和后端一致）
// ======================================================================================================

// EEPROM配置定义（新增：固定存储地址和大小，无需修改）
#define EEPROM_SIZE    512    // 分配512字节闪存，足够存储所有配置
#define ADDR_WIFI_SSID 0      // WiFi账号存储起始地址，占32字节
#define ADDR_WIFI_PWD  32     // WiFi密码存储起始地址，占32字节
#define ADDR_WORK_TIME 64     // 上班时间存储起始地址，占8字节
#define ADDR_OFF_TIME  72     // 下班时间存储起始地址，占8字节

// EMQX Cloud固定配置（和后端完全一致，勿改）
const char* MQTT_BROKER = "mb300ee7.ala.cn-hangzhou.emqxsl.cn";
const uint16_t MQTT_PORT = 8883;
#define BAUD_RATE 115200                     // 与STM32串口波特率一致，严格勿改

// NTP北京时间配置（东八区，自动同步，勿改）
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "cn.pool.ntp.org", 8 * 3600, 0); // 8小时时差，实时同步
char timeStr[32] = {0};  // 固定格式：YYYY-MM-DD HH:MM:SS（20字节）

// 帧协议配置（和STM32/后端完全一致，勿改）
#define FRAME_HEAD          0xAA
#define FRAME_TAIL          0x55
#define TYPE_BJ_TIME        0x01    // ESP→STM32 时间帧
#define TYPE_CHECK_DATA     0x02    // STM32→ESP 打卡帧（核心）
#define TYPE_TIME_REQ       0x03    // STM32→ESP 时间请求帧
#define TYPE_ADD_USER       0x04    // ESP→STM32 新增用户帧
#define TYPE_REMOTE_CHECKIN 0x05    // ESP→STM32 远程打卡帧（小程序触发）
// 帧协议配置（新增2个帧类型，和原有一致）
#define TYPE_RESTART_STM32   0x06    // ESP→STM32 STM32重启帧
#define TYPE_SET_WORK_TIME   0x07    // ESP→STM32 打卡时间设置帧
#define FRAME_BUF_LEN       256     // 帧缓冲区，防溢出

// 时间同步间隔（按需微调，单位：毫秒，勿改也可）
#define NTP_SYNC_INTERVAL   600000  // ESP自身NTP校准：10分钟
#define TIME_SEND_INTERVAL  60000   // ESP给STM32补发时间：1分钟

// CRC16-MODBUS 全局变量（查表法，高效校验）
uint16_t crc_table[256];

// MQTT-TLS 全局变量
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
String DEVICE_ID = "";       // 设备ID（MAC自动生成，唯一）
String MQTT_TOPIC_PUB;       // 发布主题：attendance/设备ID/data
String MQTT_TOPIC_SUB;       // 订阅主题：attendance/设备ID/cmd

// 串口接收缓冲区（解析STM32帧用）
uint8_t serial_buf[FRAME_BUF_LEN] = {0};
uint8_t serial_buf_idx = 0;

// 时间戳变量（记录上一次同步/下发时间）
unsigned long last_ntp_sync = 0;
unsigned long last_time_send = 0;

// 全局配置变量（修改：从EEPROM读取，替代原固定字符串）
char WIFI_SSID[32] = {0};  // WiFi账号（32字节，EEPROM读取）
char WIFI_PWD[32] = {0};   // WiFi密码（32字节，EEPROM读取）
char WORK_TIME[8] = {0};   // 上班时间（8字节，EEPROM读取）
char OFF_TIME[8] = {0};    // 下班时间（8字节，EEPROM读取）

// 所有函数声明（新增EEPROM相关函数，保留原有所有声明）
void initWiFi();
void formatBeijingTime();
void CRC16_Modbus_Init_Table();
uint16_t CRC16_Modbus_Table(uint8_t* data, uint8_t len);
void sendFrame(uint8_t type, uint8_t* data, uint8_t len);
bool checkFrameValid(uint8_t* frame, uint8_t len);
void parseSTM32Frame();
void handleSTM32Frame(uint8_t type, uint8_t* data, uint8_t len);
void getDeviceID();
void initMqtt();
void mqttReconnect();
void handleMqttCmd(char* topic, byte* payload, unsigned int length);
String frame2Json(uint8_t type, uint8_t* data, uint8_t len);
String getCheckInTypeDesc(uint8_t type);
void forceNtpSyncAndSend();
void cleanString(char* str, uint8_t len);
void blinkLED(uint8_t times, uint16_t delayMs);
void reconnectWiFi(char* newSsid, char* newPwd); // 修改：参数改为char*，适配EEPROM
// 新增：EEPROM相关函数声明
void initEeprom();
void eepromWriteStr(int addr, char* str, int maxLen);
void eepromReadStr(int addr, char* buf, int maxLen);
void restoreDefaultConfig();

void setup() {
  Serial.begin(BAUD_RATE);    // 初始化串口（和STM32一致）
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, HIGH); // ESP8266内置LED：高电平灭，低电平亮

  // 核心模块初始化（调整顺序：先初始化EEPROM读取配置）
  CRC16_Modbus_Init_Table();  // 初始化CRC校验表
  initEeprom();               // 新增：初始化EEPROM，读取WiFi/打卡时间配置
  initWiFi();                 // 连接WiFi（使用EEPROM读取的配置）
  getDeviceID();              // 生成设备ID和MQTT主题
  initMqtt();                 // 初始化MQTT-TLS

  // 初始化成功：WiFi连通则同步NTP并给STM32发时间
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    forceNtpSyncAndSend();    // 首次强制同步时间
    digitalWrite(LED_BUILTIN, LOW); // LED常亮=初始化成功
    Serial.println("=== ESP8266初始化完成：EEPROM+WiFi+NTP+MQTT+CRC16 ===");
    Serial.printf("设备ID：%s\n发布主题：%s\n订阅主题：%s\n", 
                  DEVICE_ID.c_str(), MQTT_TOPIC_PUB.c_str(), MQTT_TOPIC_SUB.c_str());
    Serial.printf("当前配置：WiFi=%s | 上班=%s | 下班=%s\n", WIFI_SSID, WORK_TIME, OFF_TIME);
  } else {
    Serial.println("=== 初始化失败：WiFi连接失败！检查2.4G WiFi名/密码 ===");
  }
}

void loop() {
  // 1. WiFi断连自动重连（使用EEPROM读取的当前配置）
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    initWiFi();
    delay(1000);
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(LED_BUILTIN, LOW);
      forceNtpSyncAndSend(); // 重连成功后强制同步时间
    }
    return;
  }

  // 2. MQTT断连自动重连
  if (!mqttClient.connected()) {
    mqttReconnect();
    delay(500);
    return;
  }
  mqttClient.loop(); // MQTT轮询（必需，接收小程序指令）

  // 3. 实时解析STM32发来的串口帧（打卡/时间请求）
  parseSTM32Frame();

  // 4. 定时时间同步：10分钟校准ESP时间，1分钟给STM32补发时间
  unsigned long now = millis();
  if (now - last_ntp_sync >= NTP_SYNC_INTERVAL) {
    if (timeClient.update()) {
      formatBeijingTime();
      last_ntp_sync = now;
      sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
      last_time_send = now;
      Serial.printf("📌 NTP定时校准成功：%s，已推送给STM32\n", timeStr);
    } else {
      Serial.println("❌ NTP定时校准失败，检查网络！");
    }
  }
  if (now - last_time_send >= TIME_SEND_INTERVAL) {
    formatBeijingTime();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = now;
    Serial.printf("📤 给STM32补发时间：%s\n", timeStr);
  }
}

// ===================== 新增：EEPROM核心函数（配置永久存储）=====================
// 初始化EEPROM，读取配置；首次使用则写入默认配置
void initEeprom() {
  EEPROM.begin(EEPROM_SIZE); // 初始化EEPROM，分配指定大小
  // 从EEPROM读取所有配置到全局变量
  eepromReadStr(ADDR_WIFI_SSID, WIFI_SSID, 32);
  eepromReadStr(ADDR_WIFI_PWD, WIFI_PWD, 32);
  eepromReadStr(ADDR_WORK_TIME, WORK_TIME, 8);
  eepromReadStr(ADDR_OFF_TIME, OFF_TIME, 8);

  // 首次使用校验：如果配置为空（全0/0xFF），写入默认配置
  bool isFirstUse = false;
  if (WIFI_SSID[0] == '\0' || WIFI_SSID[0] == 0xFF) isFirstUse = true;
  if (WORK_TIME[0] == '\0' || WORK_TIME[0] == 0xFF) isFirstUse = true;

  if (isFirstUse) {
    Serial.println("📦 首次使用，写入默认配置到EEPROM...");
    restoreDefaultConfig(); // 写入默认WiFi/打卡时间
    // 重新读取默认配置到全局变量
    eepromReadStr(ADDR_WIFI_SSID, WIFI_SSID, 32);
    eepromReadStr(ADDR_WIFI_PWD, WIFI_PWD, 32);
    eepromReadStr(ADDR_WORK_TIME, WORK_TIME, 8);
    eepromReadStr(ADDR_OFF_TIME, OFF_TIME, 8);
  }
  EEPROM.end(); // 读取完成，关闭EEPROM节省资源
  Serial.printf("✅ EEPROM初始化完成，读取配置：WiFi=%s | 上班=%s | 下班=%s\n", WIFI_SSID, WORK_TIME, OFF_TIME);
}

// 向EEPROM指定地址写入字符串（封装，避免重复代码）
void eepromWriteStr(int addr, char* str, int maxLen) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, str[i]);
    if (str[i] == '\0' || i >= maxLen - 1) break; // 遇到结束符或到最大长度，停止写入
  }
  EEPROM.commit(); // 关键：提交写入，否则不会真正保存到Flash
  EEPROM.end();
  Serial.printf("💾 EEPROM写入：地址%d | 内容：%s\n", addr, str);
}

// 从EEPROM指定地址读取字符串到缓冲区（封装，避免重复代码）
void eepromReadStr(int addr, char* buf, int maxLen) {
  EEPROM.begin(EEPROM_SIZE);
  memset(buf, '\0', maxLen); // 清空缓冲区，避免脏数据
  for (int i = 0; i < maxLen - 1; i++) {
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == '\0' || buf[i] == 0xFF) break; // 遇到结束符或空值，停止读取
  }
  EEPROM.end();
}

// 恢复默认配置并写入EEPROM（首次使用/配置错乱时调用）
void restoreDefaultConfig() {
  eepromWriteStr(ADDR_WIFI_SSID, (char*)DEFAULT_WIFI_SSID, 32);
  eepromWriteStr(ADDR_WIFI_PWD, (char*)DEFAULT_WIFI_PWD, 32);
  eepromWriteStr(ADDR_WORK_TIME, (char*)DEFAULT_WORK_TIME, 8);
  eepromWriteStr(ADDR_OFF_TIME, (char*)DEFAULT_OFF_TIME, 8);
}

// ===================== 原有函数修改+保留（仅适配EEPROM配置）=====================
// 强制NTP同步并立即给STM32下发时间（联网/重连时调用）
void forceNtpSyncAndSend() {
  Serial.println("🔍 开始强制同步NTP北京时间...");
  int retry = 0;
  while (!timeClient.update() && retry < 5) {
    timeClient.forceUpdate();
    delay(300);
    retry++;
  }
  if (timeClient.update()) {
    formatBeijingTime();
    last_ntp_sync = millis();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = millis();
    Serial.printf("✅ NTP同步成功：%s，已推送给STM32\n", timeStr);
  } else {
    Serial.println("❌ NTP同步失败，请检查网络连接！");
  }
}

// 修改：WiFi动态重连函数（参数改为char*，适配EEPROM，新增写入EEPROM逻辑）
void reconnectWiFi(char* newSsid, char* newPwd) {
  // 保存原配置（用于失败回滚）
  char oldSsid[32] = {0};
  char oldPwd[32] = {0};
  strncpy(oldSsid, WIFI_SSID, 31);
  strncpy(oldPwd, WIFI_PWD, 31);

  // 更新全局WiFi配置
  strncpy(WIFI_SSID, newSsid, 31);
  if (newPwd != NULL && strlen(newPwd) > 0) {
    strncpy(WIFI_PWD, newPwd, 31);
  } else {
    memset(WIFI_PWD, '\0', 32); // 开放WiFi，密码置空
  }

  // 断开原有WiFi连接
  WiFi.disconnect();
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH); // LED灭，标识重连中

  // 打印新配置
  Serial.print("📶 开始连接新WiFi：");
  Serial.print(WIFI_SSID);
  Serial.println(strlen(WIFI_PWD) == 0 ? "（开放WiFi）" : "（密码已设置）");

  // 连接新WiFi
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 60) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  // 连接结果判断
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("");
    Serial.println("✅ 新WiFi连接成功！IP地址：" + WiFi.localIP().toString());
    // 新增：将新WiFi配置写入EEPROM永久保存
    eepromWriteStr(ADDR_WIFI_SSID, WIFI_SSID, 32);
    eepromWriteStr(ADDR_WIFI_PWD, WIFI_PWD, 32);
    digitalWrite(LED_BUILTIN, LOW); // LED常亮，标识成功
    forceNtpSyncAndSend(); // 同步NTP时间并推送给STM32
    mqttReconnect();       // 重新连接MQTT并订阅主题
    // 上报WiFi修改成功状态到MQTT
    String resp = "{\"status\":\"success\",\"msg\":\"WiFi配置修改成功\",\"new_wifi_ssid\":\"" + String(WIFI_SSID) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
  } else {
    Serial.println("");
    Serial.println("❌ 新WiFi连接失败，回滚到原WiFi配置！");
    // 回滚原配置
    strncpy(WIFI_SSID, oldSsid, 31);
    strncpy(WIFI_PWD, oldPwd, 31);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 60) { delay(500); retry++; }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("✅ 原WiFi重连成功！");
      digitalWrite(LED_BUILTIN, LOW);
      mqttReconnect();
    } else {
      Serial.println("❌ 原WiFi也连接失败，请检查硬件！");
    }
    // 上报WiFi修改失败状态到MQTT
    String resp = "{\"status\":\"fail\",\"msg\":\"WiFi配置修改失败，已回滚原配置\",\"old_wifi_ssid\":\"" + String(oldSsid) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
  }
}

// WiFi初始化（使用EEPROM读取的配置，适配动态修改）
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  Serial.printf("正在连接WiFi：%s...\n", WIFI_SSID);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✅ WiFi连接成功！IP地址：" + WiFi.localIP().toString());
  }
}

// NTP时间格式化：转为YYYY-MM-DD HH:MM:SS（STM32可直接解析）
void formatBeijingTime() {
  time_t rawTime = timeClient.getEpochTime();
  struct tm* timeInfo = localtime(&rawTime);
  sprintf(timeStr, "%04d-%02d-%02d %02d:%02d:%02d",
          timeInfo->tm_year + 1900,
          timeInfo->tm_mon + 1,
          timeInfo->tm_mday,
          timeInfo->tm_hour,
          timeInfo->tm_min,
          timeInfo->tm_sec);
  timeStr[20] = '\0'; // 强制结束符，避免溢出
}

// CRC16-MODBUS 查表法初始化
void CRC16_Modbus_Init_Table() {
  uint16_t crc, poly = 0xA001;
  for (int i = 0; i < 256; i++) {
    crc = i;
    for (int j = 0; j < 8; j++) {
      crc = (crc & 0x0001) ? (crc >> 1) ^ poly : crc >> 1;
    }
    crc_table[i] = crc;
  }
}

// CRC16-MODBUS 计算（高字节在前）
uint16_t CRC16_Modbus_Table(uint8_t* data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc = (crc >> 8) ^ crc_table[(crc & 0xFF) ^ data[i]];
  }
  return crc;
}

// ESP→STM32 通用帧发送函数（封装帧头/类型/CRC/帧尾）
void sendFrame(uint8_t type, uint8_t* data, uint8_t len) {
  if (len >= FRAME_BUF_LEN - 7) {
    Serial.println("❌ 发送帧失败：数据过长！");
    return;
  }
  uint8_t frame[FRAME_BUF_LEN] = {0};
  uint8_t idx = 0;
  frame[idx++] = FRAME_HEAD;    // 帧头
  frame[idx++] = type;          // 帧类型
  frame[idx++] = len;           // 数据长度
  if (len > 0 && data != NULL) {
    memcpy(&frame[idx], data, len);
    idx += len;
  }
  // CRC校验
  uint16_t crc = CRC16_Modbus_Table(data, len);
  frame[idx++] = (crc >> 8) & 0xFF;
  frame[idx++] = crc & 0xFF;
  frame[idx++] = FRAME_TAIL;    // 帧尾
  // 串口发送
  for (int i = 0; i < idx; i++) {
    Serial.write(frame[i]);
  }
}

// 帧有效性校验（帧头/帧尾/CRC/长度）
bool checkFrameValid(uint8_t* frame, uint8_t len) {
  if (len < 7) return false;
  if (frame[0] != FRAME_HEAD || frame[len-1] != FRAME_TAIL) return false;
  uint8_t data_len = frame[2];
  uint8_t actual_data_len = len - 6;
  if (actual_data_len != data_len) return false;
  uint16_t crc_calc = CRC16_Modbus_Table(&frame[3], data_len);
  uint16_t crc_recv = (frame[3+data_len] << 8) | frame[3+data_len+1];
  return crc_calc == crc_recv;
}

// 解析STM32串口帧（帧头检测+帧尾触发校验）
void parseSTM32Frame() {
  while (Serial.available() > 0) {
    uint8_t ch = Serial.read();
    if (serial_buf_idx > 0) {
      if (serial_buf_idx < FRAME_BUF_LEN - 1) {
        serial_buf[serial_buf_idx++] = ch;
        if (ch == FRAME_TAIL) {
          if (checkFrameValid(serial_buf, serial_buf_idx)) {
            uint8_t type = serial_buf[1];
            uint8_t len = serial_buf[2];
            uint8_t* data = &serial_buf[3];
            handleSTM32Frame(type, data, len);
            blinkLED(1, 200); // 收到有效帧，LED闪1次
          }
          serial_buf_idx = 0;
          memset(serial_buf, 0, FRAME_BUF_LEN);
        }
      } else {
        serial_buf_idx = 0;
        memset(serial_buf, 0, FRAME_BUF_LEN);
      }
    } else if (ch == FRAME_HEAD) {
      serial_buf_idx = 0;
      memset(serial_buf, 0, FRAME_BUF_LEN);
      serial_buf[serial_buf_idx++] = ch;
    }
  }
}

// 处理STM32有效帧（时间请求/打卡数据）
void handleSTM32Frame(uint8_t type, uint8_t* data, uint8_t len) {
  // 处理时间请求帧：立即回复STM32最新时间
  if (type == TYPE_TIME_REQ) {
    formatBeijingTime();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = millis();
    Serial.printf("📩 收到STM32时间请求，回复：%s\n", timeStr);
    return;
  }

  // MQTT未连接，无法上报数据
  if (!mqttClient.connected()) {
    Serial.println("❌ MQTT未连接，打卡数据无法上报！");
    return;
  }

  // 处理打卡帧：解析并打印打卡信息
  if (type == TYPE_CHECK_DATA && len >= 25) {
    uint32_t user_id = (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | data[3];
    char check_time[21] = {0};
    memcpy(check_time, &data[4], 20);
    check_time[20] = '\0';
    cleanString(check_time, 20);
    uint8_t check_type = data[24];
    Serial.printf("🎉 打卡成功！%s | 用户ID：%d | 时间：%s\n", 
                  getCheckInTypeDesc(check_type).c_str(), user_id, check_time);
  }

  // 帧转JSON，上报到EMQX（后端自动入库）
  String json = frame2Json(type, data, len);
  mqttClient.publish(MQTT_TOPIC_PUB.c_str(), json.c_str());
  Serial.printf("📤 打卡数据上报MQTT：%s\n", json.c_str());
}

// 生成设备ID（MAC地址）和MQTT主题
void getDeviceID() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  DEVICE_ID = "esp_" + mac;
  MQTT_TOPIC_PUB = "attendance/" + DEVICE_ID + "/data";
  MQTT_TOPIC_SUB = "attendance/" + DEVICE_ID + "/cmd";
}

// MQTT-TLS初始化（跳过证书验证，适配免费EMQX）
void initMqtt() {
  espClient.setInsecure(); // 关键：免费EMQX无需证书，跳过验证
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMqttCmd); // 指令回调函数
}

// MQTT断连重连+重新订阅主题
void mqttReconnect() {
  Serial.print("正在连接EMQX...");
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_ID.c_str(), MQTT_USER, MQTT_PWD)) {
      Serial.println("✅ EMQX连接成功！");
      mqttClient.subscribe(MQTT_TOPIC_SUB.c_str());
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      Serial.printf("❌ EMQX连接失败！错误码：%d，5秒后重试\n", mqttClient.state());
      digitalWrite(LED_BUILTIN, HIGH);
      delay(5000);
    }
  }
}

void handleMqttCmd(char* topic, byte* payload, unsigned int length) {
  char cmd[128] = {0};
  memcpy(cmd, payload, length);
  cmd[length] = '\0';
  Serial.printf("📩 收到小程序指令：%s\n", cmd);
  blinkLED(2, 200); // 收到指令，LED闪2次

  // ==============================================
  // 【第一步：优先处理 setwifi/setWifi 指令，放在最最前面！】
  // ==============================================
  // 检查指令是否以 "setwifi|" 或 "setWifi|" 开头（不区分大小写）
  if (strstr(cmd, "setwifi|") == cmd || strstr(cmd, "setWifi|") == cmd) {
    Serial.println("🔧 识别到 WiFi 配置指令，开始解析...");
    // 找到第一个 | 的位置，指向 SSID 的开始
    char* firstPipe = strstr(cmd, "|");
    if (firstPipe == NULL) {
      Serial.println("❌ WiFi 指令格式错误，缺少 SSID");
      return;
    }
    char* ssidStart = firstPipe + 1;

    // 找到第二个 | 的位置，区分 SSID 和 PWD
    char* secondPipe = strstr(ssidStart, "|");
    if (secondPipe == NULL) {
      // 没有第二个 |，说明是开放 WiFi
      char newSsid[32] = {0};
      strncpy(newSsid, ssidStart, 31);
      Serial.printf("🔧 开始修改 WiFi：新SSID=%s，密码为空（开放WiFi）\n", newSsid);
      reconnectWiFi(newSsid, "");
    } else {
      // 有第二个 |，分别提取 SSID 和 PWD
      char newSsid[32] = {0};
      char newPwd[32] = {0};
      strncpy(newSsid, ssidStart, secondPipe - ssidStart); // 复制 SSID
      strncpy(newPwd, secondPipe + 1, 31); // 复制 PWD
      Serial.printf("🔧 开始修改 WiFi：新SSID=%s，新PWD=%s\n", newSsid, newPwd);
      reconnectWiFi(newSsid, newPwd);
    }
    return; // 处理完毕，直接返回，不再执行后面的代码！
  }

  // ==============================================
  // 【第二步：处理其他所有指令（原有逻辑）】
  // ==============================================
  char* cmdCode = strtok(cmd, "_");
  if (cmdCode == NULL) {
    Serial.println("❌ 指令格式错误，无法解析");
    return;
  }

  char* param1 = strtok(NULL, "_");
  char* param2 = strtok(NULL, "_");
  char* param3 = strtok(NULL, "_");

  // 1. 同步时间指令：syncTime
  if (strcmp(cmdCode, "syncTime") == 0) {
    formatBeijingTime();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = millis();
    Serial.printf("📤 给STM32同步时间：%s\n", timeStr);
    String resp = "{\"status\":\"success\",\"msg\":\"时间同步成功\",\"time\":\"" + String(timeStr) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 2. 通用远程打卡：checkIn
  if (strcmp(cmdCode, "checkIn") == 0) {
    char msg[16] = "remote_checkin";
    sendFrame(TYPE_REMOTE_CHECKIN, (uint8_t*)msg, strlen(msg));
    Serial.println("📤 通用远程打卡指令发送给STM32！");
    String resp = "{\"status\":\"success\",\"msg\":\"远程打卡指令触发\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 3. 指定员工远程打卡：checkInUser_UID
  if (strcmp(cmdCode, "checkInUser") == 0 && param1 != NULL) {
    uint32_t userUid = atol(param1);
    uint8_t uidData[4] = {0};
    uidData[0] = (userUid >> 24) & 0xFF;
    uidData[1] = (userUid >> 16) & 0xFF;
    uidData[2] = (userUid >> 8) & 0xFF;
    uidData[3] = userUid & 0xFF;
    sendFrame(TYPE_REMOTE_CHECKIN, uidData, 4);
    Serial.printf("📤 指定员工远程打卡：UID=%d，指令发送给STM32！\n", userUid);
    String resp = "{\"status\":\"success\",\"msg\":\"指定员工打卡指令触发\",\"user_id\":" + String(userUid) + "}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 4. 重启STM32设备：restartSTM32
  if (strcmp(cmdCode, "restartSTM32") == 0) {
    char restartMsg[16] = "stm32_restart";
    sendFrame(TYPE_RESTART_STM32, (uint8_t*)restartMsg, strlen(restartMsg));
    Serial.println("📤 重启STM32指令发送给STM32！");
    String resp = "{\"status\":\"success\",\"msg\":\"STM32重启指令已发送\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 5. 重启ESP网络：restartNet
  if (strcmp(cmdCode, "restartNet") == 0) {
    Serial.println("🔄 ESP开始重连WiFi+MQTT...");
    digitalWrite(LED_BUILTIN, HIGH);
    WiFi.disconnect();
    delay(1000);
    initWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      forceNtpSyncAndSend();
      mqttReconnect();
      digitalWrite(LED_BUILTIN, LOW);
      Serial.println("✅ ESP网络重启成功！");
      String resp = "{\"status\":\"success\",\"msg\":\"ESP网络重启成功\"}";
      mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    } else {
      Serial.println("❌ ESP网络重启失败，WiFi未连接！");
      String resp = "{\"status\":\"fail\",\"msg\":\"ESP网络重启失败\"}";
      mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    }
    return;
  }

  // 6. 设置打卡时间：setWorkTime_09:00_18:00
  if (strcmp(cmdCode, "setWorkTime") == 0 && param1 != NULL && param2 != NULL) {
    strncpy(WORK_TIME, param1, 7);
    strncpy(OFF_TIME, param2, 7);
    eepromWriteStr(ADDR_WORK_TIME, WORK_TIME, 8);
    eepromWriteStr(ADDR_OFF_TIME, OFF_TIME, 8);
    char workTimeData[40] = {0};
    sprintf(workTimeData, "%s|%s", param1, param2);
    sendFrame(TYPE_SET_WORK_TIME, (uint8_t*)workTimeData, strlen(workTimeData));
    Serial.printf("📤 设置打卡时间（永久保存）：上班=%s，下班=%s，指令发送给STM32！\n", WORK_TIME, OFF_TIME);
    String resp = "{\"status\":\"success\",\"msg\":\"打卡时间设置成功（永久保存）\",\"work_time\":\"" + String(WORK_TIME) + "\",\"off_work_time\":\"" + String(OFF_TIME) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 兼容原有新增用户指令（兜底）
  uint8_t cmd_data[128] = {0};
  memcpy(cmd_data, cmd, length);
  sendFrame(TYPE_ADD_USER, cmd_data, length);
  Serial.println("📤 新增用户指令发送给STM32！");
}

// 帧转JSON：适配后端解析格式（含设备ID/用户ID/打卡时间）（保留原有逻辑）
String frame2Json(uint8_t type, uint8_t* data, uint8_t len) {
  String json = "{\"device_id\":\"" + DEVICE_ID + "\",\"frame_type\":" + String(type);
  if (type == TYPE_CHECK_DATA && len >= 25) {
    uint32_t user_id = (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | data[3];
    char check_time[21] = {0};
    memcpy(check_time, &data[4], 20);
    check_time[20] = '\0';
    cleanString(check_time, 20);
    uint8_t check_type = data[24];
    json += ",\"user_id\":" + String(user_id) + 
            ",\"check_time\":\"" + String(check_time) + 
            "\",\"check_type_code\":" + String(check_type) + 
            ",\"check_type_desc\":\"" + getCheckInTypeDesc(check_type) + "\"";
  } else if (len > 0 && data != NULL) {
    char temp[128] = {0};
    memcpy(temp, data, len);
    cleanString(temp, len);
    json += ",\"data\":\"" + String(temp) + "\"";
  } else {
    json += ",\"data\":\"无\"";
  }
  json += "}";
  return json;
}

// 打卡类型码转文字描述（和后端/小程序一致）（保留原有逻辑）
String getCheckInTypeDesc(uint8_t type) {
  switch (type) {
    case 0x01: return "上班打卡";
    case 0x02: return "下班打卡";
    case 0x03: return "迟到打卡";
    case 0x04: return "提前打卡";
    default:   return "正常打卡";
  }
}

// 字符串清洗：过滤不可见字符，避免JSON解析失败（保留原有逻辑）
void cleanString(char* str, uint8_t len) {
  if (str == NULL || len == 0) return;
  uint8_t j = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (str[i] >= 0x20 && str[i] <= 0x7E) { // 只保留可打印ASCII字符
      str[j++] = str[i];
    }
  }
  str[j] = '\0';
}

// LED闪烁提示：直观显示设备状态（次数/间隔可改）（保留原有逻辑）
void blinkLED(uint8_t times, uint16_t delayMs) {
  for (uint8_t i = 0; i < times; i++) {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(delayMs);
    digitalWrite(LED_BUILTIN, LOW);
    delay(delayMs);
  }
  if (mqttClient.connected()) {
    digitalWrite(LED_BUILTIN, LOW); // 闪烁后恢复常亮
  }
}
