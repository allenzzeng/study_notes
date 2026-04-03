#ifndef ADSCALER_TABLE_H
#define ADSCALER_TABLE_H

#ifdef ADSCALER

// 缩放器相位位宽（5位）
#define ADSCALER_NPHASE_BITS 5
// 相位半单位值（16）
#define ADSCALER_NPHASE_HALF_UNIT 16
// 缩放器系数位宽（6位）
#define ADSCALER_COEFS_BITS 6
// 系数半单位值（32）
#define ADSCALER_COEFS_HALF_UNIT 32
// 缩放表长度（128）
#define ADSCALER_TABLE_LENGTH 128

// 水平缩放表（128个short型元素）
extern short horztab[128];
// 对角线缩放表（128个short型元素）
extern short diagtab[128];
// 平滑模式斜率表（128个short型元素）
extern short slope_smooth_tab[128];
// 锐化模式斜率表（128个short型元素）
extern short slope_sharp_tab[128];

#endif

#endif