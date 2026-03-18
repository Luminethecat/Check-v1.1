const app = getApp();
const request = require('../../utils/request.js');

Page({
  data: {
    startTime: '',
    endTime: '',
    attList: [],
    userType: '',
    userInfo: {}
  },

  onLoad(options) {
    const today = new Date().toISOString().split('T')[0];
    this.setData({
      startTime: today,
      endTime: today,
      userType: app.globalData.userType || '',
      userInfo: app.globalData.userInfo || {}
    });
  },

  onShow() {
    this.setData({
      userType: app.globalData.userType || '',
      userInfo: app.globalData.userInfo || {}
    });
  },

  onStartChange(e) {
    this.setData({ startTime: e.detail.value });
  },

  onEndChange(e) {
    this.setData({ endTime: e.detail.value });
  },

  async getAttList() {
    try {
      const { startTime, endTime, userType, userInfo } = this.data;

      // ========== 新增：日期校验（解决开始>结束问题） ==========
      const startDate = new Date(startTime);
      const endDate = new Date(endTime);
      if (startDate > endDate) {
        wx.showToast({ title: '开始日期不能晚于结束日期', icon: 'none' });
        return; // 阻止后续查询
      }

      wx.showLoading({ title: '查询中...' });
      let res = { code: 0, data: { list: [] } };
      let attList = [];

      if (userType === 'admin') {
        res = await request.get('/api/att/list', { 
          startTime, 
          endTime,
          page: 1,
          size: 50 
        });
        if (res.code === 200) {
          attList = (res.data.list || []).map(item => ({
            ...item,
            // 管理员端处理check_time：去掉T、删除时区后缀，和员工端格式统一
            check_time: item.check_time.replace('T', ' ').split('+')[0]
          }));
        }
      } 
      // ========== 修改：员工视角处理日期格式（去掉T） ==========
      else if (userType === 'user' && userInfo.user_id) {
        res = await request.get('/api/att/user/list', { 
          user_id: userInfo.user_id,
          startTime, 
          endTime 
        });
        if (res.code === 200) {
          // 格式化日期：把 "2026-02-03T18:14:00+00:00" 转成 "2026-02-03 18:14:00"
          attList = (res.data || []).map(item => ({
            ...item,
            date: item.date.replace('T', ' ').split('+')[0] // 替换T为空格，去掉时区
          }));
        }
      } 
      else {
        wx.showToast({ title: '请先登录', icon: 'none' });
        wx.hideLoading();
        return;
      }

      this.setData({ attList });
      if (attList.length === 0) {
        wx.showToast({ title: '暂无打卡记录', icon: 'none' });
      }
      if (res.code !== 200) {
        wx.showToast({ title: res.msg || '查询失败', icon: 'none' });
      }
    } catch (err) {
      console.error('查询打卡记录异常：', err);
      wx.showToast({ title: '网络异常，查询失败', icon: 'none' });
      this.setData({ attList: [] });
    } finally {
      wx.hideLoading();
    }
  }
});