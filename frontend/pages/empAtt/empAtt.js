// 引入请求工具（新手别动这行）
const request = require('../../utils/request.js');

Page({
  data: {
    startDate: '',    // 开始日期
    endDate: '',      // 结束日期
    attList: [],      // 员工考勤统计列表
    isLoading: false, // 加载状态：防止重复点击
    isLoading: false  // 修复原有bug：补全未定义的变量
  },

  // 页面加载：默认查询近7天考勤
  onLoad() {
    const now = new Date();
    const lastWeek = new Date(now.getTime() - 7 * 24 * 60 * 60 * 1000); // 7天前
    // 设置默认日期（格式化为YYYY-MM-DD）
    this.setData({
      startDate: this.formatDate(lastWeek),
      endDate: this.formatDate(now)
    });
  },

  // 工具函数：格式化日期（把日期对象转成【2026-02-05】格式，新手别动）
  formatDate(date) {
    const year = date.getFullYear();
    const month = (date.getMonth() + 1).toString().padStart(2, '0'); // 月份补0
    const day = date.getDate().toString().padStart(2, '0');         // 日期补0
    return `${year}-${month}-${day}`;
  },

  // 选择【开始日期】- 绑定wxml
  onStartDateChange(e) {
    this.setData({ startDate: e.detail.value });
  },

  // 选择【结束日期】- 绑定wxml
  onEndDateChange(e) {
    this.setData({ endDate: e.detail.value });
  },

  // 核心功能：查询全员工考勤统计
  async getEmpAtt() {
    const { startDate, endDate } = this.data;

    // 1. 校验日期：没选/开始晚于结束，提示用户
    if (!startDate || !endDate) {
      wx.showToast({ title: '请选择查询时间', icon: 'none' });
      return;
    }
    if (new Date(startDate) > new Date(endDate)) {
      wx.showToast({ title: '开始日期不能晚于结束', icon: 'none' });
      return;
    }

    // 2. 防止重复点击
    if (this.data.isLoading) return;
    this.setData({ isLoading: true });

    try {
      // 3. 调用管理员考勤统计接口（和你原来的接口路径完全一致）
      const res = await request.get('/api/att/getEmpAtt', {
        start: startDate,
        end: endDate
      });

      // 4. 处理接口返回
      if (res.code === 200) {
        
        this.setData({ attList: res.data || [] });
        if (res.data.length === 0) {
          wx.showToast({ title: '暂无该时间段考勤数据', icon: 'none' });
        }
      } else {
        wx.showToast({ title: res.msg || '查询失败', icon: 'none' });
        this.setData({ attList: [] });
      }
    } catch (err) {
      // 5. 网络异常
      wx.showToast({ title: '网络不好，请重试', icon: 'none' });
      this.setData({ attList: [] });
    } finally {
      // 6. 标记加载完成
      this.setData({ isLoading: false });
    }
  }
});