#include "stdafx.h"
#include "adscaler_model.h"
#include "adscaler_table.h"
#include "normal_coefs.h"
#include "bidirection.h"
#include "adsampler.h"  // 原代码中拼写修正：adsampled.h -> adsampler.h（推测）
#include <math.h>
#include <direct.h>

// 调试宏（可选启用）
//#include "../../common/Include/AwAlgBase.h"
//#define DEBUG_ADSCALER
#define DBG_CHN 0
#define DBG_X   304
#define DBG_Y   100

// 辅助宏定义（数值运算与裁剪）
#define SIGNED_RSHIFT(num, r, s) (((num) >= 0) ? (((num) + (r)) >> (s)) : (-((abs(num) + (r)) >> (s))))
#define CLAMP(num, d, u)         ((num) > (u)? (u) : ((num) < (d) ? (d) : (num)))
#define ABS(a, b)                ((a) > (b) ? ((a) - (b)) : ((b) - (a)))
#define POW2(a)                  ((a) * (a))

// 行缓存滚动函数（声明，具体逻辑需补充）
void AdscalerModel_RollLineBuffer(struct AdscalerModel *model, struct AdscalerConfig *config);
static char chpath[100];
static char fname[100];
// 模型创建函数：初始化 AdscalerModel 并分配内存
struct AdscalerModel *AdscalerModel_New(struct AdscalerConfig *config) {
    int i;
    struct AdscalerModel *model;

    // 分配模型结构体内存（PCMEM_ALLOC( 为自定义内存分配宏）
    PCMEM_ALLOC(model, struct AdscalerModel);


    // 为像素缓存、插值结果、采样参数等分配内存
    for (i = 0; i < ADSCALER_WIN_H; ++i) {
        PCMEM_ALLOC_N(model->pixel[i], config->width[0] + 2 * ADSCALER_WIN_W, AdscalerPixel);
        PCMEM_ALLOC_N(model->h_nmintrp_out[i], config->out_width[0], AdscalerPixel);
    }

    PCMEM_ALLOC_N(model->v_nmintrp_out, config->width[0] + 2 * ADSCALER_WIN_W, AdscalerPixel);
    PCMEM_ALLOC_N(model->nmintrp_out, config->out_width[0], AdscalerPixel);

    PCMEM_ALLOC_N(model->x_intgs, config->out_width[0], int);
    PCMEM_ALLOC_N(model->y_intgs, config->out_height[0], int);
    PCMEM_ALLOC_N(model->x_fracs, config->out_width[0], int);
    PCMEM_ALLOC_N(model->y_fracs, config->out_height[0], int);

    PCMEM_ALLOC_N(model->out, config->out_width[0], AdscalerPixel);

    // 初始化方向控制与采样器（需实现 BiDirection_New / Adsampler_New）
    model->direction = BiDirection_New(config->bidir);
    model->sampler   = Adsampler_New();

dbg_error:
    return model;
}


void AdscalerModel_Free(struct AdscalerModel **model)
{
    int i;
    CHECK_PTR(model);
    CHECK_PTR(*model);

    for (i = 0; i < ADSCALER_WIN_H; ++i)
    {
        PCMEM_FREE((*model)->pixel[i]);
        PCMEM_FREE((*model)->h_nmintrp_out[i]);
    }

    PCMEM_FREE((*model)->nmintrp_out);
    PCMEM_FREE((*model)->v_nmintrp_out);

    PCMEM_FREE((*model)->x_intgs);
    PCMEM_FREE((*model)->y_intgs);
    PCMEM_FREE((*model)->x_fracs);
    PCMEM_FREE((*model)->y_fracs);

    PCMEM_FREE((*model)->out);

    BiDirection_Free(&(*model)->direction);
    Adsampler_Free(&(*model)->sampler);

    PCMEM_FREE(*model);
dbg_error:
    return;
}

void AdscalerModel_OpenDumpFiles(struct AdscalerModel *model, struct AdscalerConfig *config)
{
    char tmpfilename[300];

    if (config->dump_debug)
    {
        _mkdir(config->dump_path);
        for (int i = 0; i < NUM_ASU_DUMP_FILTES; ++i)
        {
            sprintf(tmpfilename, "%s%s", config->dump_path, g_dump_file_list[i]);
            fopen_s(&model->fptn[i], tmpfilename, "wb");
        }
    }
}

void AdscalerModel_CloseDumpFiles(struct AdscalerModel *model, struct AdscalerConfig *config)
{
    if (config->dump_debug)
    {
        for (int i = 0; i < NUM_ASU_DUMP_FILTES; ++i)
        {
            PCMEM_FCLOSE(model->fptn[i]);
        }
    }
}

void AdscalerModel_CheckConfig(struct AdscalerConfig *config)
{
    /*enum IMAGETYPE
    {
        IMAGEYUV420 = 0, IMAGEYUV422 = 2, IMAGEYUV411, IMAGEFLAG, IMAGERGB888, IMAGERGB16BIT, IMAGECSIRGB, IMAGEYUV444,
    };*/
    int h_shift, v_shift;
    int ch0v_fir_coef_ofst, ch0h_fir_coef_ofst, ch1v_fir_coef_ofst, ch1h_fir_coef_ofst;

    float ychfac, ycvfac;
    __s32 sfh0, sfv0, sfh1, sfv1;
    __s32 inw0, inw1, inh0, inh1;
    __u32 in_fmt, w_shift;

    // scale mode
    for (int i = 1; i < NUM_CHN; ++i)
    {
        config->adscaler_mode[i] = VIDEONMMODE;
    }

    //13-12-21 self-determined enable //situation 1: all size equal to zero; situation 2: input Y size and C size and Output size all equal
    if ((0 == config->width[0] || 0 == config->out_width[0] || 0 == config->height[0] || 0 == config->out_height[0]) ||
        (config->width[0] == config->out_width[0] && config->height[0] == config->out_height[0]
         && config->width[1] == config->width[0] && config->height[0] == config->height[1]))
    {
        config->adscaler_en = 0;
    }
    else
    {
        config->adscaler_en = 1;
        //determine format through Y/C input size
        ychfac = ((float)config->width[0])/((float)config->width[1]);
        ycvfac = ((float)config->height[0])/((float)config->height[1]);

        if (ychfac>0.5 && ychfac<1.5 && ycvfac>0.5 && ycvfac<1.5 )
        {
            in_fmt = INYUV444;
        }
        else if(ychfac>1.5 && ychfac<2.5 && ycvfac>0.5 && ycvfac<1.5)
        {
            in_fmt = INYUV422;
        }
        else if(ychfac>1.5 && ychfac<2.5 && ycvfac>1.5 && ycvfac<2.5)
        {
            in_fmt = INYUV420;
        }
        else
        {
            in_fmt = INYUV411;
        }
        config->input_yuv_format = (IMAGETYPE)(in_fmt + 3);

        if (in_fmt == INYUV444)
        {
            config->adscaler_mode[0] = UIMODE;

            if(config->width[0]>LBLEN)
            {
                config->width[0] = LBLEN;
            }

            // lb_mode add in de350
            if (config->width[0] > RBB_4LINE_LBLEN)
            {
                config->lb_mode = 1; // 2 line mode
            }

            for (int i = 0; i < 3; ++i)
            {
                config->peaking_en[i] = 0;
            }
        }
        else if(in_fmt == INYUV420 || in_fmt == INYUV422 || in_fmt == INYUV411)
        {
            if (config->adscaler_mode[0] > VIDEOADMODE)
            {
                config->adscaler_mode[0] = VIDEOADMODE;
            }

            if (config->adscaler_mode[0] == UIMODE) //if not set the scale_mode in ptn file
            {
                config->adscaler_mode[0] = VIDEOADMODE; //video ed mode prior
            }

            if (config->width[0]>Y_LBLEN)
            {
                config->width[0] = Y_LBLEN;
                config->adscaler_mode[0] = VIDEONMMODE;
            }
            else if (config->width[0]<=Y_LBLEN && config->width[0]>EDI_Y_LBLEN && config->adscaler_mode[0]==VIDEOADMODE && Y_LBLEN>EDI_Y_LBLEN)
            {
                config->adscaler_mode[0] = VIDEONMMODE;
            }
        }
    

    if(config->width[0]<4)
    {
        config->width[0] = 4;
    }
    if(config->height[0]<2)
    {
        config->height[0] = 2;
    }
    if(config->height[0]>MAXINHEIGHT)
    {
        config->height[0]=MAXINHEIGHT;
    }

    //scaler ratio in range of [1/16, 32]
    if(config->width[0] * MAXZOOMINRATIO < config->out_width[0])
    {
        config->out_width[0] = config->width[0] * MAXZOOMINRATIO;
    }
    if((float)config->out_width[0] * MAXZOOMOUTRATIO < (float)config->width[0])
    {
        config->out_width[0] = config->width[0] / MAXZOOMOUTRATIO;
    }
    if(config->height[0] * MAXZOOMINRATIO < config->out_height[0])
    {
        config->out_height[0] = config->height[0] * MAXZOOMINRATIO;
    }
    if((float)config->out_height[0] * MAXZOOMOUTRATIO < (float)config->height[0])
    {
        config->out_height[0] = config->height[0] / MAXZOOMOUTRATIO;
    }

    if (config->adscaler_mode[0] == VIDEOADMODE && ((config->width[0] > config->out_width[0]) || (config->height[0] > config->out_height[0])))
    {
        config->adscaler_mode[0] = VIDEONMMODE;
    }

    if (config->crop_hor_end <= 0
        || config->crop_hor_end >= config->out_width[0])
    {
        config->crop_hor_end = config->out_width[0] - 1;
    }

    if (config->crop_hor_start > config->crop_hor_end
        || config->crop_hor_start < 0)
    {
        config->crop_hor_start = 0;
    }

    if (config->demo_hor_end <= 0
        || config->demo_hor_end >= config->out_width[0])
    {
        config->demo_hor_end = config->out_width[0] - 1;
    }

    if (config->demo_hor_start > config->demo_hor_end
        || config->demo_hor_start < 0)
    {
        config->demo_hor_start = 0;
    }

    if (config->demo_ver_end <= 0
        || config->demo_ver_end >= config->out_height[0])
    {
        config->demo_ver_end = config->out_height[0] - 1;
    }

    if (config->demo_ver_start > config->demo_ver_end
        || config->demo_ver_start < 0)
    {
        config->demo_ver_start = 0;
    }
}
    inw0 = config->width[0];
    inh0 = config->height[0];

    in_fmt = (IMAGETYPE)(config->input_yuv_format - 3);

    //format
    if(in_fmt == INYUV422 || in_fmt == INYUV420)
    {
        w_shift = 1;
        inw1 = (inw0 + 0x1)>>1;
    }
    else if(in_fmt == INYUV411)
    {
        w_shift = 2;
        inw1 = (inw0 + 0x3)>>2;
    }
    else
    {
        w_shift = 0;
        inw1 = inw0;
    }

    //vertical
    if(in_fmt == INYUV420)
    {
        h_shift = 1;
        inh1 = (inh0 + 0x1 )>>1;
    }
    else
    {
        h_shift = 0;
        inh1 = inh0;
    }

    if(config->adscaler_en)
    {
        if (config->step_outside)
        {
            sfh0 = config->hstep[0]<<1; //phase is 18 bit fraction, so X2
            sfv0 = config->vstep[0]<<1;
            sfh1 = sfh0>>w_shift;
            sfv1 = sfv0>>h_shift;
        }
        else
        {
            sfh0 = (__s32)(((__int64)inw0<<LOCFRACBIT)/(__int64)config->out_width[0]);
            sfv0 = (__s32)(((__int64)inh0<<LOCFRACBIT)/(__int64)config->out_height[0]);
            sfh1 = sfh0>>w_shift;
            sfv1 = sfv0>>h_shift;
        }
    }
    else
    {
        sfh0 = sfh1 = 1<<LOCFRACBIT;
        sfv0 = sfv1 = 1<<LOCFRACBIT;
    }

    config->initl = 0;
    config->infield = 0;

    //output
    config->outitl = 0;

    config->out_width[1] = config->out_width[0];
    config->out_height[1] = config->out_height[0];
    config->out_width[2] = config->out_width[0];
    config->out_height[2] = config->out_height[0];

    //SCALER
    config->hstep[0] = sfh0;
    config->hstep[1] = sfh1;
    config->hstep[2] = sfh1;
    config->vstep[0] = sfv0;
    config->vstep[1] = sfv1;
    config->vstep[2] = sfv1;

    //extend sign bit of phase
    for (int i = 0; i < NUM_CHN; ++i)
    {
        config->hphase[i] <<= sizeof(config->hphase[i]) * 8 - LOCBIT;
        config->hphase[i] >>= sizeof(config->hphase[i]) * 8 - LOCBIT;
        config->vphase[i] <<= sizeof(config->vphase[i]) * 8 - LOCBIT;
        config->vphase[i] >>= sizeof(config->vphase[i]) * 8 - LOCBIT;
        config->vphase1[i] <<= sizeof(config->vphase1[i]) * 8 - LOCBIT;
        config->vphase1[i] >>= sizeof(config->vphase1[i]) * 8 - LOCBIT;
    }

    if (config->step_outside)
    {
        config->hphase[0] = config->hphase[0]<<1;   //phase is 18 bit fraction, so X2
        config->vphase[0] = config->vphase[0]<<1;
        config->vphase1[0] = config->vphase1[0]<<1;

        if (in_fmt == INYUV420)
        {
            config->hphase[1] = config->hphase[1];  //phase is 18 bit fraction, so X2, but Chroma /2
            config->vphase[1] = config->vphase[1] - (1<<(LOCFRACBIT-2));   //phase is 18 bit fraction, so X2, but Chroma /2, vertical phase -0.25
            config->vphase1[1] = config->vphase1[1] - (1<<(LOCFRACBIT-2)); //phase is 18 bit fraction, so X2, but Chroma /2, vertical phase -0.25
        }
        else    //RGB
        {
            config->hphase[1] = config->hphase[0];  //phase is 18 bit fraction, so X2
            config->vphase[1] = config->vphase[0];
            config->vphase1[1] = config->vphase1[0];
        }
    }

 
        config->hphase[2] =config->hphase[1];
        config->vphase[2] = config->vphase[1];
        config->vphase1[2] = config->vphase1[1];

        

    ch0h_fir_coef_ofst = AdscalerModel_CalcTableOffset(config->hstep[0]);
    ch0v_fir_coef_ofst = AdscalerModel_CalcTableOffset(config->vstep[0]);
    ch1h_fir_coef_ofst = AdscalerModel_CalcTableOffset(config->hstep[1]);
    ch1v_fir_coef_ofst = AdscalerModel_CalcTableOffset(config->vstep[1]);

    if (config->input_yuv_format == IMAGEYUV444)
    {
        if (config->lb_mode == 0)
        {
            memcpy(config->nm_horz_coefs[0], lan3coefftab32_full + ch0h_fir_coef_ofst * 64, 64 * 4);
            memcpy(config->nm_vert_coefs[0], lan2coefftab32_full + ch0v_fir_coef_ofst * 32, 32 * 4);
            memcpy(config->nm_horz_coefs[1], lan3coefftab32_full + ch1h_fir_coef_ofst * 64, 64 * 4);
            memcpy(config->nm_vert_coefs[1], lan2coefftab32_full + ch1v_fir_coef_ofst * 32, 32 * 4);
        }
        else
        {
            // 2 line mode
            memcpy(config->nm_horz_coefs[0], lan3coefftab32_full + ch0h_fir_coef_ofst * 64, 64 * 4);
            memcpy(config->nm_vert_coefs[0], linearcoefftab32_4tap, 32 * 4);
            memcpy(config->nm_horz_coefs[1], lan3coefftab32_full + ch1h_fir_coef_ofst * 64, 64 * 4);
            memcpy(config->nm_vert_coefs[1], linearcoefftab32_4tap, 32 * 4);
        }
    }
    else
    {
        if ((config->out_width[0] >= config->width[0] && config->out_height[0] >= config->height[0])
            && config->dovi_en == 0)
        {
            memcpy(config->nm_horz_coefs[0], bicubic8coefftab32 + ch0h_fir_coef_ofst * 64, 64 * 4);
            memcpy(config->nm_vert_coefs[0], bicubic4coefftab32 + ch0v_fir_coef_ofst * 32, 32 * 4);
        }
        else
        {
            memcpy(config->nm_horz_coefs[0], lan3coefftab32_full + ch0h_fir_coef_ofst * 32, 32 * 4);
            memcpy(config->nm_vert_coefs[0], lan2coefftab32_full + ch0v_fir_coef_ofst * 64, 64 * 4);
        }

        memcpy(config->nm_horz_coefs[1], bicubic8coefftab32 + ch1h_fir_coef_ofst * 32, 32 * 4);
        memcpy(config->nm_vert_coefs[1], bicubic4coefftab32 + ch1v_fir_coef_ofst * 32, 32 * 4);
}
        memcpy(config->nm_horz_coefs[2], config->nm_horz_coefs[1], 64 * 4);
        memcpy(config->nm_vert_coefs[2], config->nm_vert_coefs[1], 32 * 4);
        memcpy(config->nm_horz_coefs[3], config->nm_horz_coefs[0], 64 * 4);
        memcpy(config->nm_vert_coefs[3], config->nm_vert_coefs[0], 32 * 4);
        memcpy(config->nm_horz_coefs[4], nncoefftab32_4tap, sizeof(nncoefftab32_4tap));
        memcpy(config->nm_horz_coefs[4], nncoefftab32_8tap, sizeof(nncoefftab32_8tap));

        if (config->dovi_en)
        {
            /* all phase zero for luma : may every original grid pixel unchanged */
            config->hphase[0] = 0;
            config->vphase[0] = 0;
            config->vphase1[0] = 0;
            config->hphase[1] = 0;   // horizon for chroma copy
            config->vphase[1] = -(1<<(LOCFRACBIT-2));   // vertical for chroma fix -0.25
            config->vphase1[1] = -(1<<(LOCFRACBIT-2)); // vertical for chroma fix -0.25
            config->hphase[2] = 0;   // horizon for chroma copy
            config->vphase[2] = -(1<<(LOCFRACBIT-2));   // vertical for chroma fix -0.25
            config->vphase1[2] = -(1<<(LOCFRACBIT-2)); // vertical for chroma fix -0.25

            config->adscaler_mode[0] = VIDEONMMODE; /* normal video mode */

            config->peaking_en[0] = 0;
            config->local_clamp_en = 0;
}
            for (int i = 3; i < NUM_CHN; i++)
            {
                config->width[i] = config->width[0];
                config->height[i] = config->height[0];
                config->out_width[i] = config->out_width[0];
                config->out_height[i] = config->out_height[0];
                config->hphase[i] = config->hphase[0];
                config->vphase[i] = config->vphase[0];
                config->vphase1[i] = config->vphase1[0];
                config->hstep[i] = config->hstep[0];
                config->vstep[i] = config->vstep[0];
            
        
    }
}


int AdscalerModel_CalcTableOffset(int ratio)
{
    int group_sizes[5] = { N_GROUPS_RATIO0, N_GROUPS_RATIO1, N_GROUPS_RATIO2, N_GROUPS_RATIO3, N_GROUPS_RATIO4 };
    int group_size_bits[5] = { 1, 3, 2, 1, 1 };
    int ratio_int, ratio_frac;
    int i;
    int offset;

    // Base on scale ratio, fetch corresponding lanczos/bicubic/nn coefficients group
    ratio_int = ratio >> LOCFRACBIT;
    ratio_frac = (ratio - (ratio_int << LOCFRACBIT)) >> (LOCFRACBIT - group_size_bits[ratio_int]);

    if (ratio_int >= 5)
    {
        ratio_int = 5;
        ratio_frac = 0;
    }

    offset = 0;
    for (i = 0; i < ratio_int; ++i)
    {
        offset += group_sizes[i];
    }
    offset += ratio_frac;

    return offset;
}

void AdscalerModel_SetBitdepth(struct AdscalerModel *model, int bitdepth[])
{
    for (int i = 0; i < NUM_CHN; ++i)
    {
        model->bit_depth[i] = bitdepth[i];
        model->pixel_step[i] = (bitdepth[i] + 7) >> 3;
    }
}

/**
 * @brief ASU 模块核心处理函数：实现输入数据调试转储 + 多通道并行处理
 * @param model      算法模型句柄（包含调试文件指针、通道状态等）
 * @param config     配置结构体（含图像尺寸、格式、调试开关等）
 * @param inbuff0~4  输入图像通道数据（Y/UV/Alpha/标志位等）
 * @param outbuff0~4 输出图像通道数据（处理后结果）
 * @param dump_path  调试文件存储路径
 */
void AdscalerModel_RunDE(struct AdscalerModel *model,struct AdscalerConfig *config, 
                         unsigned char *inbuff0, unsigned char *inbuff1, 
                         unsigned char *inbuff2, unsigned char *inbuff3, 
                         unsigned char *inbuff4, unsigned char *outbuff0, 
                         unsigned char *outbuff1, unsigned char *outbuff2, 
                         unsigned char *outbuff3, unsigned char *outbuff4, 
                         char dump_path[FILENAMEMAX])
{
    // --------------------------- 调试数据转储（输入预处理） ---------------------------
    if (config->dump_debug == 1) { // 仅当调试开关开启时执行
        // 遍历主图像区域（尺寸：width[0] * height[0]）
        for (int i = 0; i < config->width[0] * config->height[0]; ++i) {
            if ((config->input_yuv_format == IMAGEYUV444) && (inbuff3 != NULL)) {
                // 场景1：YUV444 格式且 Alpha 通道有效 
                fprintf(
                    model->fptn[DUMP_ASU_FAY_IN],    // 调试文件：FAY 通道输入
                    g_dump_fmt_list[DUMP_ASU_FAY_IN],// 格式化字符串（如 "%02x %02x %04x"）
                    // 提取通道4的1bit标志位 + 通道3的8bit Alpha值 + 通道0的12bit Y数据
                    *(unsigned char *)(inbuff4 + i) & 0x00000001,  
                    *(unsigned char *)(inbuff3 + i) & 0x000000ff,  
                    *(unsigned short *)(inbuff0 + 2 * i) & 0x000003ff  
                );
            } else {
                // 场景2：非YUV444 或 Alpha通道无效 
                fprintf(
                    model->fptn[DUMP_ASU_FAY_IN],    
                    g_dump_fmt_list[DUMP_ASU_FAY_IN],
                    // 仅保留通道4标志位 + 通道0 Y数据，Alpha填0
                    *(unsigned char *)(inbuff4 + i) & 0x00000001,  
                    0,  
                    *(unsigned short *)(inbuff0 + 2 * i) & 0x000003ff  
                );
            }
        }

        // 遍历 UV 图像区域（尺寸：width[1] * height[1]）
        for (int i = 0; i < config->width[1] * config->height[1]; ++i) {
            fprintf(
                model->fptn[DUMP_ASU_UV_IN],     // 调试文件：UV 通道输入
                g_dump_fmt_list[DUMP_ASU_UV_IN], // 格式化字符串（如 "%04x %04x"）
                // 提取通道1/2的12bit UV数据
                *(unsigned short *)(inbuff1 + 2 * i) & 0x000003ff,  
                *(unsigned short *)(inbuff2 + 2 * i) & 0x000003ff  
            );
        }
    }

    // --------------------------- 多通道并行处理 ---------------------------
    // 通道0：主Y通道处理
    model->chn = 0; // 标记当前处理通道
    AdscalerModel_RunChannel(model, config, inbuff0, outbuff0);

    // 通道1：UV通道1处理
    model->chn = 1;
    AdscalerModel_RunChannel(model, config, inbuff1, outbuff1);

    // 通道2：UV通道2处理
    model->chn = 2;
    AdscalerModel_RunChannel(model, config, inbuff2, outbuff2);


// --------------------- 通道 3 处理（Alpha 逻辑） ---------------------
model->chn = 3;
if (config->alpha_en == 1) {
    AdscalerModel_RunChannel(model, config, inbuff3, outbuff3);
} else if (config->alpha_en == 2) {
    memset(outbuff3, config->glbalpha, 
           config->crop_out_width * config->out_height[0]);
}

// --------------------- 通道 4 处理（标志位逻辑） ---------------------
model->chn = 4;
if (config->flag_en) {
    AdscalerModel_RunChannel(model, config, inbuff4, outbuff4);
}

}




// 多通道处理入口：遍历所有通道，逐个调用单通道处理函数
void AdscalerModel_Run(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config, 
    PixelType *in[3], 
    PixelType *out[3]
) {
    // 遍历每个通道（NUM_CHN 为通道数量宏定义，如 3 表示 Y/U/V）
    for (int chn = 0; chn < NUM_CHN; ++chn) {
        // 设置当前处理通道
        model->chn = chn;
        // 调用单通道处理函数
        AdscalerModel_RunChannel(model, config, in[chn], out[chn]);
    }
}

// 单通道处理核心逻辑：负责单个通道的图像缩放流程
void AdscalerModel_RunChannel(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config, 
    PixelType *in, 
    PixelType *out
) {
    int r; // 循环变量，用于遍历输出图像的行

    // 【优化路径】：若输入输出尺寸完全一致，直接内存拷贝跳过缩放
    if (config->out_width[model->chn] == config->width[model->chn] &&
        config->out_height[model->chn] == config->height[model->chn]) 
    {
        // 计算总字节数：宽度 * 高度 * 每个像素的字节步长
        size_t size = config->width[model->chn] * 
                      config->height[model->chn] * 
                      model->pixel_step[model->chn];
        memcpy(out, in, size);
        //return; // 直接返回，无需后续缩放处理
    }

    // 初始化模型：准备缓存、参数等（如行缓存、插值系数）
    AdscalerModel_Init(model, config, in);

    // 遍历输出图像的每一行（r 表示当前处理的输出行）
    for (r = 0; r < config->out_height[model->chn]; ++r) {
        // 设置当前输出行号
        model->out_y = r;

        // 【调试逻辑】：若匹配调试坐标，可插入断点或日志
        if (model->out_y == config->dbg_y && model->chn == config->dbg_chn) {
            // 空操作，可扩展调试逻辑（如暂停、打印状态）
            r = r; 
        }

        // 刷新垂直方向的行缓存：从输入图像中读取新的行数据
        AdscalerModel_RefreshY(model, config, in);
        // 执行归一化插值：计算水平/垂直方向的插值结果
        AdscalerModel_NormalInterpolation(model, config);
        // 处理当前行：如边缘增强、裁剪等后处理
        AdscalerModel_RunLine(model, config);
        // 将处理后的行数据输出到最终缓冲区
        AdscalerModel_OutputLine(
            out + config->crop_out_width * r * model->pixel_step[model->chn], 
            model->out + config->crop_hor_start, 
            config->crop_out_width, 
            model->pixel_step[model->chn]
        );
    }
}









void AdscalerModel_Init(
    struct AdscalerModel *model,    // 算法模型（包含缓存、参数等状态）
    struct AdscalerConfig *config,  // 配置参数（分辨率、通道等）
    PixelType *in                    // 输入图像数据指针
) {
    // 循环变量：r 遍历窗口行，idx_load_line 计算实际加载的行索引
    int r, idx_load_line;

    // 1. 生成输出坐标（整数+小数部分）
    //    作用：计算缩放后每个输出像素对应的输入图像坐标（用于插值）
    AdscalerModel_GenOutputCoors(model, config);

    // 2. 初始化模型的坐标参数（取第 0 个输出像素的坐标值）
    //    说明：y_intg/x_intg 是坐标整数部分，y_frac/x_frac 是小数部分
    model->y_intg = model->y_intgs[0];  
    model->x_intg = model->x_intgs[0];  
    model->y_frac = model->y_fracs[0];  
    model->x_frac = model->x_fracs[0];  

    // 3. 遍历垂直窗口的每一行（ADSCALER_WIN_H 是垂直窗口高度，如 4 行）
    for (r = 0; r < ADSCALER_WIN_H; ++r) {
        // 计算当前窗口行对应的输入图像行索引
        //    逻辑：以窗口中心为基准，偏移 r 行，确保窗口覆盖有效区域
        idx_load_line = model->y_intg + r - ADSCALER_HALF_WIN_H + 1;

        // 4. 边界保护：防止行索引越界（小于 0 或超过最大行）
        if (idx_load_line < 0) {
            idx_load_line = 0;  // 小于 0 时取第一行
        }
        if (idx_load_line >= config->height[model->chn] - 1) {
            idx_load_line = config->height[model->chn] - 1;  // 超过时取最后一行
        }

        // 5. 从输入图像加载一行数据到模型缓存
        //    参数说明：
        //    - model->pixel[r] + ADSCALER_WIN_W：缓存起始位置（预留窗口宽度用于边缘扩展）
        //    - in + ...：计算输入图像的行偏移（考虑通道、像素步长）
        //    - config->width[model->chn]：行宽度
        //    - model->pixel_step[model->chn]：每个像素的字节步长（如 RGB 占 3 字节）
        AdscalerModel_InputLine(
            model->pixel[r] + ADSCALER_WIN_W, 
            in + config->width[model->chn] * idx_load_line * model->pixel_step[model->chn], 
            config->width[model->chn], 
            model->pixel_step[model->chn]
        );

        // 6. 对加载的行数据进行边缘扩展
        //    作用：处理图像边缘时，通过复制边缘像素避免越界访问
        AdscalerModel_LineBorderExtend(model->pixel[r], config->width[model->chn]);
    }
}


void AdscalerModel_GenOutputCoors(struct AdscalerModel *model, struct AdscalerConfig *config)
{
    __int64 coor_y, coor_x, tmp_loc;
    int i;

    coor_y = config->outfield==0x1 ? config->vphase1[model->chn]:config->vphase[model->chn];
    //coor_y += (1 << (HALFPHASEBIT - 1));
    coor_x = config->hphase[model->chn];
    //coor_x += (1 << (HALFPHASEBIT - 1));

    for (i = 0; i < config->out_width[model->chn]; ++i)
    {
        tmp_loc = coor_x;
        if (tmp_loc < 0)
        {
            tmp_loc = (tmp_loc >> LOCFRACBIT) << LOCFRACBIT;
        }
        if ((tmp_loc >> LOCFRACBIT) >= (config->width[model->chn]-1))
        {
            tmp_loc = (config->width[model->chn]-1);
            tmp_loc <<= LOCFRACBIT;
        }
        model->x_intgs[i] = tmp_loc >> LOCFRACBIT;
        model->x_fracs[i] = tmp_loc - (model->x_intgs[i] << LOCFRACBIT);
        model->x_intgs[i] = CLAMP(model->x_intgs[i],
            -ADSCALER_WIN_W - HORZ_TAB_LEFT, config->width[0] + 2 * ADSCALER_WIN_W - HORZ_TAB_RIGHT);
        coor_x += config->hstep[model->chn];
    }

    for (i = 0; i < config->out_height[model->chn]; ++i)
    {
        tmp_loc = coor_y;
        if (tmp_loc < 0)
        {
            tmp_loc = (tmp_loc >> LOCFRACBIT) << LOCFRACBIT;
        }
        if ((tmp_loc >> LOCFRACBIT) >= (config->height[model->chn]-1))
        {
            tmp_loc = (config->height[model->chn]-1);
            tmp_loc <<= LOCFRACBIT;
        }
        model->y_intgs[i] = tmp_loc >> LOCFRACBIT;
        model->y_fracs[i] = tmp_loc - (model->y_intgs[i] << LOCFRACBIT);
        coor_y += config->vstep[model->chn];
    }
}

void AdscalerModel_RunLine(struct AdscalerModel *model, struct AdscalerConfig *config)
{
    int c;
    int main_out, sub_out, main_sub, bicubic_out, intrp_out;
    int demo_in;

    for (c = 0; c < config->out_width[model->chn]; ++c)
    {
        model->out_x = c;
        if (model->chn == config->dbg_chn && model->out_x == config->dbg_x && model->out_y == config->dbg_y)
        {
            c = c;
        }
        AdscalerModel_RefreshX(model, config);

        if (config->demo_en == 0 ||
            (model->out_y >= config->demo_ver_start && model->out_y <= config->demo_ver_end
             && model->out_x >= config->demo_hor_start && model->out_x <= config->demo_hor_end))
        {
            demo_in = 1;
        }
        else
        {
            demo_in = 0;
        }

        if (config->adscaler_mode[model->chn] == VIDEOADMODE
            && (model->direction->flat_weights != config->wmax_blending || config->dump_debug == 1))
        {
            Adscaler_RegionProcessing(model, config);
            Adsampler_GetMainSamples(model->sampler, model->direction, model->main_region_idx, model->main_samples);
            main_out = AdscalerModel_MainInterpolation(model, config);
            main_out = CLAMP(main_out, 0, (1 << model->bit_depth[model->chn]) - 1);

            if (model->direction->sub_weights != 0 || config->dump_debug == 1)
            {
                Adsampler_GetSubSamples(model->sampler, model->direction, model->sub_region_idx, model->sub_samples);
                sub_out = AdscalerModel_SubInterpolation(model, config);
                sub_out = CLAMP(sub_out, 0, (1 << model->bit_depth[model->chn]) - 1);
                main_sub = (main_out * (config->wmax_blending - model->direction->sub_weights)
                    + sub_out * model->direction->sub_weights + 8) >> 4;
            }
            else
            {
                sub_out = 0;
                main_sub = main_out;
            }

            if (config->demo_en == 1 && demo_in == 0)
            {
                bicubic_out = model->nmintrp_out[c];
                intrp_out = bicubic_out;
            }
            else if (model->direction->flat_weights != 0 || config->dump_debug == 1)
            {
                //bicubic_out = AdscalerModel_NormalInterpolationWin(model, config);
                bicubic_out = model->nmintrp_out[c];
                intrp_out = (main_sub * (config->wmax_blending - model->direction->flat_weights)
                    + bicubic_out * model->direction->flat_weights + 8) >> 4;
            }
            else
            {
                bicubic_out = 0;
                intrp_out = main_sub;
            }
        }
        else
        {
            //intrp_out = AdscalerModel_NormalInterpolationWin(model, config);
            intrp_out = model->nmintrp_out[c];
        }
        intrp_out = CLAMP(intrp_out, 0, (1 << model->bit_depth[model->chn]) - 1);

        if (model->chn == 0)
        {
            model->out[c] = AdscalerModel_Peaking(intrp_out, model, config);
        }
        else if (model->chn < 3)
        {
            model->out[c] = AdscalerModel_UVPeaking(intrp_out, model, config);
        }
        else
        {
            model->out[c] = intrp_out;
        }

        if (config->dump_debug && model->chn == 0
            &&config->adscaler_mode[model->chn] == VIDEOADMODE
            && demo_in == 1)
        {
            AdscalerModel_DumpDebugInfos(model);
            fprintf(model->fptn[DUMP_ASU_MAIN_SUB], g_dump_fmt_list[DUMP_ASU_MAIN_SUB],
                (main_out) & g_dump_mask_list[DUMP_ASU_MAIN_SUB],
                (sub_out) & g_dump_mask_list[DUMP_ASU_MAIN_SUB]);
            fprintf(model->fptn[DUMP_ASU_SUB_WEIGHT], g_dump_fmt_list[DUMP_ASU_SUB_WEIGHT],
                (model->direction->sub_weights) & g_dump_mask_list[DUMP_ASU_SUB_WEIGHT]);
            fprintf(model->fptn[DUMP_ASU_OUT], g_dump_fmt_list[DUMP_ASU_OUT],
                (main_sub) & g_dump_mask_list[DUMP_ASU_OUT]);
            fprintf(model->fptn[DUMP_ASU_FLAT_WEIGHT], g_dump_fmt_list[DUMP_ASU_FLAT_WEIGHT],
                (model->direction->flat_weights) & g_dump_mask_list[DUMP_ASU_FLAT_WEIGHT]);
        }
    }
}

void Adscaler_RegionProcessing(struct AdscalerModel *model, struct AdscalerConfig *config)
{
    int x_p, y_p;

    //if (model->direction->main_dir.idx == 4 || model->direction->main_dir.idx == 6)
    //{
    //  model->x_table = smooth_table;
    //  model->y_table = smooth_table;
    //  model->x_scale = config->main_y_scale;
    //  model->y_scale = config->main_x_scale;
    //}
    //else
    //{
    //  model->x_table = smooth_table;
    //  model->y_table = smooth_table;
    //  model->x_scale = config->main_x_scale;
    //  model->y_scale = config->main_y_scale;
    //}
    //model->sub_x_table = smooth_table;
    //model->sub_y_table = smooth_table;

    if (model->direction->main_dir.idx == 2 || model->direction->main_dir.idx == 6)
    {
        if (model->y_frac >= model->x_frac && model->y_frac < LOC_UNIT - model->x_frac)
        {
            model->main_region_idx = 0;
            x_p = model->x_frac;
            y_p = model->y_frac;
        }
        else if (model->y_frac >= model->x_frac && model->y_frac >= LOC_UNIT - model->x_frac)
        {
            model->main_region_idx = 1;
            x_p = model->x_frac - HALF_LOC_UNIT;
            y_p = model->y_frac - HALF_LOC_UNIT;
        }
        else if (model->y_frac < model->x_frac && model->y_frac >= LOC_UNIT - model->x_frac)
        {
            model->main_region_idx = 2;
            x_p = model->x_frac - LOC_UNIT;
            y_p = model->y_frac;
        }
        else
        {
            model->main_region_idx = 3;
            x_p = model->x_frac - HALF_LOC_UNIT;
            y_p = model->y_frac + HALF_LOC_UNIT;
        }
        model->main_x_phase = x_p + y_p;
        model->main_y_phase = y_p - x_p;
        //model->main_x_phase = (724 * x_p + 724 * y_p) >> 10;
        //model->main_y_phase = (724 * y_p - 724 * x_p) >> 10;
    }
    else
    {
        if (model->x_frac < HALF_LOC_UNIT && model->y_frac < HALF_LOC_UNIT)
        {
            model->main_region_idx = 0;
        }
        else if (model->x_frac >= HALF_LOC_UNIT && model->y_frac < HALF_LOC_UNIT)
        {
            model->main_region_idx = 1;
        }
        else if (model->x_frac < HALF_LOC_UNIT && model->y_frac >= HALF_LOC_UNIT)
        {
            model->main_region_idx = 2;
        }
        else
        {
            model->main_region_idx = 3;
        }
        model->main_x_phase = model->x_frac;
        model->main_y_phase = model->y_frac;
    }
    model->main_x_phase = CLAMP(model->main_x_phase, 0, LOC_UNIT - 1);
    model->main_y_phase = CLAMP(model->main_y_phase, 0, LOC_UNIT - 1);

    if (model->direction->sub_dir.idx == 1)
    {
        if (model->y_frac >= (model->x_frac >> 1) + HALF_LOC_UNIT)
        {
            model->sub_region_idx = 0;
            x_p = model->x_frac;
            y_p = model->y_frac - HALF_LOC_UNIT;
        }
        else if (model->y_frac < (model->x_frac >> 1))
        {
            model->sub_region_idx = 2;
            x_p = model->x_frac;
            y_p = model->y_frac + HALF_LOC_UNIT;
        }
        else
        {
            model->sub_region_idx = 1;
            x_p = model->x_frac;
            y_p = model->y_frac;
        }
        //model->sub_x_phase = (683 * x_p + 341 * y_p) >> 10;
        //model->sub_y_phase = (y_p << 1) - x_p;
        //model->sub_x_phase = (916 * x_p + 458 * y_p) >> 10;
        //model->sub_y_phase = (-458 * x_p + 916 * y_p) >> 10;
        model->sub_x_phase = (819 * x_p + 410 * y_p) >> 10;
        model->sub_y_phase = (y_p << 1) - x_p;
    }
    else if (model->direction->sub_dir.idx == 3)
    {
        if (model->y_frac >= (model->x_frac << 1))
        {
            model->sub_region_idx = 0;
            x_p = model->x_frac;
            y_p = model->y_frac;
        }
        else if (model->y_frac < (model->x_frac << 1) - LOC_UNIT)
        {
            model->sub_region_idx = 2;
            x_p = model->x_frac - LOC_UNIT;
            y_p = model->y_frac;
        }
        else
        {
            model->sub_region_idx = 1;
            x_p = model->x_frac - HALF_LOC_UNIT;
            y_p = model->y_frac;
        }
        //model->sub_x_phase = ((341 * x_p + 683 * y_p) >> 10) + 87399;
        //model->sub_y_phase = y_p - (x_p << 1);
        //model->sub_x_phase = ((458 * x_p + 916 * y_p) >> 10);
        //model->sub_y_phase = ((-916 * x_p + 458 * y_p) >> 10);
        model->sub_x_phase = ((410 * x_p + 819 * y_p) >> 10);
        model->sub_y_phase = y_p - (x_p << 1);
    }
    else if (model->direction->sub_dir.idx == 5)
    {
        if (model->y_frac < LOC_UNIT - (model->x_frac << 1))
        {
            model->sub_region_idx = 0;
            x_p = model->x_frac - HALF_LOC_UNIT;
            y_p = model->y_frac;
        }
        else if (model->y_frac > (LOC_UNIT << 1) - (model->x_frac << 1))
        {
            model->sub_region_idx = 2;
            x_p = model->x_frac - LOC_UNIT - HALF_LOC_UNIT;
            y_p = model->y_frac;
        }
        else
        {
            model->sub_region_idx = 1;
            x_p = model->x_frac - LOC_UNIT;
            y_p = model->y_frac;
        }
        //model->sub_x_phase = (683 * y_p >> 10) - (341 * x_p >> 10);
        //model->sub_y_phase = -y_p - (x_p << 1);
        //model->sub_x_phase = (-458 * x_p + 916 * y_p) >> 10;
        //model->sub_y_phase = (-916 * x_p - 458 * y_p) >> 10;
        model->sub_x_phase = (819 * y_p - 410 * x_p) >> 10;
        model->sub_y_phase = -y_p - (x_p << 1);
    }
    else
    {
        if (model->y_frac <= HALF_LOC_UNIT - (model->x_frac >> 1))
        {
            model->sub_region_idx = 0;
            x_p = model->x_frac - LOC_UNIT;
            y_p = model->y_frac;
        }
        else if (model->y_frac > LOC_UNIT - (model->x_frac >> 1))
        {
            model->sub_region_idx = 2;
            x_p = model->x_frac - LOC_UNIT;
            y_p = model->y_frac - LOC_UNIT;
        }
        else
        {
            model->sub_region_idx = 1;
            x_p = model->x_frac - LOC_UNIT;
            y_p = model->y_frac - HALF_LOC_UNIT;
        }
        //model->sub_x_phase = (341 * y_p >> 10) - (683 * x_p >> 10) + 87399;
        //model->sub_y_phase = -x_p - (y_p << 1);
        //model->sub_x_phase = (-916 * x_p + 458 * y_p) >> 10;
        //model->sub_y_phase = (-458 * x_p - 916 * y_p) >> 10;
        model->sub_x_phase = (410 * y_p - 819 * x_p) >> 10;
        model->sub_y_phase = -x_p - (y_p << 1);
    }
    model->sub_x_phase = CLAMP(model->sub_x_phase, 0, LOC_UNIT - 1);
    model->sub_y_phase = CLAMP(model->sub_y_phase, 0, LOC_UNIT - 1);
}


//989
// ====================== 主插值分支（核心业务逻辑） ======================
int AdscalerModel_MainInterpolation(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int cumSumH, cumSum;       // 累加器：水平/总加权和
    int main_out;              // 主插值输出结果
    int x_phase, y_phase;      // 水平/垂直相位（小数部分）
    int coefs_idy, coefs_idx;  // 垂直/水平系数索引
    int r, c;                  // 循环变量
    int w_c[4], w_r[4];        // 垂直/水平权重数组

    // 1. 计算垂直/水平相位（小数部分转换为系数索引的前置值）
    y_phase = model->main_y_phase >> (LOCFRACBIT - ADSCALER_NPHASE_BITS);
    x_phase = model->main_x_phase >> (LOCFRACBIT - ADSCALER_NPHASE_BITS);

    // 2. 计算系数索引（左移2位：每个相位对应4个连续系数）
    coefs_idy = y_phase << 2;
    coefs_idx = x_phase << 2;

    // 3. 系数索引溢出保护（防止越界）
    coefs_idy = coefs_idy >= ADSCALER_TABLE_LENGTH ? ADSCALER_TABLE_LENGTH - 4 : coefs_idy;
    coefs_idx = coefs_idx >= ADSCALER_TABLE_LENGTH ? ADSCALER_TABLE_LENGTH - 4 : coefs_idx;

    // 4. 【注释部分】方向插值分支（如对角线方向，当前代码走默认水平/垂直）
    // if (model->direction->main_dir.idx == 2 || model->direction->main_dir.idx == 6) {
    //     for (r = 0; r < 4; ++r) {
    //         w_r[r] = diagtab[coefs_idy + r];
    //         w_c[r] = diagtab[coefs_idx + r];
    //     }
    // } else {
    // 默认分支：使用水平/垂直权重表
    for (r = 0; r < 4; ++r) {
        w_r[r] = horztab[coefs_idy + r];
        w_c[r] = horztab[coefs_idx + r];
    }
    // }

    // 5. 二维加权求和（先水平后垂直）
    cumSum = 0;
    for (r = -1; r < 3; ++r) {
        cumSumH = 0;
        for (c = -1; c < 3; ++c) {
            // 水平方向加权和
            cumSumH += w_c[c + 1] * model->main_samples[r + 1][c + 1];
        }
        // 符号右移：加权和缩放（带四舍五入）
        cumSumH = SIGNED_RSHIFT(cumSumH, ADSCALER_COEFS_HALF_UNIT, ADSCALER_COEFS_BITS);
        // 垂直方向加权和
        cumSum += w_r[r + 1] * cumSumH;
    }

    // 6. 负值截断（确保输出非负）
    cumSum = cumSum < 0 ? 0 : cumSum;

    // 7. 最终结果缩放（右移 + 半精度偏移）
    main_out = (cumSum + ADSCALER_COEFS_HALF_UNIT) >> ADSCALER_COEFS_BITS;

    return main_out;
}

// ====================== 次插值分支（辅助业务逻辑） ======================
int AdscalerModel_SubInterpolation(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int cumSumH, cumSum;       // 累加器
    int sub_out;               // 次插值输出结果
    int x_phase, y_phase;      // 相位
    int coefs_idy, coefs_idx;  // 系数索引
    int r, c;                  // 循环变量
    int w_c[4], w_r[4];        // 权重数组

    // 1. 计算相位与系数索引（逻辑同主插值）
    y_phase = model->sub_y_phase >> (LOCFRACBIT - ADSCALER_NPHASE_BITS);
    x_phase = model->sub_x_phase >> (LOCFRACBIT - ADSCALER_NPHASE_BITS);
    coefs_idy = y_phase << 2;
    coefs_idx = x_phase << 2;
    coefs_idy = coefs_idy >= ADSCALER_TABLE_LENGTH ? ADSCALER_TABLE_LENGTH - 4 : coefs_idy;
    coefs_idx = coefs_idx >= ADSCALER_TABLE_LENGTH ? ADSCALER_TABLE_LENGTH - 4 : coefs_idx;

    // 2. 获取权重（使用锐利斜率表 + 水平表）
    for (r = 0; r < 4; ++r) {
        w_r[r] = slope_sharp_tab[coefs_idy + r];
        w_c[r] = horztab[coefs_idx + r];
    }

    // 3. 二维加权求和（逻辑同主插值）
    cumSum = 0;
    for (r = -1; r < 3; ++r) {
        cumSumH = 0;
        for (c = -1; c < 3; ++c) {
            cumSumH += w_c[c + 1] * model->sub_samples[r + 1][c + 1];
        }
        cumSumH = SIGNED_RSHIFT(cumSumH, ADSCALER_COEFS_HALF_UNIT, ADSCALER_COEFS_BITS);
        cumSum += w_r[r + 1] * cumSumH;
    }

    // 4. 负值截断与结果缩放
    cumSum = cumSum < 0 ? 0 : cumSum;
    sub_out = (cumSum + ADSCALER_COEFS_HALF_UNIT) >> ADSCALER_COEFS_BITS;

    return sub_out;
}

//1086
// ====================== 主插值函数：协调垂直与水平插值流程 ======================
void AdscalerModel_NormalInterpolation(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int c, i;                  // 循环变量：c=列，i=水平窗口索引
    int sx;                    // 水平方向系数索引
    int cumsum;                // 加权和累加器
    char htab[HORZ_TAB];       // 水平插值系数表
    char *htab_ptr;            // 系数表指针（支持负索引）
    short sign;                // 符号标记（正/负）
    int hfilt_max;             // 水平滤波器最大值（防止溢出）
    //htab_ptr=htab-HORZ_TAB_LEFT;
    AdscalerPixel *win_ptr;    // 垂直插值结果的窗口指针

    // 1. 先执行垂直方向插值
    AdscalerModel_NormalInterpolationV(model, config);

    // 2. 初始化水平系数表指针（支持负索引，如 HORZ_TAB_LEFT 为负）
    htab_ptr = htab - HORZ_TAB_LEFT;

    // 3. 计算水平滤波器最大值（根据像素位深动态调整）
    hfilt_max = (1 << (model->bit_depth[model->chn] + 6)) - 1;

    // 4. 遍历所有输出列，执行水平插值
    for (c = 0; c < config->out_width[model->chn]; ++c) {
        // 调试断点：匹配指定通道、行、列时触发
        if (model->chn == config->dbg_chn && 
            c == config->dbg_x && 
            model->out_y == config->dbg_y) 
        {
            c = c;  // 空操作，仅用于调试断点
        }

        // 垂直插值结果的窗口起始地址（+ADSCALER_WIN_W 是边缘扩展偏移）
        win_ptr = model->v_nmintrp_out + model->x_intgs[c] + ADSCALER_WIN_W;
        // 计算水平方向小数部分对应的系数索引
        sx = model->x_fracs[c] >> HALFPHASEBIT;
        // 从配置中获取水平插值系数并填充到 htab
        Adscaler_GetNormalScalerCoefs(htab, HORZ_TAB, sx, config->nm_horz_coefs[model->chn]);

        // 5. 累加水平窗口内所有列的加权像素值
        cumsum = 0;
        for (i = HORZ_TAB_LEFT; i <= HORZ_TAB_RIGHT; ++i) {
            cumsum += htab_ptr[i] * win_ptr[i];
        }

        // 6. 符号与溢出处理
        sign = (cumsum >= 0) ? 1 : -1;
        if (sign == 1) {
            // 正值：四舍五入 + 溢出保护
            cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
            cumsum = (cumsum > hfilt_max) ? hfilt_max : cumsum;
        } else {
            // 负值特殊处理（置0）
            cumsum = 0;
        }

        // 7. 水平插值结果：右移缩放后存储
        model->nmintrp_out[c] = (unsigned short)(cumsum >> SCALER_COEFS_BITS);

        // 调试数据转储：启用时写入文件
        if (config->dump_debug == 1 && model->chn < 4) {
		if(model->chn==3){sign=sign;}
            fprintf(
                model->fptn[DUMP_ASU_NMH + model->chn], 
                g_dump_fmt_list[DUMP_ASU_NMH + model->chn], 
                (model->nmintrp_out[c]) & g_dump_mask_list[DUMP_ASU_NMH + model->chn]
            );
        }
    }
}

// ====================== 垂直方向归一化插值 ======================
void AdscalerModel_NormalInterpolationV(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int r, c;                  // 循环变量：r=垂直窗口行，c=列
    int lb_width,sy;                    // 垂直方向系数索引
    int cumsum;                // 加权和累加器
    char vtab[VERT_TAB];     // 垂直插值系数表
    char *vtab_ptr;            // 系数表指针（支持负索引）
    short sign;                // 符号标记
    int vfilt_max;             // 垂直滤波器最大值

    // 初始化系数表指针（支持负索引）
    vtab_ptr = vtab - VERT_TAB_UP;

    // 计算垂直方向系数索引
    sy = model->y_frac >> HALFPHASEBIT;
    // 获取垂直插值系数
    Adscaler_GetNormalScalerCoefs(vtab, VERT_TAB, sy, config->nm_vert_coefs[model->chn]);

    // 计算垂直滤波器最大值
    vfilt_max = (1 << (model->bit_depth[model->chn] + 8)) - 1;
	lb_width=config->width[model->chn] + 2 * ADSCALER_WIN_W;
    // 遍历所有列，执行垂直插值
    for (c = 0; c <lb_width ; ++c) {
        // 调试断点
        if (model->chn == config->dbg_chn && 
            model->out_y == config->dbg_y && 
            c == config->dbg_x + ADSCALER_WIN_W) 
        {
            c = c;
        }

        // 累加垂直窗口内所有行的加权像素值
        cumsum = 0;
        for (r = VERT_TAB_UP; r <= VERT_TAB_DOWN; ++r) {
            cumsum += vtab_ptr[r] * model->pixel[r - ADSCALER_WIN_UP][c];
        }

        // 符号与溢出处理
        sign = (cumsum >= 0) ? 1 : -1;
        cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
        cumsum = (cumsum > vfilt_max) ? vfilt_max : cumsum;

        // 垂直插值结果存储
        model->v_nmintrp_out[c] = sign * (cumsum >> SCALER_COEFS_BITS);

        // 调试数据转储
        if (config->dump_debug == 1 && model->chn < 4) {
	  for(c=ADSCALER_WIN_W;	c<ADSCALER_WIN_W+config->width[model->chn];++c){
            fprintf(
                model->fptn[DUMP_ASU_NMV + model->chn], 
                g_dump_fmt_list[DUMP_ASU_NMV + model->chn], 
                (model->v_nmintrp_out[c]) & g_dump_mask_list[DUMP_ASU_NMV + model->chn]
            );}
        }
    }
}

// 水平方向归一化插值函数
// 参数：
// - out：输出像素数组，存储水平插值后的结果
// - pixel_in：输入像素数组（通常为垂直插值后的结果）
// - x_intgs：水平方向整数坐标数组
// - x_fracs：水平方向小数坐标数组
// - coefs：插值系数表
// - out_width：输出宽度
// - bitdepth：像素位深
void AdscalerModel_NormalInterpolationH(
    AdscalerPixel *out, 
    AdscalerPixel *pixel_in, 
    int *x_intgs, 
    int *x_fracs, 
    unsigned int *coefs, 
    int out_width, 
    int bitdepth
) {
    int c, i;              // 循环变量，c 遍历列，i 遍历水平窗口索引
    int sx;                // 用于计算水平插值系数索引的中间变量
    int cumsum;            // 加权和累加器
    char htab[HORZ_TAB];   // 水平插值系数表，存储当前计算所需的水平插值系数
    char *htab_ptr;        // 系数表指针，用于支持负索引访问
    int filt_max;          // 滤波器输出最大值，防止溢出
    short sign;            // 符号标记，区分正负值
    AdscalerPixel *win_ptr;// 指向当前处理窗口起始位置的指针

    // 初始化系数表指针，使其支持负索引（HORZ_TAB_LEFT 可能为负）
    htab_ptr = htab - HORZ_TAB_LEFT;

    // 计算滤波器最大值，根据位深动态调整，防止溢出
    filt_max = (1 << (bitdepth + 8)) - 1;

    // 遍历输出的每一列
    for (c = 0; c < out_width; ++c) {
        // 获取当前列对应的输入窗口起始位置，ADSCALER_HALF_WIN_W 是窗口半宽偏移
        win_ptr = pixel_in + x_intgs[c] + ADSCALER_HALF_WIN_W;
        // 计算水平方向小数部分对应的插值系数索引
        sx = x_fracs[c] >> HALFPHASEBIT;
        // 从预计算的系数表中获取当前所需的水平插值系数，填充到 htab
        Adscaler_GetNormalScalerCoefs(htab, HORZ_TAB, sx, coefs);

        // 初始化累加器为 0
        cumsum = 0;
        // 遍历水平窗口内的所有索引，计算加权和
        for (i = HORZ_TAB_LEFT; i <= HORZ_TAB_RIGHT; ++i) {
            cumsum += htab_ptr[i] * win_ptr[i];
        }

        // 符号判断，确定累加和的正负
        sign = (cumsum >= 0) ? 1 : -1;
        if (sign == 1) {
            // 正值处理：先取绝对值，加上半精度偏移（四舍五入）
            cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
            // 溢出保护：若超过最大值则截断为最大值
            cumsum = (cumsum > filt_max) ? filt_max : cumsum;
        } else {
            // 负值处理：直接置 0（根据业务需求，也可做其他处理）
            cumsum = 0;
        }

        // 最终水平插值结果：符号乘以（累加和右移缩放后的值）
        out[c] = sign * (cumsum >> SCALER_COEFS_BITS);
    }
}

void Adscaler_GetNormalScalerCoefs(char coefs[], int tabs, int sx, unsigned int coefs_table[])
{
    int num_table_unit, offset;
    char *coefs_ptr;
    int i;
    unsigned int table_unit;

    // 计算需要从coefs_table中获取的unsigned int数量，tabs >> 2 等价于 tabs / 4
    num_table_unit = tabs >> 2;
    // 计算偏移量，确定在coefs_table中的起始位置
    offset = sx * num_table_unit;

    // 初始化指向coefs数组的指针
    coefs_ptr = coefs;

    for (i = 0; i < num_table_unit; ++i)
    {
        // 从coefs_table中获取当前的unsigned int单元
        table_unit = coefs_table[offset + i];

        // 提取table_unit的第0个字节（低8位），并将其符号扩展后存入coefs数组
        *coefs_ptr++ = (((table_unit & 0xff)) << 24) >> 24;
        // 提取table_unit的第1个字节（8 - 15位），并将其符号扩展后存入coefs数组
        *coefs_ptr++ = ((((table_unit & 0xff00) >> 8)) << 24) >> 24;
        // 提取table_unit的第2个字节（16 - 23位），并将其符号扩展后存入coefs数组
        *coefs_ptr++ = ((((table_unit & 0xff0000) >> 16)) << 24) >> 24;
        // 提取table_unit的第3个字节（24 - 31位），并将其符号扩展后存入coefs数组
        *coefs_ptr++ = ((((table_unit & 0xff000000) >> 24)) << 24) >> 24;
    }
}


// ====================== 亮度通道峰值处理 ======================
int AdscalerModel_Peaking(int intrp_out, struct AdscalerModel *model, struct AdscalerConfig *config) {
    int local_min2x1[2], local_max2x1[2]; // 中间插值后的局部最小/最大值
    int w;                                // 插值权重
    int local_min, local_max, local_range; // 最终局部最小、最大、范围
    int peak_gain, local_avg, peak_in, peak_out; // 峰值增益、局部平均、输入/输出峰值
    int result;                           // 最终结果
    int half_range_limit;                 // 半范围限制值

    // 1. 线性模式直接返回插值结果
    if (config->lb_mode == 1) {
        return intrp_out;
    }

    // 2. 计算半范围限制
    half_range_limit = config->peak_range_limit[model->chn] >> 1;

    // 3. 水平方向插值（计算中间局部极值）
    w = model->x_frac >> HALFPHASEBIT;
    local_min2x1[0] = (model->local_min2x2[0][0] * (SCALERPHASE - w) + model->local_min2x2[0][1] * w) >> SCALERPHASEBIT;
    local_min2x1[1] = (model->local_min2x2[1][0] * (SCALERPHASE - w) + model->local_min2x2[1][1] * w) >> SCALERPHASEBIT;
    local_max2x1[0] = (model->local_max2x2[0][0] * (SCALERPHASE - w) + model->local_max2x2[0][1] * w) >> SCALERPHASEBIT;
    local_max2x1[1] = (model->local_max2x2[1][0] * (SCALERPHASE - w) + model->local_max2x2[1][1] * w) >> SCALERPHASEBIT;

    // 4. 垂直方向插值（得到最终局部极值）
    w = model->y_frac >> HALFPHASEBIT;
    local_min = (local_min2x1[0] * (SCALERPHASE - w) + local_min2x1[1] * w) >> SCALERPHASEBIT;
    local_max = (local_max2x1[0] * (SCALERPHASE - w) + local_max2x1[1] * w) >> SCALERPHASEBIT;

    // 5. 局部钳位（可选）
    if (config->local_clamp_en == 1) {
        peak_in = CLAMP(intrp_out, local_min, local_max);
    } else {
        peak_in = intrp_out;
    }

    // 6. 峰值增强（若使能）
    if (config->peaking_en[model->chn] == 1) {
        local_avg = (local_min + local_max + 1) >> 1;
        local_range = local_max - local_min;

        // 范围限制：若局部范围超过配置值，缩放到半范围限制内
        if (local_range > config->peak_range_limit[model->chn]) {
            //local_min = local_avg - half_range_limit;
            //local_max = local_avg + half_range_limit;
		local_min=local_max-config->peak_range_limit[model->chn];
        }

        // 超出局部范围则直接返回，否则计算峰值增益
        if (peak_in < local_min || peak_in > local_max) {
            result = peak_in;
        } else {
            local_avg = (local_min + local_max + 1) >> 1;
            if (peak_in <= local_avg) {
                // 低于平均值：计算负增益（降低亮度）
                peak_gain = (local_avg - peak_in) * (peak_in - local_min);
                peak_gain = peak_gain * config->peak_m[model->chn];
                peak_gain = (peak_gain + 512) >> 10;
                peak_out = peak_in - peak_gain;
            } else {
                // 高于平均值：计算正增益（提升亮度）
                peak_gain = (peak_in - local_avg) * (local_max - peak_in);
                peak_gain = peak_gain * config->peak_m[model->chn];
                peak_gain = (peak_gain + 512) >> 10;
                peak_out = peak_in + peak_gain;
            }

            // 最终结果钳位 + 权重混合
            peak_out = CLAMP(peak_out, 0, (1 << model->bit_depth[model->chn]) - 1);
            result = peak_out * model->peak_weights + peak_in * ((1 << config->th_strong_edge) - model->peak_weights);
            result = (result + (1 << (config->th_strong_edge - 1))) >> config->th_strong_edge;
            result = CLAMP(result, local_min, local_max);
        }

    // 7. 调试数据转储（仅通道0）
    if (config->dump_debug == 1 && model->chn == 0) {
        fprintf(model->fptn[DUMP_ASU_EDGE_STRENGTH], g_dump_fmt_list[DUMP_ASU_EDGE_STRENGTH],
                (model->edge_strength) & g_dump_mask_list[DUMP_ASU_EDGE_STRENGTH]);
        fprintf(model->fptn[DUMP_ASU_PEAK_WEIGHT], g_dump_fmt_list[DUMP_ASU_PEAK_WEIGHT],
                (model->peak_weights) & g_dump_mask_list[DUMP_ASU_PEAK_WEIGHT]);
        fprintf(model->fptn[DUMP_ASU_PEAK_OUT], g_dump_fmt_list[DUMP_ASU_PEAK_OUT],
                (result) & g_dump_mask_list[DUMP_ASU_PEAK_OUT]);
    }

    } else {
        // 峰值增强未使能，直接返回输入
        result = peak_in;
    }



    return result;
}

// ====================== 色度通道峰值处理 ======================
int AdscalerModel_UVPeaking(int intrp_out, struct AdscalerModel *model, struct AdscalerConfig *config) {
    int local_min2x1[2], local_max2x1[2]; // 中间插值后的局部最小/最大值
    int w;                                // 插值权重
    int local_min, local_max, local_range; // 最终局部最小、最大、范围
    int peak_gain, local_avg, peak_in, peak_out; // 峰值增益、局部平均、输入/输出峰值
    int result;                           // 最终结果
    int half_range_limit;                 // 半范围限制值

    // 1. 线性模式直接返回插值结果
    if (config->lb_mode == 1) {
        return intrp_out;
    }

    // 2. 计算半范围限制
    half_range_limit = config->peak_range_limit[model->chn] >> 1;

    // 3. 水平方向插值（计算中间局部极值）
    w = model->x_frac >> HALFPHASEBIT;
    local_min2x1[0] = (model->local_min2x2[0][0] * (SCALERPHASE - w) + model->local_min2x2[0][1] * w) >> SCALERPHASEBIT;
    local_min2x1[1] = (model->local_min2x2[1][0] * (SCALERPHASE - w) + model->local_min2x2[1][1] * w) >> SCALERPHASEBIT;
    local_max2x1[0] = (model->local_max2x2[0][0] * (SCALERPHASE - w) + model->local_max2x2[0][1] * w) >> SCALERPHASEBIT;
    local_max2x1[1] = (model->local_max2x2[1][0] * (SCALERPHASE - w) + model->local_max2x2[1][1] * w) >> SCALERPHASEBIT;

    // 4. 垂直方向插值（得到最终局部极值）
    w = model->y_frac >> HALFPHASEBIT;
    local_min = (local_min2x1[0] * (SCALERPHASE - w) + local_min2x1[1] * w) >> SCALERPHASEBIT;
    local_max = (local_max2x1[0] * (SCALERPHASE - w) + local_max2x1[1] * w) >> SCALERPHASEBIT;


	//zengxinxin 1384

    // 5. 局部钳位（可选）
    if (config->local_clamp_en == 1) {
        peak_in = CLAMP(intrp_out, local_min, local_max);
    } else {
        peak_in = intrp_out;
    }

    // 6. 峰值增强（若使能）
    if (config->peaking_en[model->chn] == 1) {
        local_avg = (local_min + local_max + 1) >> 1;
        local_range = local_max - local_min;

        // 范围限制：若局部范围超过配置值，缩放到半范围限制内
        if (local_range > config->peak_range_limit[model->chn]) {
            local_min = local_avg - half_range_limit;
            local_max = local_avg + half_range_limit;
        }

        // 色度白电平处理：确保局部极值不低于/高于色度白
        if (peak_in >= config->chroma_white) {
            local_min = local_min < config->chroma_white ? config->chroma_white : local_min;
        } else {
            local_max = local_max > config->chroma_white ? config->chroma_white : local_max;
        }

        // 超出局部范围则直接返回，否则计算峰值增益
        if (peak_in < local_min || peak_in > local_max) {
            result = peak_in;
        } else {
            local_avg = (local_min + local_max + 1) >> 1;
		local_range=local_max-local_min;
            if (peak_in <= local_avg) {
                // 低于平均值：计算负增益（降低色度）
                peak_gain = (local_avg - peak_in) * (peak_in - local_min);
                peak_gain = peak_gain * config->peak_m[model->chn];
                peak_gain = (peak_gain + 128) >> 8;
                peak_out = peak_in - peak_gain;
            } else {
                // 高于平均值：计算正增益（提升色度）
                peak_gain = (peak_in - local_avg) * (local_max - peak_in);
                peak_gain = peak_gain * config->peak_m[model->chn];
                peak_gain = (peak_gain + 128) >> 8;
                peak_out = peak_in + peak_gain;
            }

            // 最终结果钳位
            result = CLAMP(peak_out, local_min, local_max);
        }
    } else {
        // 峰值增强未使能，直接返回输入
        result = peak_in;
    }

    // 7. 调试数据转储（通道1、2）
    if (config->dump_debug == 1 && model->chn < 3) {
        fprintf(model->fptn[DUMP_ASU_U_PEAK + model->chn - 1], g_dump_fmt_list[DUMP_ASU_U_PEAK + model->chn - 1],
                (result) & g_dump_mask_list[DUMP_ASU_U_PEAK + model->chn - 1]);
    }

    return result;
}

// ====================== 梯度计算：边缘强度检测 ======================
void AdscalerModel_CalcGradient(struct AdscalerModel *model, AdscalerPixel *win[4]) {
    int avg_dx2, avg_dy2; // 水平、垂直方向的二次梯度平均值

    // 1. 计算水平方向二次梯度（相邻像素差的平方和）
    avg_dx2 = POW2(ABS(win[0][-1], win[0][0])) + POW2(ABS(win[0][0], win[0][1])) + POW2(ABS(win[0][1], win[0][2])) +
              POW2(ABS(win[1][-1], win[1][0])) + POW2(ABS(win[1][0], win[1][1])) + POW2(ABS(win[1][1], win[1][2])) +
              POW2(ABS(win[2][-1], win[2][0])) + POW2(ABS(win[2][0], win[2][1])) + POW2(ABS(win[2][1], win[2][2]));
    // 加权平均（7个边缘 + 中心像素，右移6位等价于除以64）
    avg_dx2 = (avg_dx2 * 7 + POW2(ABS(win[1][0], win[1][1])) + 32) >> 6;

    // 2. 计算垂直方向二次梯度（相邻像素差的平方和）
    avg_dy2 = POW2(ABS(win[0][-1], win[1][-1])) + POW2(ABS(win[0][0], win[1][0])) + POW2(ABS(win[0][1], win[1][1])) +
              POW2(ABS(win[1][-1], win[2][-1])) + POW2(ABS(win[1][0], win[2][0])) + POW2(ABS(win[1][1], win[2][1])) +
              POW2(ABS(win[2][-1], win[3][-1])) + POW2(ABS(win[2][0], win[3][0])) + POW2(ABS(win[2][1], win[3][1]));
    // 加权平均（7个边缘 + 中心像素，右移6位等价于除以64）
    avg_dy2 = (avg_dy2 * 7 + POW2(ABS(win[1][0], win[2][0])) + 32) >> 6;

    // 3. 最终边缘强度 = 水平 + 垂直梯度的平均
    model->edge_strength = (avg_dx2 + avg_dy2 + 1) >> 1;
}

//1474
// ------------------------------
// 垂直方向状态刷新：处理行缓存滚动与状态更新
// ------------------------------
void AdscalerModel_RefreshY(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config, 
    PixelType *in
) {
    // 计算需要滚动的行数（垂直方向坐标差）
    model->num_rolllines = model->y_intgs[model->out_y] - model->y_intg;
    // 更新垂直方向坐标（整数+小数部分）
    model->y_intg = model->y_intgs[model->out_y];
    model->y_frac = model->y_fracs[model->out_y];
    model->x_intg = model->x_intgs[0];
    model->x_frac = model->x_fracs[0];

    // 需要滚动行缓存时，调用滚动函数
    if (model->num_rolllines > 0) {
        AdscalerModel_RollLineBuffer(model, config, in);
    }

    // 刷新垂直方向状态（如边缘检测、插值参数）
    AdscalerModel_RefreshStatusY(model, config);
}

// ------------------------------
// 垂直方向状态刷新核心逻辑
// ------------------------------
void AdscalerModel_RefreshStatusY(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int i, j, m, n;

    // 遍历垂直窗口的每一行（ADSCALER_WIN_H 为窗口高度）
    for (i = 0; i < ADSCALER_WIN_H; ++i) {
        // 计算当前窗口行对应的像素缓存地址（考虑水平偏移）
        model->win_pixel[i] = model->pixel[i] + model->x_intg + ADSCALER_WIN_W;
    }

    // 【模式判断】：VIDEOADMODE 下启用双向插值与采样
    if (config->adscaler_mode[model->chn] == VIDEOADMODE) {
        BiDirection_Run(model->direction, model->win_pixel);      // 边缘方向检测
        Adsampler_Sampling(model->sampler, model->direction, model->win_pixel); // 方向采样
    }

    // 【模式判断】：非 UIMODE 下计算梯度与峰值权重
    if (config->adscaler_mode[model->chn] != UIMODE) {
        AdscalerModel_CalcGradient(model, model->win_pixel);      // 计算图像梯度
        // 计算峰值权重（带移位优化的乘法与裁剪）
        model->peak_weights = (model->edge_strength * config->peak_weights_strength + 256) >> 9;
        // 权重裁剪（防止溢出）
        model->peak_weights = model->peak_weights > (1 << config->th_strong_edge) ? 
            (1 << config->th_strong_edge) : model->peak_weights;
    }

    // 遍历局部窗口，计算 min/max 用于边缘检测
    for (m = 0; m < 2; ++m) {
        for (n = -1; n < 1; ++n) {
            // 初始化局部 min/max 为当前窗口像素值
            model->local_min2x2[m][n + 1] = model->win_pixel[m][n];
            model->local_max2x2[m][n + 1] = model->win_pixel[m][n];

            // 遍历 3x3 窗口，更新局部 min/max
            for (i = m; i < m + 3; ++i) {
                for (j = n; j < n + 3; ++j) {
                    // 更新局部最小值
                    if (model->win_pixel[i][j] < model->local_min2x2[m][n + 1]) {
                        model->local_min2x2[m][n + 1] = model->win_pixel[i][j];
                    }
                    // 更新局部最大值
                    if (model->win_pixel[i][j] > model->local_max2x2[m][n + 1]) {
                        model->local_max2x2[m][n + 1] = model->win_pixel[i][j];
                    }
                }
            }
        }
    }
}

// ------------------------------
// 水平方向状态刷新：处理列偏移与状态更新
// ------------------------------
void AdscalerModel_RefreshX(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int i;
    // 计算水平方向步长（列偏移量）
    model->x_stride = model->x_intgs[model->out_x] - model->x_intg;
    // 更新水平方向坐标（整数+小数部分）
    model->x_intg = model->x_intgs[model->out_x];
    model->x_frac = model->x_fracs[model->out_x];

    // 需要更新列偏移时，调整窗口像素地址
    if (model->x_stride > 0) {
        for (i = 0; i < ADSCALER_WIN_H; ++i) {
            model->win_pixel[i] += model->x_stride;
        }
    }

    // 刷新水平方向状态（如局部 min/max 更新）
    AdscalerModel_RefreshStatusX(model, config);
}

// ------------------------------
// 水平方向状态刷新核心逻辑
// ------------------------------
void AdscalerModel_RefreshStatusX(
    struct AdscalerModel *model, 
    struct AdscalerConfig *config
) {
    int i, j, m, n;

    // 【步长判断】：x_stride == 1 时简化更新逻辑
    if (model->x_stride == 1) {
        for (m = 0; m < 2; ++m) {
            // 平移局部 min/max 缓存
            model->local_min2x2[m][0] = model->local_min2x2[m][1];
            model->local_max2x2[m][0] = model->local_max2x2[m][1];
            // 初始化新列的 min/max 为当前窗口像素值
            model->local_min2x2[m][1] = model->win_pixel[m][0];
            model->local_max2x2[m][1] = model->win_pixel[m][0];

            // 遍历 3x3 窗口，更新局部 min/max
            for (i = m; i < m + 3; ++i) {
                for (j = 0; j < 3; ++j) {
                    // 更新局部最小值
                    if (model->win_pixel[i][j] < model->local_min2x2[m][1]) {
                        model->local_min2x2[m][1] = model->win_pixel[i][j];
                    }
                    // 更新局部最大值
                    if (model->win_pixel[i][j] > model->local_max2x2[m][1]) {
                        model->local_max2x2[m][1] = model->win_pixel[i][j];
                    }
                }
            }
        }
    } else {
        // 【通用逻辑】：x_stride != 1 时重新计算局部 min/max
        for (m = 0; m < 2; ++m) {
            for (n = -1; n < 1; ++n) {
                // 初始化局部 min/max 为当前窗口像素值
                model->local_min2x2[m][n + 1] = model->win_pixel[m][n];
                model->local_max2x2[m][n + 1] = model->win_pixel[m][n];

                // 遍历 3x3 窗口，更新局部 min/max
                for (i = m; i < m + 3; ++i) {
                    for (j = n; j < n + 3; ++j) {
                        // 更新局部最小值
                        if (model->win_pixel[i][j] < model->local_min2x2[m][n + 1]) {
                            model->local_min2x2[m][n + 1] = model->win_pixel[i][j];
                        }
                        // 更新局部最大值
                        if (model->win_pixel[i][j] > model->local_max2x2[m][n + 1]) {
                            model->local_max2x2[m][n + 1] = model->win_pixel[i][j];
                        }
                    }
                }
            }
        }
    }

    // 【模式判断】：VIDEOADMODE 下启用双向插值与采样
    if (config->adscaler_mode[model->chn] == VIDEOADMODE) {
        BiDirection_Run(model->direction, model->win_pixel);      // 边缘方向检测
        Adsampler_Sampling(model->sampler, model->direction, model->win_pixel); // 方向采样
    }

    // 【模式判断】：非 UIMODE 下计算梯度与峰值权重
    if (config->adscaler_mode[model->chn] != UIMODE) {
        AdscalerModel_CalcGradient(model, model->win_pixel);      // 计算图像梯度
        // 计算峰值权重（带移位优化的乘法与裁剪）
        model->peak_weights = (model->edge_strength * config->peak_weights_strength + 256) >> 9;
        // 权重裁剪（防止溢出）
        model->peak_weights = model->peak_weights > (1 << config->th_strong_edge) ? 
            (1 << config->th_strong_edge) : model->peak_weights;
    }
}



void AdscalerModel_RollLineBuffer(struct AdscalerModel *model, struct AdscalerConfig *config, PixelType *in)
{
    int roll, r, idx_load_line;
    AdscalerPixel *tmp_pix;
    AdscalerPixel *tmp_h_norm_out;

    if (model->num_rolllines >= ADSCALER_WIN_H)
    {
        model->num_rolllines = ADSCALER_WIN_H;
    }
    else
    {
        for (roll = 0; roll < model->num_rolllines; ++roll)
        {
            tmp_pix = model->pixel[0];
            tmp_h_norm_out = model->h_nmintrp_out[0];
            for (r = 0; r < ADSCALER_WIN_H - 1; ++r)
            {
                model->pixel[r] = model->pixel[r + 1];
                model->h_nmintrp_out[r] = model->h_nmintrp_out[r + 1];
            }
            model->pixel[ADSCALER_LASTROW] = tmp_pix;
            model->h_nmintrp_out[ADSCALER_LASTROW] = tmp_h_norm_out;
        }
}
        // load num_rolllines input line.
        for (roll = 0; roll < model->num_rolllines; ++roll)
        {
            idx_load_line = model->y_intg + ADSCALER_HALF_WIN_H - roll;
            if (idx_load_line < 0)
                idx_load_line = 0;
            if (idx_load_line >= config->height[model->chn] - 1)
                idx_load_line = config->height[model->chn] - 1;

            AdscalerModel_InputLine(model->pixel[ADSCALER_LASTROW - roll] + ADSCALER_WIN_W,
                in + config->width[model->chn] * idx_load_line * model->pixel_step[model->chn],
                config->width[model->chn], model->pixel_step[model->chn]);

            AdscalerModel_LineBorderExtend(model->pixel[ADSCALER_LASTROW - roll], config->width[model->chn]);
        }
    
}

void AdscalerModel_LineBorderExtend(AdscalerPixel *line, int width)
{
    int i;
    CHECK_PTR(line);
    for (i = 0; i < ADSCALER_WIN_W; ++i)
    {
        line[i] = line[ADSCALER_WIN_W];
        line[width + ADSCALER_WIN_W + i] = line[width + ADSCALER_WIN_W - 1];
    }
dbg_error:
    return;
}

void AdscalerModel_InputLine(AdscalerPixel *dst, PixelType *src, int length, int pixel_step)
{
    int i;

    if (pixel_step == 1)
    {
        for (i = 0; i < length; ++i)
        {
            *dst = *(unsigned char *)src;
            ++dst;
            src += pixel_step;
        }
    }
    else if (pixel_step == 2)
    {
        for (i = 0; i < length; ++i)
        {
            *dst = *(unsigned short *)src;
            ++dst;
            src += pixel_step;
        }
    }
    else
    {
        for (i = 0; i < length; ++i)
        {
            *dst = *(unsigned int *)src;
            ++dst;
            src += pixel_step;
        }
    }
}

void AdscalerModel_OutputLine(PixelType *dst, AdscalerPixel *src, int length, int pixel_step)
{
    int i;
    unsigned int *dst_4bytes;
    unsigned short *dst_2bytes;
    unsigned char *dst_1bytes;

    if (pixel_step == 1)
    {
        dst_1bytes = (unsigned char *)dst;
        for (i = 0; i < length; ++i)
        {
            *dst_1bytes = (unsigned char)*src;
            ++src;
            ++dst_1bytes;
        }
    }
    else if (pixel_step == 2)
    {
        dst_2bytes = (unsigned short *)dst;
        for (i = 0; i < length; ++i)
        {
            *dst_2bytes = (unsigned short)*src;
            ++src;
            ++dst_2bytes;
        }
    }
    else
    {
        dst_4bytes = (unsigned int *)dst;
        for (i = 0; i < length; ++i)
        {
            *dst_4bytes = (unsigned int)*src;
            ++src;
            ++dst_4bytes;
        }
    }
}

void AdscalerModel_DumpDebugInfos(struct AdscalerModel *model)
{
    fprintf(model->fptn[DUMP_ASU_DIFF4X6], g_dump_fmt_list[DUMP_ASU_DIFF4X6],
        (model->direction->diff4x6[0]) & g_dump_mask_list[DUMP_ASU_DIFF4X6],
        (model->direction->diff4x6[1]) & g_dump_mask_list[DUMP_ASU_DIFF4X6],
        (model->direction->diff4x6[2]) & g_dump_mask_list[DUMP_ASU_DIFF4X6],
        (model->direction->diff4x6[3]) & g_dump_mask_list[DUMP_ASU_DIFF4X6]);
    fprintf(model->fptn[DUMP_ASU_DIR6], g_dump_fmt_list[DUMP_ASU_DIR6],
        (model->direction->dir6[0].idx) & g_dump_mask_list[DUMP_ASU_DIR6],
        (model->direction->dir6[1].idx) & g_dump_mask_list[DUMP_ASU_DIR6],
        (model->direction->dir6[2].idx) & g_dump_mask_list[DUMP_ASU_DIR6],
        (model->direction->dir6[3].idx) & g_dump_mask_list[DUMP_ASU_DIR6]);
    fprintf(model->fptn[DUMP_ASU_DIFF4X4], g_dump_fmt_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[0]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[1]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[2]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[3]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[4]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[5]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[6]) & g_dump_mask_list[DUMP_ASU_DIFF4X4],
        (model->direction->diff4x4[7]) & g_dump_mask_list[DUMP_ASU_DIFF4X4]);
    fprintf(model->fptn[DUMP_ASU_DIR], g_dump_fmt_list[DUMP_ASU_DIR],
        (model->direction->main_dir.idx) & g_dump_mask_list[DUMP_ASU_DIR],
        (model->direction->sub_dir.idx) & g_dump_mask_list[DUMP_ASU_DIR]);
    fprintf(model->fptn[DUMP_ASU_MAIN_ROT], g_dump_fmt_list[DUMP_ASU_MAIN_ROT],
        (model->main_x_phase) & g_dump_mask_list[DUMP_ASU_MAIN_ROT],
        (model->main_y_phase) & g_dump_mask_list[DUMP_ASU_MAIN_ROT],
        (model->main_samples[0][0] & 0x000003ff),
        (model->main_samples[0][1] & 0x000003ff),
        (model->main_samples[0][2] & 0x000003ff),
        (model->main_samples[0][3] & 0x000003ff),
        (model->main_samples[1][0] & 0x000003ff),
        (model->main_samples[1][1] & 0x000003ff),
        (model->main_samples[1][2] & 0x000003ff),
        (model->main_samples[1][3] & 0x000003ff),
        (model->main_samples[2][0] & 0x000003ff),
        (model->main_samples[2][1] & 0x000003ff),
        (model->main_samples[2][2] & 0x000003ff),
        (model->main_samples[2][3] & 0x000003ff),
        (model->main_samples[3][0] & 0x000003ff),
        (model->main_samples[3][1] & 0x000003ff),
        (model->main_samples[3][2] & 0x000003ff),
        (model->main_samples[3][3] & 0x000003ff));
    fprintf(model->fptn[DUMP_ASU_SUB_ROT], g_dump_fmt_list[DUMP_ASU_SUB_ROT],
        (model->sub_x_phase) & g_dump_mask_list[DUMP_ASU_SUB_ROT],
        (model->sub_y_phase) & g_dump_mask_list[DUMP_ASU_SUB_ROT],
        (model->sub_samples[0][0] & 0x000003ff),
        (model->sub_samples[0][1] & 0x000003ff),
        (model->sub_samples[0][2] & 0x000003ff),
        (model->sub_samples[0][3] & 0x000003ff),
        (model->sub_samples[1][0] & 0x000003ff),
        (model->sub_samples[1][1] & 0x000003ff),
        (model->sub_samples[1][2] & 0x000003ff),
        (model->sub_samples[1][3] & 0x000003ff),
        (model->sub_samples[2][0] & 0x000003ff),
        (model->sub_samples[2][1] & 0x000003ff),
        (model->sub_samples[2][2] & 0x000003ff),
        (model->sub_samples[2][3] & 0x000003ff),
        (model->sub_samples[3][0] & 0x000003ff),
        (model->sub_samples[3][1] & 0x000003ff),
        (model->sub_samples[3][2] & 0x000003ff),
        (model->sub_samples[3][3] & 0x000003ff));
}

