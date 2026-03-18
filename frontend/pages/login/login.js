// pages/login/login.js
const request = require('../../utils/request');
const app = getApp();

Page({
  data: {
    username: 'admin', // 账号初始值，输入框自动显示
    password: '',      // 密码初始值
    isClick: false,    // 员工登录入口防重标记
    loginClick: false  // 登录按钮专属防重标记
  },

  // 账号输入框内容变化监听
  onUsernameChange(e) {
    this.setData({ username: e.detail.value });
  },

  // 密码输入框内容变化监听
  onPasswordChange(e) {
    this.setData({ password: e.detail.value });
  },

  // 管理员登录核心方法
  async login() {
    // 防重点击拦截：已点击则直接返回
    if (this.data.loginClick) return;
    this.setData({ loginClick: true });

    const { username, password } = this.data;
    // 表单非空验证
    if (!username || !password) {
      wx.showToast({ title: '请输入账号和密码', icon: 'none' });
      this.setData({ loginClick: false });
      return;
    }

    try {
      // 调用后端登录接口，request内部已处理loading/异常
      const res = await request.post('/api/login', { username, password });
      if (res.code === 200) {
        // 存储Token/用户信息到本地缓存
        wx.setStorageSync('token', res.data.token);
        wx.setStorageSync('userType', 'admin');
        wx.setStorageSync('userInfo', { username });
        // 赋值全局数据，供其他页面使用
        app.globalData.token = res.data.token;
        app.globalData.userType = 'admin';
        app.globalData.userInfo = { username };
        // 登录成功提示+延迟跳转首页
        wx.showToast({ title: '登录成功', icon: 'success' });
        setTimeout(() => {
          wx.redirectTo({ url: '/pages/index/index' });
        }, 1000);
      } else {
        // 后端业务错误提示（如账号密码错误）
        wx.showToast({ title: res.msg || '登录失败', icon: 'none' });
      }
    } catch (err) {
      // 网络/接口异常兜底（request内部已做基础提示）
      console.error('【登录请求异常】', err);
    } finally {
      // 无论成功/失败，重置登录按钮防重标记
      this.setData({ loginClick: false });
    }
  },

  // 跳转到员工登录页
  toUserLogin() {
    if (this.data.isClick) return;
    this.setData({ isClick: true });
    wx.navigateTo({
      url: '/pages/userlogin/userlogin',
      // 任何情况都重置防重标记，避免按钮锁死
      success: () => { this.setData({ isClick: false }); },
      fail: () => { this.setData({ isClick: false }); },
      complete: () => { this.setData({ isClick: false }); }
    });
  }
});