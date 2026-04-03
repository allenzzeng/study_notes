#ifndef ADSCALER_CONFIG_H
#define ADSCALER_CONFIG_H

// --------------------- 相位与累加器宏定义 ---------------------
#define LOCINTGBIT    4   // 累加相位的整数部分位宽
#define LOCFRACBIT   20   // 累加相位的小数部分位宽
#define LOCBIT       (LOCINTGBIT + LOCFRACBIT) // 累加相位总位宽
#define LOC_UNIT     (1 << LOCFRACBIT)         // 累加相位的单位值
#define HALF_LOC_UNIT (1 << (LOCFRACBIT - 1))   // 半单位值（用于比较）

#define SCALERPHASEBIT 5  // 缩放相位位宽
#define HALFPHASEBIT  (LOCFRACBIT - SCALERPHASEBIT) // 半相位小数位宽
#define SCALERPHASE   (1 << SCALERPHASEBIT)    // 缩放相位单位
#define PHASEMASK     (SCALERPHASE - 1)        // 相位掩码（用于截断）

// --------------------- 通道与位深宏定义 ---------------------
#define NUM_CHN       5   // 通道数量

#define ALPHABITDEPTH 8   // Alpha 通道位深
#define ALPHAVCLIP    6   // Alpha 垂直裁剪位
#define ALPHACLIP     (12 - ALPHAVCLIP) // Alpha 裁剪值
#define ALPHACLAMP    (ALPHABITDEPTH + ALPHACLIP) // Alpha 钳位值

// --------------------- 分辨率与缩放宏定义 ---------------------
#define ASU_MAX_WIDTH 4096 // ASU 最大支持宽度

#define RGB_4LINE_LBLEN  (ASU_MAX_WIDTH >> 1) // RGB 通道行缓存宽度（UI 模式）
#define LBLEN          ASU_MAX_WIDTH        
#define Y_LBLEN          ASU_MAX_WIDTH        // Y 通道行缓存宽度（正常视频模式）
#define EDI_Y_LBLEN      ASU_MAX_WIDTH        // Y 通道行缓存宽度（EDI 模式）
#define MAXINHEIGHT      ASU_MAX_WIDTH        // 最大输入高度
#define MAXOUTWIDTH      ASU_MAX_WIDTH        // 最大输出宽度
#define MAXOUTHEIGHT     ASU_MAX_WIDTH        // 最大输出高度

#define MAXZOOMINRATIO  2048  // 最大缩放输入比例（32 倍）
#define MAXZOOMOUTRATIO 15.9f // 最大缩放输出比例（浮点）

#define MAXTAPNUM       8     // 最大抽头数（滤波器阶数）
#define SCALERTABLEN    (SCALERPHASE << 2) // 缩放查找表长度

// --------------------- 缩放模式枚举 ---------------------
enum SCALEMODE {
    UIMODE = 0,          // 4K 视频正常缩放模式
    VIDEONMMODE = 1,      // 2K UI 正常缩放模式
    VIDEOADMODE = 2      // 2K 视频边缘方向上采样模式
};

// --------------------- 双向配置结构体 ---------------------
struct BiDirectionConfig {
    int w_bits;          // 宽度位宽
    int th_62_63_switch; // 62/63 阈值切换
    int a;               // 参数 a
    int b;               // 参数 b
    int th_sub;          // 子阈值
    int th_flat;         // 平坦阈值
    int abnormal_a;      // 异常参数 a
    int abnormal_b;      // 异常参数 b
    int abnormal_c;      // 异常参数 c
    int abnormal_d;      // 异常参数 d
    int abnormal_e;      // 异常参数 e
    int abnormal_f;      // 异常参数 f
    int abnormal_g;      // 异常参数 g
    int abnormal_h;      // 异常参数 h
    int abnormal_i;      // 异常参数 i
    int unstable_a;      // 不稳定参数 a
    int unstable_b;      // 不稳定参数 b
    int dump_debug;      // 调试开关（0/1）
};

// --------------------- 主配置结构体 ---------------------
struct AdscalerConfig {
    int adscaler_en;     // ASU 使能（0/1）
    int demo_en;         // 演示模式使能
    int luma_stat_en;    // 亮度统计使能
    int fbd_en;          // 反馈使能
    SCALEMODE adscaler_mode[NUM_CHN]; // 各通道缩放模式
    
    int lb_mode;         // 行缓存模式
    int dovi_en;         // Dolby Vision 使能
    int local_clamp_en;  // 局部钳位使能
    
    int chn_id;          // 通道 ID
    int dump_debug;      // 调试开关（0/1）
    char dump_path[300]; // 调试文件路径
    
    int flag_en;         // 标志使能
    int alpha_en;        // Alpha 通道使能
    
    // 分辨率参数
    int width[NUM_CHN];      // 预处理后宽度
    int height[NUM_CHN];     // 预处理后高度
    int scale_out_width;     // 缩放输出宽度
    int crop_out_width;      // 裁剪输出宽度
    int out_width[NUM_CHN];  // 各通道输出宽度
    int out_height[NUM_CHN]; // 各通道输出高度
    
    // 相位与偏移参数
    int init1;          // 初始化参数 1
    int outit1;         // 输出参数 1
    int infield;        // 场内参数
    int outfield;       // 场外参数
    int glbalpha;       // 全局 Alpha 值
    int auto_phase;     // 自动相位使能
    int lcd_field_reverse; // LCD 场反转
    int step_outside;   // 场外步长
    
    // 相位与系数参数
    int hphase[NUM_CHN];     // 水平相位
    int vphase[NUM_CHN];     // 垂直相位
    int vphase1[NUM_CHN];    // 垂直相位 1
    unsigned int *nm_horz_coefs[NUM_CHN]; // 水平系数（32 tap）
    unsigned int *nm_vert_coefs[NUM_CHN]; // 垂直系数（32 tap）
    int hstep[NUM_CHN];      // 水平步长
    int vstep[NUM_CHN];      // 垂直步长
    
    // 裁剪与演示参数
    int crop_hor_start; // 水平裁剪起始
    int crop_hor_end;   // 水平裁剪结束
    int demo_hor_start; // 演示水平起始
    int demo_hor_end;   // 演示水平结束
    int demo_ver_start; // 演示垂直起始
    int demo_ver_end;   // 演示垂直结束
    
    // 图像格式
    IMAGETYPE input_yuv_format;  // 输入 YUV 格式
    IMAGETYPE output_yuv_format; // 输出 YUV 格式
    
    // 双向配置指针
    struct BiDirectionConfig *bidir; // 双向配置（关联 BiDirectionConfig）
    
    // 增强与调试参数
    int wmax_blending;      // 最大混合权重
    int peaking_en[3];      // 峰值使能
    int peak_m[3];          // 峰值参数 m
    int peak_range_limit[3];// 峰值范围限制
    int peak_weights_strength; // 峰值权重强度
    int th_strong_edge;     // 强边缘阈值
    int chroma_white;       // 色度白电平
    
    int print_en;           // 打印使能
    int dbg_x;              // 调试 X 坐标
    int dbg_y;              // 调试 Y 坐标
    int dbg_chn;            // 调试通道
    int max_value;          // 最大值
    int min_value;          // 最小值
    long long full_range_limit_number; // 全范围限制数
    
    // 8K 显示扩展参数（zengxinxin disp7）
    int disp7_8k_mode;      // 8K 显示模式
    int wb_2ppc_sel;        // WB 2PPC 选择
    int ch0_width;          // 通道 0 宽度
    int ch1_width;          // 通道 1 宽度
    int ch2_width;          // 通道 2 宽度
    int ch3_width;          // 通道 3 宽度
    int ch4_width;          // 通道 4 宽度
    int ch5_width;          // 通道 5 宽度
    
    // 裁剪参数（zengxinxin disp7）
    int ch0_crop_left;      // 通道 0 左裁剪
    int ch0_crop_right;     // 通道 0 右裁剪
    int ch1_crop_left;      // 通道 1 左裁剪
    int ch1_crop_right;     // 通道 1 右裁剪
    int ch2_crop_left;      // 通道 2 左裁剪
    int ch2_crop_right;     // 通道 2 右裁剪
    int ch3_crop_left;      // 通道 3 左裁剪
    int ch3_crop_right;     // 通道 3 右裁剪
    int ch4_crop_left;      // 通道 4 左裁剪
    int ch4_crop_right;     // 通道 4 右裁剪
    int ch5_crop_left;      // 通道 5 左裁剪
    int ch5_crop_right;     // 通道 5 右裁剪
    
    // 合并参数（zengxinxin disp7）
    int merge0_width;       // 合并 0 宽度
    int merge0_height;      // 合并 0 高度
    int merge1_width;       // 合并 1 宽度
    int merge1_height;      // 合并 1 高度
    int merge2_width;       // 合并 2 宽度
    int merge2_height;      // 合并 2 高度
    int merge0_mode;        // 合并 0 模式
    int merge1_mode;        // 合并 1 模式
    int merge2_mode;        // 合并 2 模式
};

// --------------------- 函数声明 ---------------------
struct AdscalerConfig *AdscalerConfig_New();
void AdscalerConfig_Free(struct AdscalerConfig **config);
void AdscalerConfig_Load(struct AdscalerConfig *config, const char config_path[]);

#endif // ADSCALER_CONFIG_H