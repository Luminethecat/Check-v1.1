#ifndef APPLICATION_H
#define APPLICATION_H

#ifdef __cplusplus
extern "C" {
#endif

/* 应用层总入口，负责初始化业务相关模块。 */
/* 启用板级自检/测试任务：
 *  - 置为 1 开启测试任务/页面（仅用于开发调试）
 *  - 默认关闭（0），生产固件请保持为 0
 */
#ifndef APPLICATION_RUN_BOARD_TEST
#define APPLICATION_RUN_BOARD_TEST        0U
#endif

void Application_Init(void);

#ifdef __cplusplus
}
#endif

#endif
