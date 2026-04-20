#ifndef APPLICATION_H
#define APPLICATION_H

#ifdef __cplusplus
extern "C" {
#endif

/* 应用层总入口，负责初始化业务相关模块。 */
// #define APPLICATION_RUN_BOARD_TEST        1U

void Application_Init(void);

#ifdef __cplusplus
}
#endif

#endif
