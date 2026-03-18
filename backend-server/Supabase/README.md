# Supabase Edge Function 说明文档

## 功能概述
该 Edge Function 是考勤打卡机系统的核心数据处理层，基于 Supabase Deno Runtime 开发，提供管理员/员工认证、员工管理、打卡记录 CRUD、考勤统计等全量数据接口，适配北京时间处理，解决跨时区考勤统计误差问题。

## 部署步骤

### 1. 准备工作
- 确保已创建 Supabase 项目，获取项目 `SUPABASE_URL` 和 `SUPABASE_ANON_KEY`
- 执行配套 SQL 脚本创建所需数据表（admin/employee/attendance_rule/attendance）
- 安装 Supabase CLI：
  ```bash
  npm install -g supabase
    ```
### 2. 创建 Edge Function
  ```bash
运行
# 登录Supabase
supabase login

# 初始化项目
 supabase init

# 创建名为attendance-api的Edge Function
supabase functions new attendance-api
  ```

### 3. 替换代码
将 Edge Function 代码复制到supabase/functions/attendance-api/index.ts文件中，替换默认内容。
### 4. 修改配置
在代码开头替换为自己的 Supabase 项目信息：
  ```javascript
运行
const SUPABASE_URL = '你的Supabase项目URL';
const SUPABASE_ANON_KEY = '你的Supabase匿名密钥';
  ```
### 5. 部署函数
  ```bash
运行
# 部署到Supabase-分别将SQL和EdgaFunctions代码写入
supabase functions deploy attendance-api --no-verify-jwt
  ```
## 核心接口说明
表格
| 接口Action | 请求参数 | 功能说明 | 权限 |
|-----------|----------|----------|------|
| admin_login | username, password | 管理员登录验证 | 公开 |
| user_login | user_id, password | 员工登录验证 | 公开 |
| admin_change_pwd | username, oldPwd, newPwd | 管理员修改密码 | 管理员 |
| user_change_pwd | user_id, oldPwd, newPwd | 员工修改密码 | 员工 |
| emp_list | - | 查询所有员工列表 | 管理员 |
| emp_add | user_id, name, dept, password | 新增员工 | 管理员 |
| emp_update | id, user_id, name, dept | 修改员工信息 | 管理员 |
| emp_delete | id | 删除员工 | 管理员 |
| emp_reset_pwd | id, defaultPwd | 重置员工密码 | 管理员 |
| att_user_list | user_id, startTime, endTime | 查询员工个人打卡记录 | 员工 |
| att_admin_list | page, size, user_id, startTime, endTime | 分页查询全量打卡记录 | 管理员 |
| att_add | device_id, user_id, check_time, check_type_code, check_type_desc | 新增打卡记录 | 公开 |
| att_delete | id | 删除打卡记录 | 管理员 |
| att_user_stat | user_id, year | 员工年度考勤统计 | 员工 |
| att_admin_stat | start, end | 全员工考勤统计 | 管理员 |

## 数据格式要求
### 请求格式（POST）
 ```json
{
  "action": "接口Action名称",
  "params1": "参数值1",
  "params2": "参数值2"
}
 ```
### 响应格式
 ```json
{
  "code": 200, // 200成功/400参数错误/401权限错误/500服务器错误
  "msg": "操作结果说明",
  "data": {} // 业务数据
}
 ```
## 关键注意事项
- 时间处理：所有时间参数需传入YYYY-MM-DD HH:mm:ss格式，函数内部自动转换为北京时间存储
- 权限控制：部署时需添加 JWT 验证（生产环境），当前为测试模式（--no-verify-jwt）
- 依赖说明：仅依赖@supabase/supabase-js@2，无需额外安装其他依赖
- 错误处理：所有接口均包含参数校验和异常捕获，关键错误会输出日志到 Supabase 控制台
- 索引建议：为 attendance 表的 user_id 和 check_time 字段创建索引，提升查询效率：
 ```sql
CREATE INDEX idx_attendance_user_id ON attendance(user_id);
CREATE INDEX idx_attendance_check_time ON attendance(check_time);
```
## 常用调试命令
```bash
运行
# 查看函数日志
supabase functions logs attendance-api --follow

# 本地调用测试
curl -X POST 'http://localhost:54321/functions/v1/attendance-api' \
  -H 'Content-Type: application/json' \
  -d '{"action":"admin_login","username":"admin","password":"123456"}'
```
## 配套 SQL 说明
执行提供的 SQL 脚本可自动创建：
- 管理员表（默认账号：admin/123456）
- 员工表（测试员工：1001/123456）
- 考勤规则表（默认上班 09:00，下班 18:00）
- 打卡记录表（含唯一约束避免重复打卡）
- 考勤统计存储过程（辅助数据分析）
