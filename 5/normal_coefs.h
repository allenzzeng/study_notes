#ifndef __TAB_H
#define __TAB_H

// 缩放比例分组配置 - 不同倍率对应的分组数量
#define N_GROUPS_RATIO0 1    // 缩放比例组0：1倍
#define N_GROUPS_RATIO1 8    // 缩放比例组1：8倍
#define N_GROUPS_RATIO2 4    // 缩放比例组2：4倍
#define N_GROUPS_RATIO3 1    // 缩放比例组3：1倍
#define N_GROUPS_RATIO4 1    // 缩放比例组4：1倍

// 缩放系数的位宽配置 - 6位精度（决定系数计算的误差大小）
#define SCALER_COEFS_BITS 6

// 色彩空间转换（CSC）系数矩阵 - BT.709高清视频标准
__s32 bt709y2r1mt[3][4];        // YUV转RGB的系数矩阵（BT.709标准）
__s32 bt709r2ylmt[3][4];        // RGB转YUV的系数矩阵（BT.709标准）
__s32 bypasscsc[3][4];          // CSC旁路系数（直接透传色彩数据，不做转换）
__s32 bt709r2ylmt1umacsc[4];    // 简化版RGB转YUV系数（单通道/硬件优化版）

// 16位精度图像缩放插值系数
__u32 linearcoefftab16[8];              // 16位线性插值系数（基础缩放，速度快）
__u32 lan2coefftab16[16];               // 16位Lanczos2插值系数（高阶缩放，画质好）
__u32 lan2coefftab16_down[16];          // 16位Lanczos2下采样系数（专门用于缩小图像）
__u32 nncoefftab16_4tap[16];            // 16位4抽头最近邻插值系数（最快的缩放算法）
__u32 nncoefftab16_2tap[8];             // 16位2抽头最近邻插值系数（极简版最近邻）

// 32位精度图像缩放插值系数
__u32 lan3coefftab32[64];               // 32位Lanczos3插值系数（比Lanczos2画质更优）
__u32 lan3coefftab32_down[64];          // 32位Lanczos3下采样系数（高精度缩小图像）
__u32 lan2coefftab32[32];               // 32位Lanczos2插值系数（32位精度版）
__u32 lan2coefftab32_down[32];          // 32位Lanczos2下采样系数（32位精度缩小）
__u32 nncoefftab32_8tap[64];            // 32位8抽头最近邻插值系数（高精度最近邻）
__u32 nncoefftab32_4tap[32];            // 32位4抽头最近邻插值系数（32位精度版）
__u32 duplicatecoefftab32_8tap[64];     // 32位8抽头复制插值系数（整数倍放大图像）
__u32 duplicatecoefftab32_4tap[32];     // 32位4抽头复制插值系数（基础整数倍放大）
__u32 linearcoefftab32_8tap[64];        // 32位8抽头线性插值系数（32位精度线性缩放）
__u32 linearcoefftab32_4tap[32];        // 32位4抽头线性插值系数（基础版线性缩放）
__u32 lan3coefftab32_full[1024];        // 32位Lanczos3全精度系数（最高画质缩放）
__u32 lan2coefftab32_full[512];         // 32位Lanczos2全精度系数（Lanczos2最高精度）
__u32 bicubic4coefftab32[512];          // 32位4阶双三次插值系数（专业级图像缩放）
__u32 bicubic8coefftab32[1024];         // 32位8阶双三次插值系数（最优画质缩放）

// 编译开关 - 启用8位滤波系数（节省内存/提升速度，画质略有损失）
#ifdef FILTERCOEFF8BIT
#endif

// 反正切函数查找表 - 用于图像旋转/透视矫正等几何变换（查表替代实时计算，提速）
int ATAN_LUT[64];

#endif