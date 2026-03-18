// pages/userlogin/userlogin.js
const request = require('../../utils/request.js');
const app = getApp();
Page({
  data: {
    user_id: '',
    password: ''
  },

  onUserIdChange(e) {
    this.setData({ user_id: e.detail.value.trim() });
  },
  onPasswordChange(e) {
    this.setData({ password: e.detail.value.trim() });
  },

  async login() {
    const { user_id, password } = this.data;
    if (!user_id || !password) {
      wx.showToast({ title: '请输入UID和密码', icon: 'none' });
      return;
    }
    try {
      wx.showLoading({ title: '登录中...' });
      const res = await request.post('/api/user/login', { user_id, password });
      if (res.code === 200) {
        // 缓存登录信息
        wx.setStorageSync('token', res.data.token);
        wx.setStorageSync('userType', 'user');
        wx.setStorageSync('userInfo', res.data.userInfo);
        // 同步全局
        app.globalData.token = res.data.token;
        app.globalData.userType = 'user';
        app.globalData.userInfo = res.data.userInfo;
        wx.showToast({ title: `欢迎你，${res.data.userInfo.name}`, icon: 'success' });
        // 跳转首页
        setTimeout(() => {
          wx.redirectTo({ url: '/pages/index/index' });
        }, 1000);
      } else {
        wx.showToast({ title: res.msg, icon: 'none' });
      }
    } catch (err) {
      wx.showToast({ title: '网络异常', icon: 'none' });
      console.log('登录失败：', err);
    }
  },

  // 跳管理员登录页
  toAdminLogin() {
    wx.navigateBack();
  }
});