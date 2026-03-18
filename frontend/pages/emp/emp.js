// pages/emp/emp.js
const request = require('../../utils/request.js');
Page({
  data: {
    empList: [], // 员工列表
    showModal: false, // 弹窗显示
    isEdit: false, // 是否编辑
    form: { name: '', user_id: '', dept: '', phone: '', id: '' } // 表单数据
  },

  onLoad(options) {
    // 页面加载查询员工列表
    this.getEmpList();
  },

  // 查询所有员工【核心修复：取res.data.list数组】
  async getEmpList() {
    try {
      wx.showLoading({ title: '加载中...', mask: true }); // 加mask防止误操作
      const res = await request.get('/api/emp/list');
      if (res.code === 200) {
        // 核心修复：后端返回的员工列表在data.list中，强制兼容空数组避免报错
        this.setData({ empList: res.data.list || [] });
      } else {
        wx.showToast({ title: res.msg || '查询失败', icon: 'none' });
      }
    } catch (err) {
      wx.showToast({ title: '网络异常', icon: 'none' });
      console.log('查询员工失败：', err);
    } finally {
      // 无论成功/失败，都隐藏加载中【新增：必加】
      wx.hideLoading();
    }
  },

  // 显示新增弹窗
  showAddModal() {
    this.setData({
      showModal: true,
      isEdit: false,
      form: { name: '', user_id: '', dept: '', phone: '', id: '' }
    });
  },

  // 显示编辑弹窗
  showEditModal(e) {
    const item = e.currentTarget.dataset.item;
    this.setData({
      showModal: true,
      isEdit: true,
      form: { ...item } // 赋值员工数据到表单
    });
  },

  // 隐藏弹窗
  hideModal() {
    this.setData({ showModal: false });
  },

  // 阻止弹窗冒泡
  stopPropagation() {},

  // 表单输入监听【优化：强制trim防空格】
  onNameInput(e) { this.setData({ 'form.name': e.detail.value.trim() }); },
  onUserIdInput(e) { this.setData({ 'form.user_id': e.detail.value.trim() }); },
  onDeptInput(e) { this.setData({ 'form.dept': e.detail.value.trim() }); },
  onPhoneInput(e) { this.setData({ 'form.phone': e.detail.value.trim() }); },

  // 保存员工（新增/编辑）【优化：加载态+必隐藏】
  async onSave() {
    const { form, isEdit } = this.data;
    // 校验必填项（姓名/UID/部门）
    if (!form.name || !form.user_id || !form.dept) {
      wx.showToast({ title: '姓名/UID/部门为必填', icon: 'none' });
      return;
    }
    try {
      wx.showLoading({ title: isEdit ? '修改中...' : '新增中...', mask: true });
      let res;
      if (isEdit) {
        // 编辑员工：匹配后端/api/emp/edit
        res = await request.post('/api/emp/edit', form);
      } else {
        // 新增员工：匹配后端/api/emp/add
        res = await request.post('/api/emp/add', form);
      }
      if (res.code === 200) {
        wx.showToast({ title: res.msg, icon: 'success' });
        this.hideModal();
        this.getEmpList(); // 刷新列表
      } else {
        wx.showToast({ title: res.msg || '操作失败', icon: 'none' });
      }
    } catch (err) {
      wx.showToast({ title: '网络异常', icon: 'none' });
      console.log('保存员工失败：', err);
    } finally {
      wx.hideLoading();
    }
  },

  // 删除员工【核心修复：请求方式/路径匹配后端GET /api/emp/delete/:id】
  async onDelete(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return wx.showToast({ title: '员工ID异常', icon: 'none' });
    
    wx.showModal({
      title: '确认删除',
      content: '删除后员工将无法打卡，是否确认？',
      success: async (res) => {
        if (res.confirm) {
          try {
            wx.showLoading({ title: '删除中...', mask: true });
            // 核心修复：后端是GET请求，路径拼接id（/api/emp/delete/123）
            const res = await request.get(`/api/emp/delete/${id}`);
            if (res.code === 200) {
              wx.showToast({ title: res.msg, icon: 'success' });
              this.getEmpList(); // 刷新列表
            } else {
              wx.showToast({ title: res.msg || '删除失败', icon: 'none' });
            }
          } catch (err) {
            wx.showToast({ title: '删除失败', icon: 'none' });
            console.log('删除员工失败：', err);
          } finally {
            wx.hideLoading();
          }
        }
      }
    });
  },

  // 重置员工密码（默认123456）【优化：加载态+必隐藏】
  async resetPwd(e) {
    const id = e.currentTarget.dataset.id;
    if (!id) return wx.showToast({ title: '员工ID异常', icon: 'none' });
    
    wx.showModal({
      title: '确认重置',
      content: '是否将该员工密码重置为默认【123456】？',
      success: async (res) => {
        if (res.confirm) {
          try {
            wx.showLoading({ title: '重置中...', mask: true });
            const res = await request.post('/api/emp/resetPwd', { id });
            if (res.code === 200) {
              wx.showToast({ title: res.msg, icon: 'success' });
            } else {
              wx.showToast({ title: res.msg || '重置失败', icon: 'none' });
            }
          } catch (err) {
            wx.showToast({ title: '重置失败', icon: 'none' });
            console.log('重置密码失败：', err);
          } finally {
            wx.hideLoading();
          }
        }
      }
    });
  }
});