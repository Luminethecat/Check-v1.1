App({
  onLaunch() {
    // 读取本地登录状态，增加容错：默认空值/空对象
    const token = wx.getStorageSync('token') || '';
    const userInfo = wx.getStorageSync('userInfo') || {};
    const userType = wx.getStorageSync('userType') || ''; // admin/user

    // 校验登录状态：token和userType必须同时存在才视为已登录
    if (token && userType) {
      this.globalData.token = token;
      this.globalData.userInfo = typeof userInfo === 'object' ? userInfo : {};
      this.globalData.userType = userType;
      // 已登录直接跳首页（tabBar页面用switchTab，正确）
      wx.switchTab({ url: '/pages/index/index' });
    } else {
      // 未登录跳管理员登录页：用redirectTo，关闭当前启动页，无返回栈，优化体验
      wx.redirectTo({ url: '/pages/login/login' });
    }
  },

  // 全局数据：token/用户类型/用户信息，初始化默认值更规范
  globalData: {
    token: '',
    userType: '', // admin=管理员，user=普通员工
    userInfo: {},  // 管理员：{username} | 员工：{user_id, name, dept}
    deviceId: ''   // 【新增】初始化全局设备ID，和首页/设备页联动
  },

  // 退出登录：清除本地存储+全局数据+强制跳登录页，体验拉满
  logout() {
    // 清除本地所有登录相关缓存
    wx.removeStorageSync('token');
    wx.removeStorageSync('userInfo');
    wx.removeStorageSync('userType');
    wx.removeStorageSync('deviceId'); // 【新增】退出时清空设备ID缓存
    // 重置全局数据
    this.globalData.token = '';
    this.globalData.userType = '';
    this.globalData.userInfo = {};
    this.globalData.deviceId = ''; // 【新增】退出时重置全局设备ID
    // 强制重启并跳登录页：reLaunch关闭所有页面，避免残留状态，最稳妥
    wx.reLaunch({ url: '/pages/login/login' });
  }
});