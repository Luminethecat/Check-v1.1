#ifndef STORAGE_LAYOUT_H
#define STORAGE_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* 内部Flash固定分区：
 * 参数区放设备配置，用户区放身份档案，记录区顺序追加。 */
#define STORAGE_ADDR_SYS_PARAM_BASE        0x0800F000UL  // 内部Flash末尾
#define STORAGE_ADDR_USER_BASE             0x0800E000UL
#define STORAGE_ADDR_RECORD_BASE           0x0800C000UL

#define STORAGE_MAX_USER_COUNT             512UL
#define STORAGE_MAX_RECORD_COUNT           4096UL

#ifdef __cplusplus
}
#endif

#endif
