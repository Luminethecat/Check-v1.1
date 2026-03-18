const request = require('../../utils/request.js');

Page({
  data: {
    deviceId: '', // 要修改的ESP设备ID
    wifiSsid: '', // 新WiFi账号
    wifiPwd: ''   // 新WiFi密码
  },

  // 监听设备ID输入
  onDeviceIdInput(e) {
    this.setData({ deviceId: e.detail.value.trim() });
  },

  // 监听WiFi账号输入
  onWifiSsidInput(e) {
    this.setData({ wifiSsid: e.detail.value.trim() });
  },

  // 监听WiFi密码输入
  onWifiPwdInput(e) {
    this.setData({ wifiPwd: e.detail.value.trim() });
  },

  // 提交WiFi配置并下发重连指令
  async submitWifiConfig() {
    const { deviceId, wifiSsid, wifiPwd } = this.data;
    // 基础校验
    if (!deviceId) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' });
      return;
    }
    if (!wifiSsid) {
      wx.showToast({ title: '请输入WiFi账号', icon: 'none' });
      return;
    }

    try {
      wx.showLoading({ title: '下发配置中...' });
      // 调用后端修改WiFi配置接口
      const res = await request.post('/api/device/setWifi', {
        deviceId,
        wifiSsid,
        wifiPwd
      });

      if (res.code === 200) {
        wx.hideLoading();
        wx.showToast({ title: res.msg, icon: 'success' });
        // 清空输入框
        this.setData({ deviceId: '', wifiSsid: '', wifiPwd: '' });
      } else {
        wx.hideLoading();
        wx.showToast({ title: res.msg || '下发配置失败', icon: 'none' });
      }
    } catch (err) {
      wx.hideLoading();
      wx.showToast({ title: '网络错误，下发失败', icon: 'none' });
      console.error('修改WiFi配置失败：', err);
    }
  }
});