// app.js - 考勤系统服务端（适配Supabase Edge + Codespaces公网访问）
const express = require('express');
const cors = require('cors');
const jwt = require('jsonwebtoken');
const xlsx = require('xlsx');
const mqtt = require('mqtt');

const app = express();
const PORT = process.env.PORT || 3000;
const SECRET = 'attendance_bishe_2025';
const CODESPACE_NAME = process.env.CODESPACE_NAME;
const CODESPACES_URL = CODESPACE_NAME ? `https://${CODESPACE_NAME}-${PORT}.app.github.dev` : `http://localhost:${PORT}`;

// 配置项
const SUPABASE_EDGE_URL = 'https://ufdwkrexvfceztrnuvrm.supabase.co/functions/v1/bright-handler';
const SUPABASE_TOKEN = 'eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InVmZHdrcmV4dmZjZXp0cm51dnJtIiwicm9sZSI6InNlcnZpY2Vfcm9sZSIsImlhdCI6MTc3MDAzNTczNiwiZXhwIjoyMDg1NjExNzM2fQ.cm34oWvD0Oc8KP83uDE53R5Xcmcd34y4Xb-VzcvKz4g';

const EMQX_BROKER = 'mqtts://mb300ee7.ala.cn-hangzhou.emqxsl.cn:8883';
const EMQX_USER = 'user';
const EMQX_PWD = '123456';
const EMQX_TOPIC_PREFIX = 'attendance/';
let mqttClient = null;

// 基础中间件
app.use(express.json({ limit: '10mb' }));
app.use(express.urlencoded({ extended: true }));
app.use(cors({ origin: '*' }));
app.use(express.static(__dirname));

// 请求转发工具函数
const supabaseRequest = async (action, data = {}) => {
  try {
    const response = await fetch(SUPABASE_EDGE_URL, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
        'Authorization': `Bearer ${SUPABASE_TOKEN}`
      },
      body: JSON.stringify({ action, ...data }),
      timeout: 30000
    });
    if (!response.ok) throw new Error(`Supabase响应失败：${response.status} - ${await response.text()}`);
    return await response.json();
  } catch (err) {
    console.error(`[${action}] Supabase转发失败：`, err.message);
    return { code: 500, msg: '服务器数据交互失败', error: err.message };
  }
};

// 鉴权中间件
const auth = (req, res, next) => {
  let token = req.headers.token || '';
  const authHeader = req.headers.authorization || '';
  if (authHeader.startsWith('Bearer ')) {
    token = authHeader.slice(7);
  }
  if (!token) return res.json({ code: 401, msg: '未登录，请先登录' });
  try {
    const payload = jwt.verify(token, SECRET, { expiresIn: '7d' });
    req.userInfo = payload;
    next();
  } catch (err) {
    return res.json({ code: 401, msg: '登录过期，请重新登录' });
  }
};

const adminAuth = (req, res, next) => {
  if (req.userInfo.type !== 'admin') return res.json({ code: 403, msg: '无管理员权限' });
  next();
};

const userAuth = (req, res, next) => {
  if (req.userInfo.type !== 'user') return res.json({ code: 403, msg: '无员工权限' });
  next();
};

// 接口：管理员登录
app.post('/api/login', async (req, res) => {
  const { username, password } = req.body || {};
  if (!username || !password) return res.json({ code: 400, msg: '请输入账号和密码' });
  const result = await supabaseRequest('admin_login', { username, password });
  if (result.code === 200) {
    const token = jwt.sign({ type: 'admin', username }, SECRET, { expiresIn: '7d' });
    res.json({ code: 200, msg: '管理员登录成功', data: { token } });
  } else {
    res.json(result);
  }
});

// 接口：员工登录
app.post('/api/user/login', async (req, res) => {
  const { user_id, password } = req.body || {};
  if (!user_id || !password) return res.json({ code: 400, msg: '请输入员工UID和密码' });
  const result = await supabaseRequest('user_login', { user_id, password });
  if (result.code === 200) {
    const { userInfo } = result.data;
    const token = jwt.sign({ 
      type: 'user', 
      user_id: userInfo.user_id,
      name: userInfo.name 
    }, SECRET, { expiresIn: '7d' });
    res.json({ code: 200, msg: '员工登录成功', data: { token, userInfo } });
  } else {
    res.json(result);
  }
});

// 员工管理模块
app.get('/api/emp/list', auth, adminAuth, async (req, res) => {
  const result = await supabaseRequest('emp_list');
  res.json(result);
});

app.post('/api/emp/add/?', auth, adminAuth, async (req, res) => {
  const params = req.body || {};
  if (!params.name || !params.user_id) return res.json({ code: 400, msg: '请输入姓名和员工UID' });
  const result = await supabaseRequest('emp_add', params);
  res.json(result);
});

app.post('/api/emp/edit', auth, adminAuth, async (req, res) => {
  const params = req.body || {};
  if (!params.name || !params.user_id || !params.id) return res.json({ code: 400, msg: '请输入员工ID、姓名和UID' });
  const result = await supabaseRequest('emp_update', params);
  res.json(result);
});

app.post('/api/emp/save', auth, adminAuth, async (req, res) => {
  const params = req.body || {};
  if (!params.name || !params.user_id) return res.json({ code: 400, msg: '请输入姓名和员工UID' });
  const result = await supabaseRequest(params.id ? 'emp_update' : 'emp_add', params);
  res.json(result);
});

app.get('/api/emp/delete/:id', auth, adminAuth, async (req, res) => {
  const { id } = req.params;
  if (!id) return res.json({ code: 400, msg: '请传入员工ID' });
  const result = await supabaseRequest('emp_delete', { id });
  res.json(result);
});

app.post('/api/emp/resetPwd', auth, adminAuth, async (req, res) => {
  const { id } = req.body || {};
  if (!id) return res.json({ code: 400, msg: '请传入员工ID' });
  const result = await supabaseRequest('emp_reset_pwd', { id, defaultPwd: '123456' });
  res.json(result);
});

// 打卡记录模块
app.get('/api/user/att/list', auth, userAuth, async (req, res) => {
  const { startTime, endTime } = req.query;
  const user_id = req.userInfo.user_id;
  
  const now = new Date();
  const defaultStartTime = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000).toISOString().slice(0, 10);
  const defaultEndTime = now.toISOString().slice(0, 10);
  const finalStartTime = startTime || defaultStartTime;
  const finalEndTime = endTime || defaultEndTime;

  console.log('员工打卡查询：', user_id, finalStartTime, finalEndTime);

  try {
    const result = await supabaseRequest('att_user_list', {
      user_id,
      startTime: finalStartTime,
      endTime: finalEndTime
    });
    console.log('打卡查询结果：', result);
    res.json(result);
  } catch (err) {
    console.log('查询失败：', err.message);
    res.json({ code: 500, msg: '查询个人打卡记录失败' });
  }
});

app.get('/api/att/list', auth, adminAuth, async (req, res) => {
  const params = { 
    ...req.query, 
    page: parseInt(req.query.page) || 1, 
    size: parseInt(req.query.size) || 20 
  };
  const result = await supabaseRequest('att_admin_list', params);
  res.json(result);
});

app.post('/api/att/delete', auth, adminAuth, async (req, res) => {
  const { id } = req.body || {};
  if (!id) return res.json({ code: 400, msg: '请传入打卡记录ID' });
  const result = await supabaseRequest('att_delete', { id });
  res.json(result);
});

app.post('/api/device/sendCmd', auth, adminAuth, async (req, res) => {
  const { deviceId, cmd } = req.body || {};
  if (!deviceId || !cmd) return res.json({ code: 400, msg: '设备ID/指令不能为空' });
  if (!mqttClient || !mqttClient.connected) return res.json({ code: 500, msg: 'MQTT未连接' });

  const mqttTopic = `${EMQX_TOPIC_PREFIX}${deviceId}/cmd`;
  mqttClient.publish(mqttTopic, cmd, async (err) => {
    if (err) return res.json({ code: 500, msg: '指令发送失败：' + err.message });
    if (cmd.startsWith('checkInUser_')) {
      const user_id = cmd.split('_')[1];
      if (!user_id) return res.json({ code: 200, msg: '指令发送成功，无员工UID' });
      
      const now = new Date();
      const beijingTime = new Date(now.getTime() + 8 * 60 * 60 * 1000);
      const check_time = beijingTime.toISOString().slice(0, 10) + ' ' + beijingTime.toISOString().slice(11, 19);
      
      await supabaseRequest('att_add', {
        device_id: deviceId, user_id, check_time, check_type_code: 1, check_type_desc: '远程打卡'
      });
      res.json({ code: 200, msg: `指令发送成功，远程打卡记录已生成` });
    } else {
      res.json({ code: 200, msg: `指令【${cmd}】发送成功` });
    }
  });
});

// 密码修改模块
app.post('/api/admin/changePwd', auth, adminAuth, async (req, res) => {
  const { oldPwd, newPwd } = req.body || {};
  if (!oldPwd || !newPwd) return res.json({ code: 400, msg: '请输入原密码和新密码' });
  const result = await supabaseRequest('admin_change_pwd', {
    username: req.userInfo.username, oldPwd, newPwd
  });
  res.json(result);
});

app.post('/api/user/changePwd', auth, userAuth, async (req, res) => {
  const { oldPwd, newPwd } = req.body || {};
  if (!oldPwd || !newPwd) return res.json({ code: 400, msg: '请输入原密码和新密码' });
  const result = await supabaseRequest('user_change_pwd', {
    user_id: req.userInfo.user_id, oldPwd, newPwd
  });
  res.json(result);
});

// 考勤统计模块
app.get('/api/user/att/stat', auth, userAuth, async (req, res) => {
  const { year = new Date().getFullYear().toString() } = req.query;
  console.log('员工考勤统计：', req.userInfo);
  const result = await supabaseRequest('att_user_stat', { 
    user_id: req.userInfo.user_id,
    year 
  });
  res.json(result);
});

app.get('/api/att/getEmpAtt/?', auth, adminAuth, async (req, res) => {
  const { start, end } = req.query;
  console.log('管理员考勤查询：', req.userInfo, start, end);

  if (!start || !end) {
    return res.json({ code: 400, msg: '请选择完整的查询时间' });
  }
  if (new Date(start) > new Date(end)) {
    return res.json({ code: 400, msg: '开始日期不能晚于结束日期' });
  }

  try {
    const result = await supabaseRequest('att_admin_stat', { start, end });
    console.log('考勤统计结果：', result);
    res.json(result);
  } catch (err) {
    console.log('统计异常：', err.message);
    res.json({ code: 500, msg: '服务器数据交互失败', error: err.message });
  }
});

app.get('/api/att/export', auth, adminAuth, async (req, res) => {
  const { start, end } = req.query;
  if (!start || !end) return res.json({ code: 400, msg: '请选择导出时间范围' });

  try {
    const result = await supabaseRequest('att_admin_stat', { start, end });
    if (result.code !== 200 || !Array.isArray(result.data) || result.data.length === 0) {
      return res.json({ code: 400, msg: '无考勤数据，无法导出' });
    }
    const attData = result.data;

    const headerMap = {
      user_id: '员工ID',
      name: '员工姓名',
      dept: '所属部门',
      totalDays: '总打卡天数',
      normalDays: '正常打卡天数',
      lateDays: '迟到天数',
      earlyDays: '早退天数'
    };
    const enKeys = Object.keys(headerMap);
    const cnTitles = Object.values(headerMap);

    const handleData = attData.map(item => {
      const newItem = {};
      enKeys.forEach(key => {
        newItem[key] = item[key] || '';
      });
      return newItem;
    });

    const ws = xlsx.utils.json_to_sheet(handleData, {
      header: enKeys,
      skipHeader: true,
      origin: 'A1'
    });
    
    cnTitles.forEach((title, index) => {
      const cellAddr = xlsx.utils.encode_cell({ r: 0, c: index });
      ws[cellAddr] = { v: title, t: 's' };
    });

    const wb = xlsx.utils.book_new();
    xlsx.utils.book_append_sheet(wb, ws, '员工考勤报表');
    const excelBase64 = xlsx.write(wb, { type: 'base64', bookType: 'xlsx' });

    res.json({
      code: 200,
      msg: '报表生成成功',
      data: {
        base64: excelBase64,
        fileName: `员工考勤报表_${start}_${end}.xlsx`
      }
    });

  } catch (err) {
    console.error('Excel生成失败：', err.message);
    res.json({ code: 500, msg: '报表生成失败', error: err.message });
  }
});

// 设备配置
app.post('/api/device/setWifi', auth, adminAuth, (req, res) => {
  const { deviceId, wifiSsid, wifiPwd = '' } = req.body || {};
  if (!deviceId || !wifiSsid) return res.json({ code: 400, msg: '设备ID/WiFi SSID不能为空' });
  if (!mqttClient || !mqttClient.connected) return res.json({ code: 500, msg: 'MQTT未连接' });
  const mqttCmd = `setwifi|${wifiSsid}|${wifiPwd}`;
  const mqttTopic = `${EMQX_TOPIC_PREFIX}${deviceId}/cmd`;
  
  mqttClient.publish(mqttTopic, mqttCmd, (err) => {
    if (err) return res.json({ code: 500, msg: 'WiFi配置下发失败：' + err.message });
    res.json({ code: 200, msg: `WiFi配置下发成功！设备${deviceId}将自动重连` });
  });
});

// MQTT初始化
const initMqtt = () => {
  mqttClient = mqtt.connect(EMQX_BROKER, {
    username: EMQX_USER,
    password: EMQX_PWD,
    rejectUnauthorized: false,
    clientId: `bishe_${Date.now()}_${Math.random().toString(36).slice(2)}`,
    keepalive: 60
  });
  
  mqttClient.on('connect', () => {
    console.log('MQTT连接EMQX成功！');
    mqttClient.subscribe(`${EMQX_TOPIC_PREFIX}#`, (err) => {
      if (!err) console.log(`订阅MQTT主题：${EMQX_TOPIC_PREFIX}#`);
    });
  });
  
  mqttClient.on('message', async (topic, payload) => {
    try {
      if (!topic.endsWith('/data')) return;
      const data = JSON.parse(payload.toString());
      if (data.frame_type !== 2 || !data.device_id || !data.user_id || !data.check_time) return;
      
      await supabaseRequest('att_add', {
        device_id: data.device_id,
        user_id: data.user_id,
        check_time: data.check_time.replace(/\//g, '-'),
        check_type_code: data.check_type_code || 0,
        check_type_desc: data.check_type_desc || '正常打卡'
      });
      console.log(`MQTT消息入库成功：${data.device_id} - ${data.user_id}`);
    } catch (err) {
      console.log('MQTT消息解析失败：', err.message);
    }
  });
  
  mqttClient.on('error', (err) => { 
    console.log('MQTT错误：', err.message); 
    setTimeout(initMqtt, 5000); 
  });
  mqttClient.on('close', () => { 
    console.log('MQTT关闭，5秒后重连...'); 
    setTimeout(initMqtt, 5000); 
  });
  mqttClient.on('offline', () => { 
    console.log('MQTT离线，5秒后重连...'); 
    setTimeout(initMqtt, 5000); 
  });
};

setTimeout(initMqtt, 2000);

// 启动服务
app.listen(PORT, '0.0.0.0', () => {
  console.log('=============================================');
  console.log(`考勤系统启动成功！`);
  console.log(`监听端口：${PORT}`);
  console.log(`本地地址：http://localhost:${PORT}`);
  console.log(`Codespaces公网地址：${CODESPACES_URL}`);
  console.log('=============================================');
});
