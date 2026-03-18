// utils/request.js 适配Supabase后端

const baseUrl = "https://74431076.r34.cpolar.top"; 
//https://346dd0f6.r34.cpolar.top    coplar内网穿透
//
/**
 * 通用请求方法
 * @param {String} url 接口路径（无需带域名，如/api/login）
 * @param {Object} data 请求参数
 * @param {String} method 请求方式 GET/POST
 */
function request(url, data = {}, method = "GET") {
  const token = wx.getStorageSync("token") || "";
  // 先清除可能残留的loading，避免重复显示导致无法关闭
  try { wx.hideLoading(); } catch (e) {}
  // 显示loading，遮罩防止误操作
  wx.showLoading({ title: "加载中...", mask: true });

  return new Promise((resolve, reject) => {
    wx.request({
      url: baseUrl + url,
      data,
      method,
      header: {
        "content-type": "application/json",
        "token": token // 自动携带Token，后端鉴权用
      },
      timeout: 20000, // 20秒超时，适配云端网络延迟
      success: (res) => {
        if (res.statusCode === 200) {
          // 401：Token过期/未登录，跳登录页
          if (res.data.code === 401) {
            wx.showToast({ title: "登录过期，请重新登录", icon: "none", duration: 1500 });
            wx.removeStorageSync("token"); // 清除无效Token
            setTimeout(() => wx.redirectTo({ url: "/pages/login/login" }), 1500);
            reject(res.data);
            return;
          }
          // 403：无权限（如员工进管理员页面），补充处理
          if (res.data.code === 403) {
            wx.showToast({ title: res.data.msg || "无操作权限", icon: "none" });
            reject(res.data);
            return;
          }
          // 正常返回，直接resolve数据
          resolve(res.data);
        } else {
          wx.showToast({ title: `接口错误：${res.statusCode}`, icon: "none" });
          reject(res.data);
        }
      },
      fail: (err) => {
        // 移除「切换本地方案」提示（当前无本地后端，适配云端）
        wx.showToast({ title: "网络异常，请检查网络", icon: "none", duration: 2000 });
        reject(err);
        console.error("【请求失败详情】", err); // 控制台打印详情，方便调试
      },
      complete: () => {
        // 无论成功/失败，最终都关闭loading，严格配对
        try { wx.hideLoading(); } catch (e) {}
      }
    });
  });
}

// 封装GET/POST快捷方法，页面调用更简洁
request.get = (url, data) => request(url, data, "GET");
request.post = (url, data) => request(url, data, "POST");

// 改回老导出方式：直接导出request函数
module.exports = request;
// 额外把baseUrl挂在request函数上，需要用baseUrl的页面可以通过request.baseUrl获取
request.baseUrl = baseUrl;