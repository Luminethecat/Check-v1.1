#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <PubSubClient.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <EEPROM.h>

// ===================== 基础配置（首次烧录需修改）=====================
#define DEFAULT_WIFI_SSID "11111111"  // 初始2.4G WiFi账号
#define DEFAULT_WIFI_PWD  "11111111"   // 初始2.4G WiFi密码
#define DEFAULT_WORK_TIME "09:00"         // 初始上班打卡时间
#define DEFAULT_OFF_TIME  "18:00"         // 初始下班打卡时间
const char* MQTT_USER = "user";          // EMQX认证用户名（与后端一致）
const char* MQTT_PWD  = "123456";        // EMQX认证密码（与后端一致）

// ===================== EEPROM 存储配置（无需修改）=====================
#define EEPROM_SIZE    512    // EEPROM总容量（字节）
#define ADDR_WIFI_SSID 0      // WiFi账号存储起始地址（32字节）
#define ADDR_WIFI_PWD  32     // WiFi密码存储起始地址（32字节）
#define ADDR_WORK_TIME 64     // 上班时间存储起始地址（8字节）
#define ADDR_OFF_TIME  72     // 下班时间存储起始地址（8字节）

// ===================== 固定通信配置（与后端/STM32 强绑定）=====================
const char* MQTT_BROKER = "rf8b8361.ala.cn-hangzhou.emqxsl.cn"; // EMQX云端地址
const uint16_t MQTT_PORT = 8883;                                // EMQX TLS端口
#define BAUD_RATE 115200                                         // 与STM32串口波特率一致

// ===================== 帧协议定义（与STM32 严格一致）=====================
#define FRAME_HEAD          0xAA    // 帧头
#define FRAME_TAIL          0x55    // 帧尾
#define TYPE_BJ_TIME        0x01    // ESP→STM32：北京时间帧
#define TYPE_CHECK_DATA     0x02    // STM32→ESP：打卡数据帧
#define TYPE_TIME_REQ       0x03    // STM32→ESP：时间请求帧
#define TYPE_ADD_USER       0x04    // ESP→STM32：新增用户帧
#define TYPE_REMOTE_CHECKIN 0x05    // ESP→STM32：远程打卡帧
#define TYPE_RESTART_STM32  0x06    // ESP→STM32：STM32重启帧
#define TYPE_SET_WORK_TIME  0x07    // ESP→STM32：打卡时间设置帧
#define FRAME_BUF_LEN       256     // 帧缓冲区最大长度（防溢出）

// ===================== 定时配置（毫秒）=====================
#define NTP_SYNC_INTERVAL   600000  // NTP时间校准间隔（10分钟）
#define TIME_SEND_INTERVAL  60000   // 向STM32补发时间间隔（1分钟）

// ===================== 全局变量 =====================
// NTP时间相关
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "cn.pool.ntp.org", 8 * 3600, 0); // 东八区时间同步
char timeStr[32] = {0};                                       // 格式化时间缓存（YYYY-MM-DD HH:MM:SS）

// CRC校验相关
uint16_t crc_table[256]; // CRC16-MODBUS 查表法缓存

// MQTT通信相关
WiFiClientSecure espClient;
PubSubClient mqttClient(espClient);
String DEVICE_ID = "";       // 设备唯一ID（MAC地址生成）
String MQTT_TOPIC_PUB;       // 发布主题：attendance/设备ID/data
String MQTT_TOPIC_SUB;       // 订阅主题：attendance/设备ID/cmd

// 串口解析相关
uint8_t serial_buf[FRAME_BUF_LEN] = {0}; // STM32串口数据缓冲区
uint8_t serial_buf_idx = 0;              // 缓冲区索引

// 定时控制相关
unsigned long last_ntp_sync = 0;  // 上次NTP同步时间戳
unsigned long last_time_send = 0; // 上次向STM32发时间戳

// 配置存储相关（从EEPROM读取）
char WIFI_SSID[32] = {0};  // 当前WiFi账号
char WIFI_PWD[32] = {0};   // 当前WiFi密码
char WORK_TIME[8] = {0};   // 当前上班时间
char OFF_TIME[8] = {0};    // 当前下班时间

// ===================== 函数声明 =====================
// 核心初始化函数
void initWiFi();
void initEeprom();
void initMqtt();
void getDeviceID();

// 时间相关函数
void formatBeijingTime();
void forceNtpSyncAndSend();

// 通信协议函数
void CRC16_Modbus_Init_Table();
uint16_t CRC16_Modbus_Table(uint8_t* data, uint8_t len);
void sendFrame(uint8_t type, uint8_t* data, uint8_t len);
bool checkFrameValid(uint8_t* frame, uint8_t len);
void parseSTM32Frame();
void handleSTM32Frame(uint8_t type, uint8_t* data, uint8_t len);

// MQTT相关函数
void mqttReconnect();
void handleMqttCmd(char* topic, byte* payload, unsigned int length);

// 数据处理函数
String frame2Json(uint8_t type, uint8_t* data, uint8_t len);
String getCheckInTypeDesc(uint8_t type);
void cleanString(char* str, uint8_t len);

// 配置管理函数
void eepromWriteStr(int addr, char* str, int maxLen);
void eepromReadStr(int addr, char* buf, int maxLen);
void restoreDefaultConfig();
void reconnectWiFi(char* newSsid, char* newPwd);

// 辅助函数
void blinkLED(uint8_t times, uint16_t delayMs);

// ===================== 初始化函数 =====================
void setup() {
  Serial.begin(BAUD_RATE);          // 初始化串口（与STM32通信）
  pinMode(LED_BUILTIN, OUTPUT);     // 初始化内置LED（状态指示）
  digitalWrite(LED_BUILTIN, HIGH);  // ESP8266 LED：高电平灭，低电平亮

  // 核心模块初始化（顺序不可乱）
  CRC16_Modbus_Init_Table();  // 初始化CRC校验表
  initEeprom();               // 读取EEPROM配置（WiFi/打卡时间）
  initWiFi();                 // 连接WiFi（使用EEPROM配置）
  getDeviceID();              // 生成设备ID和MQTT主题
  initMqtt();                 // 初始化MQTT-TLS连接

  // 初始化成功处理
  if (WiFi.status() == WL_CONNECTED) {
    timeClient.begin();
    forceNtpSyncAndSend();    // 首次强制同步NTP时间并下发STM32
    digitalWrite(LED_BUILTIN, LOW); // LED常亮：初始化成功
    Serial.printf("ESP8266初始化完成\n设备ID：%s\n发布主题：%s\n订阅主题：%s\n当前配置：WiFi=%s | 上班=%s | 下班=%s\n",
                  DEVICE_ID.c_str(), MQTT_TOPIC_PUB.c_str(), MQTT_TOPIC_SUB.c_str(),
                  WIFI_SSID, WORK_TIME, OFF_TIME);
  } else {
    Serial.println("初始化失败：WiFi连接失败，请检查配置");
  }
}

// ===================== 主循环 =====================
void loop() {
  // 1. WiFi断连自动重连
  if (WiFi.status() != WL_CONNECTED) {
    digitalWrite(LED_BUILTIN, HIGH);
    initWiFi();
    delay(1000);
    if (WiFi.status() == WL_CONNECTED) {
      digitalWrite(LED_BUILTIN, LOW);
      forceNtpSyncAndSend(); // 重连成功后同步时间
    }
    return;
  }

  // 2. MQTT断连自动重连
  if (!mqttClient.connected()) {
    mqttReconnect();
    delay(500);
    return;
  }
  mqttClient.loop(); // MQTT消息轮询（必须调用，接收小程序指令）

  // 3. 解析STM32串口数据
  parseSTM32Frame();

  // 4. 定时同步时间
  unsigned long now = millis();
  // 10分钟校准一次NTP时间，并下发STM32
  if (now - last_ntp_sync >= NTP_SYNC_INTERVAL) {
    if (timeClient.update()) {
      formatBeijingTime();
      last_ntp_sync = now;
      sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
      last_time_send = now;
      Serial.printf("NTP定时校准：%s，已下发STM32\n", timeStr);
    } else {
      Serial.println("NTP校准失败，检查网络");
    }
  }
  // 1分钟补发一次时间给STM32（防止STM32时间丢失）
  if (now - last_time_send >= TIME_SEND_INTERVAL) {
    formatBeijingTime();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = now;
    Serial.printf("补发时间给STM32：%s\n", timeStr);
  }
}

// ===================== EEPROM配置管理函数 =====================
/**
 * @brief 初始化EEPROM，读取配置；首次使用则写入默认配置
 */
void initEeprom() {
  EEPROM.begin(EEPROM_SIZE);
  // 从EEPROM读取配置到全局变量
  eepromReadStr(ADDR_WIFI_SSID, WIFI_SSID, 32);
  eepromReadStr(ADDR_WIFI_PWD, WIFI_PWD, 32);
  eepromReadStr(ADDR_WORK_TIME, WORK_TIME, 8);
  eepromReadStr(ADDR_OFF_TIME, OFF_TIME, 8);

  // 首次使用校验：配置为空则写入默认值
  bool isFirstUse = (WIFI_SSID[0] == '\0' || WIFI_SSID[0] == 0xFF) || 
                    (WORK_TIME[0] == '\0' || WORK_TIME[0] == 0xFF);
  if (isFirstUse) {
    Serial.println("首次使用，写入默认配置到EEPROM");
    restoreDefaultConfig();
    // 重新读取默认配置
    eepromReadStr(ADDR_WIFI_SSID, WIFI_SSID, 32);
    eepromReadStr(ADDR_WIFI_PWD, WIFI_PWD, 32);
    eepromReadStr(ADDR_WORK_TIME, WORK_TIME, 8);
    eepromReadStr(ADDR_OFF_TIME, OFF_TIME, 8);
  }
  EEPROM.end();
  Serial.printf("EEPROM初始化完成：WiFi=%s | 上班=%s | 下班=%s\n", WIFI_SSID, WORK_TIME, OFF_TIME);
}

/**
 * @brief 向EEPROM指定地址写入字符串
 * @param addr 起始地址
 * @param str 要写入的字符串
 * @param maxLen 最大写入长度
 */
void eepromWriteStr(int addr, char* str, int maxLen) {
  EEPROM.begin(EEPROM_SIZE);
  for (int i = 0; i < maxLen; i++) {
    EEPROM.write(addr + i, str[i]);
    if (str[i] == '\0' || i >= maxLen - 1) break; // 结束符/最大长度终止
  }
  EEPROM.commit(); // 提交写入（关键：否则不生效）
  EEPROM.end();
  Serial.printf("EEPROM写入：地址%d | 内容：%s\n", addr, str);
}

/**
 * @brief 从EEPROM指定地址读取字符串到缓冲区
 * @param addr 起始地址
 * @param buf 接收缓冲区
 * @param maxLen 最大读取长度
 */
void eepromReadStr(int addr, char* buf, int maxLen) {
  EEPROM.begin(EEPROM_SIZE);
  memset(buf, '\0', maxLen); // 清空缓冲区
  for (int i = 0; i < maxLen - 1; i++) {
    buf[i] = EEPROM.read(addr + i);
    if (buf[i] == '\0' || buf[i] == 0xFF) break; // 空值终止
  }
  EEPROM.end();
}

/**
 * @brief 恢复默认配置并写入EEPROM
 */
void restoreDefaultConfig() {
  eepromWriteStr(ADDR_WIFI_SSID, (char*)DEFAULT_WIFI_SSID, 32);
  eepromWriteStr(ADDR_WIFI_PWD, (char*)DEFAULT_WIFI_PWD, 32);
  eepromWriteStr(ADDR_WORK_TIME, (char*)DEFAULT_WORK_TIME, 8);
  eepromWriteStr(ADDR_OFF_TIME, (char*)DEFAULT_OFF_TIME, 8);
}

// ===================== WiFi管理函数 =====================
/**
 * @brief 初始化WiFi连接（使用EEPROM配置）
 */
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  Serial.printf("连接WiFi：%s...\n", WIFI_SSID);

  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi连接成功，IP：" + WiFi.localIP().toString());
  }
}

/**
 * @brief 动态修改WiFi配置并重连（小程序指令触发）
 * @param newSsid 新WiFi账号
 * @param newPwd 新WiFi密码（空字符串表示开放WiFi）
 */
void reconnectWiFi(char* newSsid, char* newPwd) {
  // 备份原配置（失败回滚）
  char oldSsid[32] = {0}, oldPwd[32] = {0};
  strncpy(oldSsid, WIFI_SSID, 31);
  strncpy(oldPwd, WIFI_PWD, 31);

  // 更新全局配置
  strncpy(WIFI_SSID, newSsid, 31);
  if (newPwd != NULL && strlen(newPwd) > 0) {
    strncpy(WIFI_PWD, newPwd, 31);
  } else {
    memset(WIFI_PWD, '\0', 32); // 开放WiFi清空密码
  }

  // 断开原有连接
  WiFi.disconnect();
  delay(1000);
  digitalWrite(LED_BUILTIN, HIGH); // LED灭：重连中
  Serial.printf("连接新WiFi：%s %s\n", WIFI_SSID, strlen(WIFI_PWD) == 0 ? "(开放WiFi)" : "(密码已设置)");

  // 连接新WiFi
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 60) {
    delay(500);
    Serial.print(".");
    retry++;
  }

  // 结果处理
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n新WiFi连接成功，IP：" + WiFi.localIP().toString());
    // 保存新配置到EEPROM
    eepromWriteStr(ADDR_WIFI_SSID, WIFI_SSID, 32);
    eepromWriteStr(ADDR_WIFI_PWD, WIFI_PWD, 32);
    digitalWrite(LED_BUILTIN, LOW); // LED亮：成功
    forceNtpSyncAndSend();          // 同步时间
    mqttReconnect();                // 重连MQTT
    // 上报成功状态
    String resp = "{\"status\":\"success\",\"msg\":\"WiFi配置修改成功\",\"new_wifi_ssid\":\"" + String(WIFI_SSID) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
  } else {
    Serial.println("\n新WiFi连接失败，回滚原配置");
    // 回滚原配置
    strncpy(WIFI_SSID, oldSsid, 31);
    strncpy(WIFI_PWD, oldPwd, 31);
    WiFi.begin(WIFI_SSID, WIFI_PWD);
    retry = 0;
    while (WiFi.status() != WL_CONNECTED && retry < 60) { delay(500); retry++; }
    digitalWrite(LED_BUILTIN, WiFi.status() == WL_CONNECTED ? LOW : HIGH);
    mqttReconnect();
    // 上报失败状态
    String resp = "{\"status\":\"fail\",\"msg\":\"WiFi配置修改失败，已回滚\",\"old_wifi_ssid\":\"" + String(oldSsid) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
  }
}

// ===================== 时间处理函数 =====================
/**
 * @brief 格式化NTP时间为北京时间（YYYY-MM-DD HH:MM:SS）
 */
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
  timeStr[20] = '\0'; // 强制终止，防止溢出
}

/**
 * @brief 强制同步NTP时间并立即下发给STM32
 */
void forceNtpSyncAndSend() {
  Serial.println("强制同步NTP北京时间...");
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
    Serial.printf("NTP同步成功：%s，已下发STM32\n", timeStr);
  } else {
    Serial.println("NTP同步失败，检查网络");
  }
}

// ===================== CRC16校验函数 =====================
/**
 * @brief 初始化CRC16-MODBUS 查表法校验表
 */
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

/**
 * @brief CRC16-MODBUS 校验计算（查表法）
 * @param data 待校验数据
 * @param len 数据长度
 * @return 16位CRC校验值（高字节在前）
 */
uint16_t CRC16_Modbus_Table(uint8_t* data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (int i = 0; i < len; i++) {
    crc = (crc >> 8) ^ crc_table[(crc & 0xFF) ^ data[i]];
  }
  return crc;
}

// ===================== 帧通信函数 =====================
/**
 * @brief ESP向STM32发送封装帧（帧头+类型+长度+数据+CRC+帧尾）
 * @param type 帧类型（TYPE_xxx）
 * @param data 帧数据
 * @param len 数据长度
 */
void sendFrame(uint8_t type, uint8_t* data, uint8_t len) {
  if (len >= FRAME_BUF_LEN - 7) { // 预留帧头/类型/长度/CRC/帧尾空间
    Serial.println("发送帧失败：数据过长");
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
  // 计算并添加CRC校验
  uint16_t crc = CRC16_Modbus_Table(data, len);
  frame[idx++] = (crc >> 8) & 0xFF; // 高字节
  frame[idx++] = crc & 0xFF;        // 低字节
  frame[idx++] = FRAME_TAIL;        // 帧尾
  // 串口发送
  for (int i = 0; i < idx; i++) {
    Serial.write(frame[i]);
  }
}

/**
 * @brief 校验STM32接收帧的有效性（帧头/帧尾/长度/CRC）
 * @param frame 待校验帧
 * @param len 帧总长度
 * @return true-有效，false-无效
 */
bool checkFrameValid(uint8_t* frame, uint8_t len) {
  if (len < 7) return false; // 最小帧长度：头+类型+长度+CRC(2)+尾 = 6，数据至少1字节
  if (frame[0] != FRAME_HEAD || frame[len-1] != FRAME_TAIL) return false;
  uint8_t data_len = frame[2];
  uint8_t actual_data_len = len - 6; // 总长度 - 头-类型-长度-CRC(2)-尾
  if (actual_data_len != data_len) return false;
  // 校验CRC
  uint16_t crc_calc = CRC16_Modbus_Table(&frame[3], data_len);
  uint16_t crc_recv = (frame[3+data_len] << 8) | frame[3+data_len+1];
  return crc_calc == crc_recv;
}

/**
 * @brief 解析STM32串口数据（帧头检测+帧尾触发校验）
 */
void parseSTM32Frame() {
  while (Serial.available() > 0) {
    uint8_t ch = Serial.read();
    if (serial_buf_idx > 0) {
      // 已有帧头，继续接收数据
      if (serial_buf_idx < FRAME_BUF_LEN - 1) {
        serial_buf[serial_buf_idx++] = ch;
        // 检测到帧尾，校验并处理
        if (ch == FRAME_TAIL) {
          if (checkFrameValid(serial_buf, serial_buf_idx)) {
            uint8_t type = serial_buf[1];
            uint8_t len = serial_buf[2];
            uint8_t* data = &serial_buf[3];
            handleSTM32Frame(type, data, len);
            blinkLED(1, 200); // LED闪1次：收到有效帧
          }
          // 重置缓冲区
          serial_buf_idx = 0;
          memset(serial_buf, 0, FRAME_BUF_LEN);
        }
      } else {
        // 缓冲区溢出，重置
        serial_buf_idx = 0;
        memset(serial_buf, 0, FRAME_BUF_LEN);
      }
    } else if (ch == FRAME_HEAD) {
      // 检测到帧头，开始接收
      serial_buf_idx = 0;
      memset(serial_buf, 0, FRAME_BUF_LEN);
      serial_buf[serial_buf_idx++] = ch;
    }
  }
}

/**
 * @brief 处理STM32有效帧
 * @param type 帧类型
 * @param data 帧数据
 * @param len 数据长度
 */
void handleSTM32Frame(uint8_t type, uint8_t* data, uint8_t len) {
  // 时间请求帧：立即回复当前时间
  if (type == TYPE_TIME_REQ) {
    formatBeijingTime();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = millis();
    Serial.printf("收到STM32时间请求，回复：%s\n", timeStr);
    return;
  }

  // MQTT未连接，无法上报数据
  if (!mqttClient.connected()) {
    Serial.println("MQTT未连接，打卡数据无法上报");
    return;
  }

  // 打卡数据帧：解析并打印
  if (type == TYPE_CHECK_DATA && len >= 25) {
    uint32_t user_id = (data[0]<<24) | (data[1]<<16) | (data[2]<<8) | data[3];
    char check_time[21] = {0};
    memcpy(check_time, &data[4], 20);
    check_time[20] = '\0';
    cleanString(check_time, 20);
    uint8_t check_type = data[24];
    Serial.printf("打卡成功：%s | 用户ID：%d | 时间：%s\n", 
                  getCheckInTypeDesc(check_type).c_str(), user_id, check_time);
  }

  // 帧转JSON并上报MQTT（后端入库）
  String json = frame2Json(type, data, len);
  mqttClient.publish(MQTT_TOPIC_PUB.c_str(), json.c_str());
  Serial.printf("打卡数据上报MQTT：%s\n", json.c_str());
}

// ===================== MQTT相关函数 =====================
/**
 * @brief 生成设备唯一ID（MAC地址）和MQTT主题
 */
void getDeviceID() {
  String mac = WiFi.macAddress();
  mac.replace(":", "");
  DEVICE_ID = "esp_" + mac;
  MQTT_TOPIC_PUB = "attendance/" + DEVICE_ID + "/data";  // 发布打卡数据
  MQTT_TOPIC_SUB = "attendance/" + DEVICE_ID + "/cmd";   // 订阅小程序指令
}

/**
 * @brief 初始化MQTT-TLS连接（跳过证书验证，适配免费EMQX）
 */
void initMqtt() {
  espClient.setInsecure(); // 免费EMQX无需证书，跳过验证
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(handleMqttCmd); // 设置指令回调函数
}

/**
 * @brief MQTT断连重连+重新订阅主题
 */
void mqttReconnect() {
  Serial.print("连接EMQX...");
  while (!mqttClient.connected()) {
    if (mqttClient.connect(DEVICE_ID.c_str(), MQTT_USER, MQTT_PWD)) {
      Serial.println("成功");
      mqttClient.subscribe(MQTT_TOPIC_SUB.c_str());
      digitalWrite(LED_BUILTIN, LOW);
    } else {
      Serial.printf("失败，错误码：%d，5秒后重试\n", mqttClient.state());
      digitalWrite(LED_BUILTIN, HIGH);
      delay(5000);
    }
  }
}

/**
 * @brief 处理MQTT订阅指令（小程序→ESP→STM32）
 * @param topic 订阅主题
 * @param payload 指令内容
 * @param length 指令长度
 */
void handleMqttCmd(char* topic, byte* payload, unsigned int length) {
  char cmd[128] = {0};
  memcpy(cmd, payload, length);
  cmd[length] = '\0';
  Serial.printf("收到小程序指令：%s\n", cmd);
  blinkLED(2, 200); // LED闪2次：收到指令

  // 优先处理WiFi配置指令（setwifi|SSID|PWD 或 setwifi|SSID）
  if (strstr(cmd, "setwifi|") == cmd || strstr(cmd, "setWifi|") == cmd) {
    Serial.println("解析WiFi配置指令");
    char* firstPipe = strstr(cmd, "|");
    if (firstPipe == NULL) {
      Serial.println("WiFi指令格式错误：缺少SSID");
      return;
    }
    char* ssidStart = firstPipe + 1;
    char* secondPipe = strstr(ssidStart, "|");
    
    if (secondPipe == NULL) {
      // 开放WiFi（无密码）
      char newSsid[32] = {0};
      strncpy(newSsid, ssidStart, 31);
      Serial.printf("修改WiFi：SSID=%s（开放WiFi）\n", newSsid);
      reconnectWiFi(newSsid, "");
    } else {
      // 加密WiFi（有密码）
      char newSsid[32] = {0}, newPwd[32] = {0};
      strncpy(newSsid, ssidStart, secondPipe - ssidStart);
      strncpy(newPwd, secondPipe + 1, 31);
      Serial.printf("修改WiFi：SSID=%s，密码=%s\n", newSsid, newPwd);
      reconnectWiFi(newSsid, newPwd);
    }
    return; // 处理完毕，终止后续逻辑
  }

  // 解析其他指令（格式：指令码_参数1_参数2）
  char* cmdCode = strtok(cmd, "_");
  if (cmdCode == NULL) {
    Serial.println("指令格式错误");
    return;
  }
  char* param1 = strtok(NULL, "_");
  char* param2 = strtok(NULL, "_");

  // 同步时间指令：syncTime
  if (strcmp(cmdCode, "syncTime") == 0) {
    formatBeijingTime();
    sendFrame(TYPE_BJ_TIME, (uint8_t*)timeStr, strlen(timeStr));
    last_time_send = millis();
    String resp = "{\"status\":\"success\",\"msg\":\"时间同步成功\",\"time\":\"" + String(timeStr) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 通用远程打卡：checkIn
  if (strcmp(cmdCode, "checkIn") == 0) {
    char msg[16] = "remote_checkin";
    sendFrame(TYPE_REMOTE_CHECKIN, (uint8_t*)msg, strlen(msg));
    String resp = "{\"status\":\"success\",\"msg\":\"远程打卡指令触发\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 指定用户远程打卡：checkInUser_UID
  if (strcmp(cmdCode, "checkInUser") == 0 && param1 != NULL) {
    uint32_t userUid = atol(param1);
    uint8_t uidData[4] = {(userUid >> 24) & 0xFF, (userUid >> 16) & 0xFF, 
                          (userUid >> 8) & 0xFF, userUid & 0xFF};
    sendFrame(TYPE_REMOTE_CHECKIN, uidData, 4);
    String resp = "{\"status\":\"success\",\"msg\":\"指定用户打卡触发\",\"user_id\":" + String(userUid) + "}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 重启STM32：restartSTM32
  if (strcmp(cmdCode, "restartSTM32") == 0) {
    char restartMsg[16] = "stm32_restart";
    sendFrame(TYPE_RESTART_STM32, (uint8_t*)restartMsg, strlen(restartMsg));
    String resp = "{\"status\":\"success\",\"msg\":\"STM32重启指令发送\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 重启ESP网络：restartNet
  if (strcmp(cmdCode, "restartNet") == 0) {
    Serial.println("重启ESP网络");
    digitalWrite(LED_BUILTIN, HIGH);
    WiFi.disconnect();
    delay(1000);
    initWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      forceNtpSyncAndSend();
      mqttReconnect();
      digitalWrite(LED_BUILTIN, LOW);
      String resp = "{\"status\":\"success\",\"msg\":\"ESP网络重启成功\"}";
      mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    } else {
      String resp = "{\"status\":\"fail\",\"msg\":\"ESP网络重启失败\"}";
      mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    }
    return;
  }

  // 设置打卡时间：setWorkTime_09:00_18:00
  if (strcmp(cmdCode, "setWorkTime") == 0 && param1 != NULL && param2 != NULL) {
    strncpy(WORK_TIME, param1, 7);
    strncpy(OFF_TIME, param2, 7);
    // 保存到EEPROM
    eepromWriteStr(ADDR_WORK_TIME, WORK_TIME, 8);
    eepromWriteStr(ADDR_OFF_TIME, OFF_TIME, 8);
    // 下发STM32
    char workTimeData[40] = {0};
    sprintf(workTimeData, "%s|%s", param1, param2);
    sendFrame(TYPE_SET_WORK_TIME, (uint8_t*)workTimeData, strlen(workTimeData));
    String resp = "{\"status\":\"success\",\"msg\":\"打卡时间设置成功\",\"work_time\":\"" + String(WORK_TIME) + "\",\"off_work_time\":\"" + String(OFF_TIME) + "\"}";
    mqttClient.publish(MQTT_TOPIC_PUB.c_str(), resp.c_str());
    return;
  }

  // 兜底：新增用户指令（兼容原有逻辑）
  uint8_t cmd_data[128] = {0};
  memcpy(cmd_data, cmd, length);
  sendFrame(TYPE_ADD_USER, cmd_data, length);
  Serial.println("新增用户指令发送给STM32");
}

// ===================== 数据转换函数 =====================
/**
 * @brief 帧数据转JSON格式（适配后端解析）
 * @param type 帧类型
 * @param data 帧数据
 * @param len 数据长度
 * @return JSON字符串
 */
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

/**
 * @brief 打卡类型码转文字描述（与后端/小程序一致）
 * @param type 打卡类型码
 * @return 描述字符串
 */
String getCheckInTypeDesc(uint8_t type) {
  switch (type) {
    case 0x01: return "上班打卡";
    case 0x02: return "下班打卡";
    case 0x03: return "迟到打卡";
    case 0x04: return "提前打卡";
    default:   return "正常打卡";
  }
}

/**
 * @brief 清洗字符串（过滤不可见字符，防止JSON解析失败）
 * @param str 待清洗字符串
 * @param len 字符串长度
 */
void cleanString(char* str, uint8_t len) {
  if (str == NULL || len == 0) return;
  uint8_t j = 0;
  for (uint8_t i = 0; i < len; i++) {
    if (str[i] >= 0x20 && str[i] <= 0x7E) { // 仅保留可打印ASCII字符
      str[j++] = str[i];
    }
  }
  str[j] = '\0';
}

// ===================== 辅助函数 =====================
/**
 * @brief LED闪烁提示（状态可视化）
 * @param times 闪烁次数
 * @param delayMs 闪烁间隔（毫秒）
 */
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
