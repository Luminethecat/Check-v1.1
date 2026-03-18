# backend-server后端服务说明
## 项目简介
本项目是考勤打卡机系统的 Node.js 后端服务，基于 Express 框架开发，通过 Supabase Edge Function 完成数据交互，支持 MQTT 设备通信、JWT 身份认证、Excel 报表导出等核心功能；
本地通过 VSCode 运行，借助 Cpolar 隧道实现内网穿透，让公网设备（如打卡机硬件）能访问本地后端服务。
## 快速启动
### 1. 环境准备
- Node.js 版本：v16+
- VS Code（安装 JavaScript/Node.js 插件）
- Cpolar 客户端（已安装并登录）
- 依赖包：express、cors、jsonwebtoken、xlsx、mqtt
### 2. 安装依赖
在 VS Code 终端（项目根目录）执行：
```bash
运行
npm install express cors jsonwebtoken xlsx mqtt
```
### 3. 启动本地后端服务
1. 在 VS Code 中打开 app.js 文件
2. 点击右上角「运行」→「运行文件」（或终端执行）：
```bash
运行
node app.js
```
3. 验证本地启动成功：访问 http://localhost:3000;能看到前端页面 / 接口响应即正常

### 4. 启动 Cpolar 内网穿透
1. 打开 Cpolar 客户端（或终端执行）：
2. 复制 Cpolar 生成的公网地址（如 https://xxxx.cpolar.top），这是公网访问后端的唯一入口
### 5. 访问方式
- 本地调试：http://localhost:3000
- 公网访问（打卡机 / 远程设备）：https://xxxx.cpolar.top（Cpolar 生成的隧道地址）
⚠️ 注意：Cpolar 免费版隧道地址重启后会变化，需同步更新打卡机 / 前端的接口地址；⚠️ 确保 VS Code 运行的后端服务和 Cpolar 隧道同时在线。
## 核心配置
### 1. Supabase Edge Function 配置
在 app.js 中修改为你的 Supabase 项目信息：
```javascript
运行
const SUPABASE_EDGE_URL = '你的Supabase Edge Function地址';
const SUPABASE_TOKEN = '你的Supabase服务角色密钥';
```
### 2. MQTT 配置（适配 Cpolar 穿透）
```javascript
运行
// EMQX 服务器地址（若MQTT也部署在本地，需给MQTT也开Cpolar隧道）
const EMQX_BROKER = '你的MQTT地址'; 
const EMQX_USER = 'MQTT用户名';
const EMQX_PWD = 'MQTT密码';

```
### 3. JWT 密钥
```javascript
运行
const SECRET = '你的JWT签名密钥';
```
## 核心功能模块
### 1. 身份认证
- 管理员登录：POST /api/login
- 员工登录：POST /api/user/login
- 管理员改密：POST /api/admin/changePwd
- 员工改密：POST /api/user/changePwd
### 2. 员工管理
- 查询员工列表：GET /api/emp/list
- 新增 / 编辑 / 删除员工：POST/GET /api/emp/xxx
- 重置员工密码：POST /api/emp/resetPwd
### 3. 打卡记录
- 员工个人打卡记录：GET /api/user/att/list
- 管理员全量打卡记录（分页）：GET /api/att/list
- 远程打卡 / 设备指令：POST /api/device/sendCmd
### 4. 考勤统计
- 员工年度统计：GET /api/user/att/stat
- 全员工统计：GET /api/att/getEmpAtt
- Excel 报表导出：/api/att/export
## 接口响应格式
所有接口统一返回 JSON 格式：
```json
{
  "code": 200,
  "msg": "操作成功",
  "data": {}
}
```
- code：200 = 成功，400 = 参数错误，401 = 未登录，403 = 无权限，500 = 服务器错误
- msg：操作结果描述
- data：业务数据（可选）
## 目录结构
```plaintext
backend-server/
├── app.js          # 后端主服务文件（VS Code 直接运行）
├── package.json    # 依赖配置
└── README.md       # 本说明文档
```
## Cpolar 内网穿透注意事项
1. 端口一致性：Cpolar 映射的端口（3000）必须和 app.js 中监听的端口一致；
2. 服务保活：VS Code 关闭 / 终端退出会导致后端服务停止，需保持 VS Code 窗口和终端在线；
3. 地址同步：Cpolar 隧道地址变化后，需同步修改：
打卡机硬件的后端接口地址；
前端页面的接口请求地址；
app.js 中 BACKEND_PUBLIC_URL 配置；
4. 防火墙放行：确保本地电脑 3000 端口未被防火墙拦截，Cpolar 客户端能正常联网。
## 常见问题
1. Cpolar 隧道启动失败：
检查 3000 端口是否被占用（终端执行 netstat -ano | findstr 3000）；
确认 Cpolar 已登录，且本地网络能访问 Cpolar 服务器。
2. 公网无法访问接口：
验证本地 localhost:3000 能正常访问；
检查 Cpolar 隧道状态是否为「在线」；
替换接口地址为 Cpolar 的 https 地址（避免 http 被浏览器拦截）。
3. 打卡机数据无法上报：
确认打卡机配置的后端地址是 Cpolar 公网地址；
检查 MQTT 服务器是否也做了内网穿透（若 MQTT 部署在本地）。
### 总结
1.核心流程：VS Code 运行后端 → Cpolar 映射 3000 端口 → 公网设备访问 Cpolar 地址；
2.关键要点：端口一致、服务保活、地址同步；
3.故障排查优先检查：后端是否启动、Cpolar 隧道是否在线、地址是否匹配。
