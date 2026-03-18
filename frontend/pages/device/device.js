const app = getApp();
const request = require('../../utils/request.js'); // 复用你原有请求工具

Page({
  data: {
    deviceId: '', // 从全局+缓存读取
    // 指令列表：按你的需求排序，包含所有功能
    cmdList: [
      { name: '同步设备时间', code: 'syncTime', needParam: false },
      { name: '指定员工ID打卡', code: 'checkInUser', needParam: true },
      { name: '设置打卡时间', code: 'setWorkTime', needParam: true },
      { name: '修改WiFi配置', code: 'setWifi', needParam: true },
      { name: '重启WIFI网络', code: 'restartNet', needParam: false },
      { name: '重启设备', code: 'restartSTM32', needParam: false }

    ],
    selectedCmd: {}, // 选中的指令
    // 指令参数（默认值合理，减少输入）
    userId: '',
    workTime: '09:00',
    offWorkTime: '18:00',
    wifiSsid: '',
    wifiPwd: '',
    isBtnDisabled: true // 按钮智能禁用
  },

  onLoad() {
    // 双重读取：全局优先，缓存兜底，确保能拿到设备ID
    const globalId = app.globalData.deviceId || '';
    const cacheId = wx.getStorageSync('deviceId') || '';
    this.setData({
      deviceId: globalId || cacheId
    });
    // 初始化检查按钮状态
    this.checkBtnStatus();
  },

  // 选择指令：重置参数+更新按钮状态
  onCmdChange(e) {
    const idx = e.detail.value;
    const cmd = this.data.cmdList[idx];
    // 重置所有参数，避免残留
    this.setData({
      selectedCmd: cmd,
      userId: '',
      wifiSsid: '',
      wifiPwd: '',
      workTime: '09:00',
      offWorkTime: '18:00'
    });
    // 检查按钮是否可点击
    this.checkBtnStatus();
  },

  // 统一参数输入监听：通过data-key区分参数
  onParamInput(e) {
    const { key } = e.currentTarget.dataset;
    this.setData({
      [key]: e.detail.value.trim()
    });
    // 输入后实时检查按钮状态
    this.checkBtnStatus();
  },

  // 按钮禁用逻辑：设备ID存在+指令选中+参数齐全
  checkBtnStatus() {
    const { deviceId, selectedCmd, userId, workTime, offWorkTime, wifiSsid } = this.data;
    // 基础条件：有设备ID + 选中了指令
    if (!deviceId || !selectedCmd.code) {
      this.setData({ isBtnDisabled: true });
      return;
    }
    // 无参数指令：直接启用
    if (!selectedCmd.needParam) {
      this.setData({ isBtnDisabled: false });
      return;
    }
    // 有参数指令：校验对应参数
    let paramOk = false;
    switch (selectedCmd.code) {
      case 'checkInUser': paramOk = !!userId; break;
      case 'setWorkTime': paramOk = !!workTime && !!offWorkTime; break;
      case 'setWifi': paramOk = !!wifiSsid; break; // 密码可选
      default: paramOk = false;
    }
    this.setData({ isBtnDisabled: !paramOk });
  },

  // 发送指令：拼接ESP识别的指令格式，调用后端接口
  async sendCmd() {
    const { deviceId, selectedCmd, userId, workTime, offWorkTime, wifiSsid, wifiPwd } = this.data;
    // 拼接指令（和ESP端指令格式完全匹配，使用|作为分隔符）
    let finalCmd = selectedCmd.code;
    switch (selectedCmd.code) {
      case 'checkInUser': finalCmd = `checkInUser_${userId}`; break;
      case 'setWorkTime': finalCmd = `setWorkTime_${workTime}_${offWorkTime}`; break;
      case 'setWifi': finalCmd = `setWifi|${wifiSsid}|${wifiPwd || ''}`; break; // 关键修改：_ 改为 |
    }

    try {
      wx.showLoading({ title: '发送中...' });
      // 调用你原有设备指令接口，无需修改后端
      const res = await request.post('/api/device/sendCmd', {
        deviceId,
        cmd: finalCmd
      });
      wx.hideLoading();
      wx.showToast({ title:'指令发送成功', icon: 'success' });
      // 重置指令和参数，方便下次操作
      this.setData({ selectedCmd: {}, userId: '', wifiSsid: '', wifiPwd: '' });
    } catch (err) {
      wx.hideLoading();
      wx.showToast({ title: '发送失败', icon: 'none' });
      console.error('指令发送失败：', err);
    }
  }
});