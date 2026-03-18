const request = require('../../utils/request.js');

Page({
  data: {
    startDate: '', // 起始时间
    endDate: '',   // 结束时间
    isManualEnd: false // 是否手动修改过结束时间，默认否
  },

  onLoad() {
    // 初始化：起始=当月第一天，结束=起始+30天
    const start = this.getMonthFirstDay();
    const end = this.add30Days(start);
    this.setData({ startDate: start, endDate: end });
  },

  // 工具方法：获取当前月份的第一天（格式YYYY-MM-DD）
  getMonthFirstDay() {
    const now = new Date();
    return new Date(now.getFullYear(), now.getMonth(), 1).toISOString().slice(0, 10);
  },

  // 工具方法：给指定日期加30天（格式YYYY-MM-DD）
  add30Days(dateStr) {
    const date = new Date(dateStr);
    date.setDate(date.getDate() + 30);
    return date.toISOString().slice(0, 10);
  },

  // 监听起始时间修改：未手动改结束时间时，自动更新结束时间为起始+30天
  onStartChange(e) {
    const newStart = e.detail.value;
    if (!this.data.isManualEnd) {
      const newEnd = this.add30Days(newStart);
      this.setData({ startDate: newStart, endDate: newEnd });
    } else {
      this.setData({ startDate: newStart });
    }
  },

  // 监听结束时间修改：标记为手动修改，后续不再自动同步
  onEndChange(e) {
    this.setData({
      endDate: e.detail.value,
      isManualEnd: true
    });
  },

  // 核心：导出Excel（保留原有正常逻辑）
  async exportExcel() {
    const { startDate, endDate } = this.data;
    if (!startDate || !endDate) {
      wx.showToast({ title: '请选择日期范围', icon: 'none' });
      return;
    }
    if (startDate > endDate) {
      wx.showToast({ title: '开始日期不能晚于结束日期', icon: 'none' });
      return;
    }

    try {
      wx.showLoading({ title: '生成报表中...' });
      const res = await request.get('/api/att/export', {
        start: startDate,
        end: endDate
      });

      console.log('【后端原始返回数据】：', res);
      if (!res || res.code !== 200) {
        wx.hideLoading();
        wx.showToast({ title: res?.msg || '生成失败', icon: 'none' });
        return;
      }

      const { base64, fileName } = res.data || {};
      if (!base64 || typeof base64 !== 'string') {
        wx.hideLoading();
        wx.showToast({ title: '后端未返回有效报表数据', icon: 'none' });
        return;
      }
      const finalFileName = fileName || `考勤报表_${startDate}_${endDate}.xlsx`;
      const filePath = `${wx.env.USER_DATA_PATH}/${finalFileName}`;
      const fs = wx.getFileSystemManager();
      fs.writeFileSync(filePath, wx.base64ToArrayBuffer(base64), 'binary');

      wx.openDocument({
        filePath,
        showMenu: true,
        fileType: 'xlsx',
        success: () => {
          wx.hideLoading();
          wx.showToast({ title: '报表打开成功', icon: 'success' });
        },
        fail: (err) => {
          wx.hideLoading();
          wx.showToast({ title: '打开失败，可重试', icon: 'none' });
          console.log('【打开Excel失败详情】：', err);
        }
      });

    } catch (err) {
      wx.hideLoading();
      wx.showToast({ title: '网络/生成失败', icon: 'none' });
      console.log('【导出整体失败详情】：', err);
    }
  }
});