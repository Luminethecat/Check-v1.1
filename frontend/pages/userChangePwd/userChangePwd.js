// pages/userchangepwd/userchangepwd.js
const request = require('../../utils/request.js');
Page({
  data: {
    oldPwd: '',
    newPwd: ''
  },

  onOldPwdChange(e) {
    this.setData({ oldPwd: e.detail.value.trim() });
  },
  onNewPwdChange(e) {
    this.setData({ newPwd: e.detail.value.trim() });
  },

  async changePwd() {
    const { oldPwd, newPwd } = this.data;
    if (!oldPwd) { wx.showToast({ title: '请输入原密码', icon: 'none' }); return; }
    if (!newPwd || newPwd.length < 6) { wx.showToast({ title: '新密码需6位及以上', icon: 'none' }); return; }
    if (oldPwd === newPwd) { wx.showToast({ title: '新密码不能与原密码一致', icon: 'none' }); return; }

    try {
      wx.showLoading({ title: '修改中...' });
      const res = await request.post('/api/user/changePwd', { oldPwd, newPwd });
      if (res.code === 200) {
        wx.showToast({ title: res.msg, icon: 'success', duration: 1500 });
        setTimeout(() => {
          wx.navigateBack();
        }, 1500);
      } else {
        wx.showToast({ title: res.msg, icon: 'none' });
      }
    } catch (err) {
      wx.showToast({ title: '网络异常，修改失败', icon: 'none' });
    }
  }
});