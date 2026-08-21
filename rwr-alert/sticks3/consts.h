// consts.h — M5StickS3 移植版(保留上游 esp32-rwr-alert 的坐标/音符数据)

// 蜂鸣音符(Hz)
#define NOTE_HIGH 554.37
#define NOTE_MIDL 466.16
#define NOTE_LOW1 370.00
#define NOTE_LOW2 392.00

// 雷达斜线位置(上游原始数据,2x2 斜线;坐标系为上游 SH1106 128x64,
// 圆心 (96,31)。sticks3.ino 里按新圆心运行时换算)
#define SLSH_POSLEN 10
#define SLSH_POSDELI 4
static const uint8_t SLSH_LX[] = { 82, 72, 119, 109, 84, 87, 90, 101, 104, 107 };
static const uint8_t SLSH_LY[] = {  7, 17,  44,  54, 19, 22, 25,  36,  39,  42 };
static const uint8_t SLSH_RX[] = { 72, 82, 109, 119, 84, 87, 90, 101, 104, 107 };
static const uint8_t SLSH_RY[] = { 44, 54,   7,  17, 42, 39, 36,  25,  22,  19 };
