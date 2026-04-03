#ifndef ADSCALER_DUMP_H
#define ADSCALER_DUMP_H

// --------------------- 调试文件类型数量与枚举 ---------------------
#define NUM_ASU_DUMP_FILES 25  // 调试文件/数据类型总数

// 每个宏对应一种调试数据类型，用于区分不同的转储内容
#define DUMP_ASU_FAY_IN          0   // FAY 通道输入数据
#define DUMP_ASU_UV_IN           1   // UV 通道输入数据
#define DUMP_ASU_NMV             2   // NMV 数据
#define DUMP_ASU_NMH             6   // NMH 数据
#define DUMP_ASU_PEAK_WEIGHT    10   // 峰值权重数据
#define DUMP_ASU_PEAK_OUT       11   // 峰值输出数据
#define DUMP_ASU_EDGE_STRENGTH  12   // 边缘强度数据
#define DUMP_ASU_U_PEAK         13   // U 通道峰值数据
#define DUMP_ASU_DIFF4X6        15   // 4x6 差分数据
#define DUMP_ASU_DIR6           16   // 6 方向数据
#define DUMP_ASU_DIFF4X4        17   // 4x4 差分数据
#define DUMP_ASU_DIR            18   // 方向数据
#define DUMP_ASU_MAIN_ROT       19   // 主旋转数据
#define DUMP_ASU_SUB_ROT        20   // 副旋转数据
#define DUMP_ASU_MAIN_SUB       21   // 主-副数据
#define DUMP_ASU_SUB_WEIGHT     22   // 副权重数据
#define DUMP_ASU_OUT            23   // 输出数据
#define DUMP_ASU_FLAT_WEIGHT    24   // 平坦权重数据
#define DUMP_ASU_FAYUV_OUT      25   // FAYUV 输出数据

// --------------------- 外部变量声明（在其他文件中定义） ---------------------
extern char *g_dump_file_list[];     // 调试文件路径列表（每个元素对应一种数据类型的存储路径）
extern char *g_dump_fmt_list[];      // 调试数据格式列表（如 "%02x "，控制数据转储的格式化输出）
extern unsigned int g_dump_mask_list[]; // 调试掩码列表（控制是否启用对应类型的转储）

#endif // ADSCALER_DUMP_H