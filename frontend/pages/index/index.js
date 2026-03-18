const app = getApp();
// pages/index/index.js
Page({
  data: {
    userType: '',
    userInfo: {},
    token: '',
    deviceId: '' // 新增：设备ID
  },

  onLoad(options) {
    this.getLoginInfo();
    // 加载缓存中的设备ID
    const cacheDeviceId = wx.getStorageSync('deviceId') || '';
    this.setData({
      deviceId: cacheDeviceId
    });
    // 【新增】同步设备ID到全局数据，供device页面读取
    app.globalData.deviceId = cacheDeviceId;
  },

  // 新增：设备ID输入监听
  onDeviceIdInput(e) {
    this.setData({ deviceId: e.detail.value.trim() });
  },

  // 新增：保存设备ID到缓存
  saveDeviceId() {
    const { deviceId } = this.data;
    if (!deviceId) {
      wx.showToast({ title: '请输入设备ID', icon: 'none' });
      return;
    }
    wx.setStorageSync('deviceId', deviceId);
    // 【新增】保存时同步更新全局设备ID
    app.globalData.deviceId = deviceId;
    wx.showToast({ title: '设备ID已保存', icon: 'success' });
  },

  // 获取登录信息并赋值给data
  getLoginInfo() {
    const token = wx.getStorageSync('token');
    const userType = wx.getStorageSync('userType');
    const userInfo = wx.getStorageSync('userInfo');
    if (token && userType && userInfo) {
      this.setData({
        token,
        userType,
        userInfo
      });
      // 同步到全局
      app.globalData.token = token;
      app.globalData.userType = userType;
      app.globalData.userInfo = userInfo;
    } else {
      // 未登录，跳回管理员登录页
      wx.redirectTo({ url: '/pages/login/login' });
    }
  },

  // 管理员-员工管理
  toEmp() {wx.navigateTo({ url: '/pages/emp/emp' });},
  // 管理员-设备指令
  toDevice() {wx.navigateTo({ url: '/pages/device/device' });},
  // 通用-打卡记录
  toAtt() {wx.navigateTo({ url: '/pages/att/att' });},
  // 管理员-报表导出
  toReport() {wx.navigateTo({ url: '/pages/report/report' });},
  // 员工-考勤统计
  toUserStart() {wx.navigateTo({ url: '/pages/userstart/userstart' });},
  // 管理员-修改密码
  toAdminChangePwd() {wx.navigateTo({ url: '/pages/adminChangePwd/adminChangePwd' });},
  // 员工-修改密码
  toUserChangePwd() {wx.navigateTo({ url: '/pages/userChangePwd/userChangePwd' });},
  toEmpAtt() {
    wx.navigateTo({ url: '/pages/empAtt/empAtt' });
  },
  // 退出登录
  logout() {
    wx.showModal({
      title: '确认退出',
      content: '是否退出当前账号？',
      success: (res) => {
        if (res.confirm) {
          // 清理缓存和全局数据
          wx.removeStorageSync('token');
          wx.removeStorageSync('userType');
          wx.removeStorageSync('userInfo');
          wx.removeStorageSync('deviceId'); // 【新增】退出时清空设备ID缓存
          app.globalData = { token: '', userType: '', userInfo: {}, deviceId: '' }; // 【修改】清空全局设备ID
          // 跳回管理员登录页
          wx.redirectTo({ url: '/pages/login/login' });
        }
      }
    });
  }
});