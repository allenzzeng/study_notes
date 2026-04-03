#ifndef ADSCALER_MODEL_H
#define ADSCALER_MODEL_H

#include "../adscaler_config.h"
#include "adscaler_dump.h"

// --------------------- 窗口尺寸与偏移宏定义 ---------------------
#define ADSCALER_WIN_H 4
#define ADSCALER_LASTROW (ADSCALER_WIN_H - 1)

#define ADSCALER_WIN_W 8
#define ADSCALER_HALF_WIN_W 4
#define ADSCALER_HALF_WIN_H 2
#define ADSCALER_WIN_UP -1
#define ADSCALER_WIN_DOWN 2
#define ADSCALER_WIN_LEFT -2
#define ADSCALER_WIN_RIGHT 3

// --------------------- 插值表尺寸宏定义 ---------------------
#define HORZ_TAB 8
#define HORZ_TAB_LEFT (-3)
#define HORZ_TAB_RIGHT 4
#define VERT_TAB 4
#define VERT_TAB_UP (-1)
#define VERT_TAB_DOWN 2

// --------------------- 滤波器最大值宏定义 ---------------------
#define HFILT_MAX ((1 << 16) - 1)
#define VFILT_MAX ((1 << 18) - 1)

// --------------------- UV 插值模式条件编译 ---------------------
#ifndef ADSCALER_UV_4x4
#define ADSCALER_WIN_CH 4
#define ADSCALER_C_LASTROW (ADSCALER_WIN_CH - 1)
#define ADSCALER_WIN_CW 4
#define ADSCALER_HALF_WIN_CH 2
#define ADSCALER_HALF_WIN_CW 2
#else
#define ADSCALER_WIN_CH 2
#define ADSCALER_C_LASTROW (ADSCALER_WIN_CH - 1)
#define ADSCALER_WIN_CW 2
#define ADSCALER_HALF_WIN_CH 1
#define ADSCALER_HALF_WIN_CW 1
#endif

// --------------------- 像素类型定义 ---------------------
typedef int AdscalerPixel;

// --------------------- 算法模型结构体 ---------------------
struct AdscalerModel {
    // 原始像素与窗口像素缓存
    AdscalerPixel *pixel[ADSCALER_WIN_H];
    AdscalerPixel *win_pixel[ADSCALER_WIN_H];
    
    // 插值结果缓存
    AdscalerPixel *nmintrp_out;
    AdscalerPixel *v_nmintrp_out;
    AdscalerPixel *h_nmintrp_out[ADSCALER_WIN_H]; // 水平插值结果
    
    // 采样缓存
    AdscalerPixel *main_samples[ADSCALER_WIN_H];
    AdscalerPixel *sub_samples[ADSCALER_WIN_H];
    
    // 输出缓存
    AdscalerPixel *out;

    // 调试文件指针（多通道）
    FILE *fptn[NUM_ASU_DUMP_FILTES];
    
    // 通道参数
    int bit_depth[NUM_CHN];
    int pixel_step[NUM_CHN]; // 像素步长（字节数）
    
    // 方向与采样器
    struct BiDirection *direction;
    struct Adsampler *sampler;
    
    // 坐标与区域参数
    int main_region_idx;
    int sub_region_idx;
    int out_y, out_x;
    int chn;               // 当前处理通道
    int num_rolllines;     // 滚动行数
    
    // 分数坐标与整数坐标
    int coor_x, coor_y;
    int x_intg, y_intg;    // 整数部分
    int x_frac, y_frac;    // 分数部分
    int x_stride;          // 水平步长
    
    // 插值参数缓存
    int *x_intgs;
    int *y_intgs;
    int *x_fracs;
    int *y_fracs;
    
    // 边缘检测参数
    int edge_strength;
    int local_min2x2[2][2];
    int local_max2x2[2][2];
    
    // 相位参数
    int main_x_phase, main_y_phase;
    int sub_x_phase, sub_y_phase;
    int peak_weights;      // 峰值权重
};

// --------------------- 函数声明 ---------------------
struct AdscalerModel *AdscalerModel_New(struct AdscalerConfig *config);
void AdscalerModel_OpenDumpFiles(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_CloseDumpFiles(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_CheckConfig(struct AdscalerConfig *config);
int AdscalerModel_CalcTableOffset(int ratio);
void AdscalerModel_Free(struct AdscalerModel **model);

// 多通道处理（带调试路径）
void AdscalerModel_RunDE(struct AdscalerModel *model, struct AdscalerConfig *config, 
                         unsigned char *inbuff0, unsigned char *inbuff1, 
                         unsigned char *inbuff2, unsigned char *inbuff3, 
                         unsigned char *inbuff4, unsigned char *outbuff0, 
                         unsigned char *outbuff1, unsigned char *outbuff2, 
                         unsigned char *outbuff3, unsigned char *outbuff4, 
                         char dump_path[FILENAMEMAX]);

// 基础处理流程
void AdscalerModel_Run(struct AdscalerModel *model, struct AdscalerConfig *config, 
                       PixelType *in[3], PixelType *out[3]);
void AdscalerModel_SetBitdepth(struct AdscalerModel *model, int bitdepth[]);
void AdscalerModel_RunChannel(struct AdscalerModel *model, struct AdscalerConfig *config, 
                              PixelType *in, PixelType *out);
void AdscalerModel_Init(struct AdscalerModel *model, struct AdscalerConfig *config, PixelType *in);
void AdscalerModel_GenOutputCoors(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_RollLineBuffer(struct AdscalerModel *model, struct AdscalerConfig *config, PixelType *in);
void AdscalerModel_LineBorderExtend(AdscalerPixel *line, int width);
void AdscalerModel_RunLine(struct AdscalerModel *model, struct AdscalerConfig *config);
void Adscaler_RegionProcessing(struct AdscalerModel *model, struct AdscalerConfig *config);
int AdscalerModel_MainInterpolation(struct AdscalerModel *model, struct AdscalerConfig *config);
int AdscalerModel_SubInterpolation(struct AdscalerModel *model, struct AdscalerConfig *config);

// 插值算法（部分为条件编译）
// int AdscalerModel_NormalInterpolationWin(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_NormalInterpolation(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_NormalInterpolationV(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_NormalInterpolationH(AdscalerPixel *out, AdscalerPixel *pixel_in, 
                                         int *x_intgs, int *x_fracs, unsigned int *coefs, 
                                         int out_width, int bitdepth);
void Adscaler_GetNormalScaleCoefs(char coefs[], int tabs, int sx, unsigned int coefs_table[]);

// 峰值与梯度计算
int AdscalerModel_Peaking(int intrp_out,struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_CalcGradient(struct AdscalerModel *model, AdscalerPixel *win[4]);
int AdscalerModel_UVPeaking(int intrp_out, struct AdscalerModel *model, struct AdscalerConfig *config);

// 状态更新与数据读写
void AdscalerModel_RefreshY(struct AdscalerModel *model, struct AdscalerConfig *config, PixelType *in);
void AdscalerModel_RefreshStatusY(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_RefreshX(struct AdscalerModel *model, struct AdscalerConfig *config, PixelType *in);
void AdscalerModel_RefreshStatusX(struct AdscalerModel *model, struct AdscalerConfig *config);
void AdscalerModel_InputLine(AdscalerPixel *dst, PixelType *src, int length, int pixel_step);
void AdscalerModel_OutputLine(PixelType *dst, AdscalerPixel *src, int length, int pixel_step);

// 调试信息转储
void AdscalerModel_DumpDebugInfos(struct AdscalerModel *model);

#endif // ADSCALER_MODEL_H