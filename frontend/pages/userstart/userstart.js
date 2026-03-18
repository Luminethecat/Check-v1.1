const request = require('../../utils/request.js');
const app = getApp();

Page({
  data: {
    year: new Date().getFullYear().toString(),
    statInfo: {
      statList: [],
      rule: {},
      total: {
        totalDays: 0,
        normalDays: 0,
        lateDays: 0,
        earlyDays: 0,
        absentDays: 0
      }
    },
    hasNoData: false
  },

  onLoad: function () {
    this.getUserStat();
  },

  onYearChange: function (e) {
    this.setData({ year: e.detail.value }, () => {
      this.getUserStat();
    });
  },

  getUserStat: async function () {
    const { year } = this.data;
    wx.showLoading({ title: '加载考勤数据...', mask: true });

    try {
      const res = await request.get('/api/user/att/stat', { year });

      if (res.code === 200) {
        const statList = res.data.statList || [];
        const rule = res.data.rule || {};
        const total = res.data.total || {
          totalDays: 0, normalDays: 0, lateDays: 0, earlyDays: 0, absentDays: 0
        };

        const hasNoData = statList.length === 0;

        this.setData({
          statInfo: { statList, rule, total },
          hasNoData: hasNoData
        });

        if (hasNoData) {
          wx.showToast({ title: `暂无${year}年考勤数据`, icon: 'none', duration: 1500 });
        }
      } else {
        wx.showToast({ title: res.msg || '加载失败', icon: 'none' });
        this.setData({
          statInfo: {
            statList: [],
            rule: {},
            total: { totalDays: 0, normalDays: 0, lateDays: 0, earlyDays: 0, absentDays: 0 }
          },
          hasNoData: true
        });
      }
    } catch (err) {
      wx.showToast({ title: '网络不好，请重试', icon: 'none' });
      this.setData({
        statInfo: {
          statList: [],
          rule: {},
          total: { totalDays: 0, normalDays: 0, lateDays: 0, earlyDays: 0, absentDays: 0 }
        },
        hasNoData: true
      });
    } finally {
      wx.hideLoading();
    }
  }
});