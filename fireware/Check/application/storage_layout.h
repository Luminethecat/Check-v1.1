#ifndef STORAGE_LAYOUT_H
#define STORAGE_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* W25Q32 固定分区：
 * 参数区放设备配置，用户区放身份档案，记录区顺序追加，音频区按索引管理。 */
#define STORAGE_ADDR_SYS_PARAM_BASE        0x000000UL
#define STORAGE_ADDR_USER_BASE             0x010000UL
#define STORAGE_ADDR_RECORD_BASE           0x060000UL
#define STORAGE_ADDR_AUDIO_INDEX_BASE      0x1E0000UL
#define STORAGE_ADDR_AUDIO_DATA_BASE       0x1E1000UL

#define STORAGE_MAX_USER_COUNT             512UL
#define STORAGE_MAX_RECORD_COUNT           4096UL

#define AUDIO_INDEX_CHECK_OK               0U
#define AUDIO_INDEX_LATE                   1U
#define AUDIO_INDEX_EARLY                  2U
#define AUDIO_INDEX_CHECK_FAIL             3U
#define AUDIO_INDEX_ENROLL_OK              4U
#define AUDIO_INDEX_TIME_SYNC_OK           5U

#ifdef __cplusplus
}
#endif

#endif
