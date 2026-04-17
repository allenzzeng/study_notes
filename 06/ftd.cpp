#include "stdafx.h"
#include "ftd_model.h"
#include "hsv_lut.h"
#include <math.h>

#define HBit 0x3FFF
/*note:  Fixed-point Scaling
from hsv_lut.h:
m_Sin_Theta[31]={0,18,36...481,496,512}         sin 30=0.5*1024
m_Cos_Theta[31]={1024,1024,1023...904,896,887}        cos 0=1*1024
SQRT_LUT[256]={0,16,23...254,255,255}      (0,255)input:1  sqrt(1)=1*16    output:16  >>4=1 ;  (256,1023)input: 256  >>2 sqrt(64)   output:128 >>3=16
ATAN_ANGLE[65]={0,0,1...44,44,45}        arctan 0/64 =0  arctan (64/64) =45
Delta_LUT[256]={4096,2048,1365...16,16,16}       [0]=4096/1  [1]=4096/2      
atan_LUT[16]={11520,6801,3593...4,2,1}        [0]=arctan(1)*256   [1]=arctan(0.5)*256   cordic
*/
struct FTDModel *FTDModel_New(struct FTDConfig *config)
{
    int i;
    struct FTDModel *model;

    PCMEM_ALLOC(model, struct FTDModel);
    for (i = 0; i < FTD_WIN_H; ++i)
    {
        PCMEM_ALLOC_N(model->pixel[i], config->width + 2 * FTD_HALF_WIN_W, FTDPixel);
    }
    PCMEM_ALLOC_N(model->win_pixel, FTD_WIN_H, FTDPixel *);
    model->win_pixel += FTD_HALF_WIN_H;
    PCMEM_ALLOC_N(model->out, config->width + 2 * FTD_HALF_WIN_W, FTDPixel);

dbg_error:
    return model;
}

void FTDModel_Free(struct FTDModel **model)
{
    int i;

    CHECK_PTR(model);
    CHECK_PTR(*model);
    for (i = 0; i < FTD_WIN_H; ++i)
    {
        PCMEM_FREE((*model)->pixel[i]);
    }
    (*model)->win_pixel -= FTD_HALF_WIN_H;
    PCMEM_FREE((*model)->win_pixel);
    PCMEM_FREE((*model)->out);
    PCMEM_FREE(*model);

dbg_error:
    return;
}

void FTDModel_SetBitdepth(struct FTDModel *model, int bitdepth)
{
    model->bit_depth = bitdepth;
}

void FTDModel_Run(struct FTDModel *model, struct FTDConfig *config, PixelType *in[3], PixelType *out[3],
                  unsigned char *skinmap, int idx_frame)
{
    int size;
    config->bit_depth = model->bit_depth;
    size = config->width * config->height;
    PixelType *pb, *pr;
    unsigned short *skin_h;
    MALLOC_AND_INIT(pb, size, PixelType);
    MALLOC_AND_INIT(pr, size, PixelType);
    MALLOC_AND_INIT(skin_h, size, unsigned short);
    PixelType *H, *S, *V;
    MALLOC_AND_INIT(H, size, PixelType);
    MALLOC_AND_INIT(S, size, PixelType);
    MALLOC_AND_INIT(V, size, PixelType);

    if (config->version == 0) //the old version
    {
        c2p(in[1], in[2], pb, pr, size, config->bit_depth);

			ftd_alg(pb, pr, size, config->atanlut, config->sqrtlut, skin_h, skinmap, config->bit_depth,
            config->hue_red_thr, config->hue_red_slope, config->hue_yellow_thr, config->hue_yellow_slope,
            config->chr_low_thr, config->chr_low_slope, config->chr_high_thr, config->chr_high_slope);
    }
    else if (config->version == 1) //the new version
    {
        FTDModel_YUV2HSV(in[0], in[1], in[2], H, S, V, size, config->Delta_LUT);
        FTDModel_CalMain(H, S, V, config->HSV_H_LOW, config->HSV_H_HIGH, config->HSV_S_LOW, config->HSV_S_HIGH,
                         config->HSV_V_LOW, config->HSV_V_HIGH, config->HSV_H, config->HSV_R, size, skinmap, config->atan_LUT);
        printf("ftd info update %d.\n", config->result[idx_frame].info_update);
        if (config->ai_ftd_enable && config->result[idx_frame].info_update)
        {
            FTDModel_ModifyHSVRange(H, S, V, idx_frame, config);
        }
    }
	else if (config->version == 2)
	{
	    int size = config->width * config->height;
	    PixelType *Y  = in[0];
	    PixelType *Cb = in[1];
	    PixelType *Cr = in[2];
	
	    // ======================
	    // 定点格式统一使用 Q12 (放大 2^12 = 4096 倍)
	    // 所有小数全部变成整数运算
	    // ======================
	    const int muCr        = 150;                  // Cr 肤色均值 (8bit 整数)
	    const int muCb        = 110;                  // Cb 肤色均值 (8bit 整数)
	    const int invC00_Q12   = 10; // 逆协方差 Q12 定点化 (int)(0.0025f * 4096)
	    const int invC11_Q12   = 10;
	    const int invC01_Q12   = 0;
	
	    for (int i = 0; i < size; i++)
	    {
	        // ======================
	        // 1. 10/12bit 转 8bit (右移2位)
	        // ======================
	        int cr = (Cr[i] >> 2);
	        int cb = (Cb[i] >> 2);
	
	        // ======================
	        // 2. 计算与肤色中心的差值
	        // ======================
	        int dcr = cr - muCr;
	        int dcb = cb - muCb;
	
	        // ======================
	        // 3. 计算马氏距离 d (结果为 Q12)
	        // 使用 int64_t 避免乘法溢出
	        // ======================
	        int64_t term1 = (int64_t)dcr * dcr * invC00_Q12;
	        int64_t term2 = (int64_t)dcb * dcb * invC11_Q12;
	        int64_t term3 = 2 * (int64_t)dcr * dcb * invC01_Q12;
	
	        // 总和就是 d，定点 Q12
	        int64_t d_Q12 = term1 + term2 + term3;
	
	        // ======================
	        // 4. 【核心】exp(-d) 用 LUT + 线性插值 全整数计算
	        // ======================
	        int score_Q15 = fast_exp_neg(d_Q12);
	
	        // ======================
	        // 5. 输出 val = 255 * (1 - exp(-d))
	        // score_Q15 是 0~32767 对应 0~1
	        // ======================
	        int val = 255 - ((score_Q15 * 255) >> 15);
	
	        // 限幅 0~255
	        if (val < 0) val = 0;
	        if (val > 255) val = 255;
	
	        skinmap[i] = (PixelType)val;
	    }
	}
    else
    {
        config->version = config->version;
    }

    memcpy(out[0], in[0], size * sizeof(PixelType));
    memcpy(out[1], in[1], size * sizeof(PixelType));
    memcpy(out[2], in[2], size * sizeof(PixelType));

    free(pb);
    free(pr);
    free(skin_h);
    free(H);
    free(S);
    free(V);
    return;
}

// ======================
// 函数功能：计算 exp(-d)
// 输入：d_Q12   Q12 定点数 (d=0~4 对应 0~16384)
// 输出：Q15     0~32767 对应 0~1
// ======================
int fast_exp_neg(int64_t d_Q12)
{
    // ======================
    // 公式 1：输入范围限制
    // d > 4 时 exp(-d) ≈ 0，直接返回 0
    // ======================
    if (d_Q12 > 16384)
        return 0;

    // ======================
    // 公式 2：映射到 LUT 索引
    // LUT 对应 d ∈ [0, 4]
    // idx = (d_Q12 * 256) / 16384 = d_Q12 / 64
    // ======================
    int idx = (int)(d_Q12 *256)>> 12>>2;

    // ======================
    // 公式 3：取小数部分（用于插值）
    // frac = d_Q12 % 64  (0~63)
    // ======================
    int frac = (int)(d_Q12 & 0x3F);

    // ======================
    // 公式 4：LUT 查表公式 (你要的计算公式)
    // 这是生成 LUT 的唯一公式：
    //
    // LUT[i] = round( exp( -4.0f * i / 255.0f ) * 32767.0f )
    //
    // i：0~255
    // 4.0f：d 的最大范围
    // 32767：Q15 放大倍数
    // ======================

    // ======================
    // 公式 5：线性插值（全整数）
    // y = y0 + (y1 - y0) * frac / 64
    // ======================
    int y0 = exp_LUT(idx);      // 查表整数点
    int y1 = exp_LUT(idx + 1);  // 查表下一个点
    int res = y0 + ((y1 - y0) * frac >> 6);

    return res;
}


int exp_LUT(int idx){
	// 生成 LUT 的公式（只此一个）
	for (int i = 0; i < 256; i++)
	{
	    // 真实 d 值：0 ~ 4
	    float d = 4.0f * i / 255.0f;
	
	    // 计算 exp(-d)
	    float e = exp(-d);
	
	    // 转为 Q15 定点整数 (0~32767)
	    //LUT[i] = (int)(e * 32767.0f);
	
		if(i==idx)return (int)(e * 32767.0f);
	}
}


int ftd_alg(PixelType *pb, PixelType *pr, int size, unsigned char alut[65], unsigned char slut[256],
            unsigned short *skin_h, unsigned char *skinmap, unsigned int bitdepth,
            unsigned int hue_red_thr, unsigned int hue_red_slope, unsigned int hue_yellow_thr, unsigned int hue_yellow_slope,
            unsigned int chr_low_thr, unsigned int chr_low_slope, unsigned int chr_high_thr, unsigned int chr_high_slope)
{
    int i;
    int abs_cb, abs_cr;
    int sign_cb, sign_cr;
    int tmpcb, tmpcr;
    unsigned char *skin_c;
    unsigned int cmet, ment;
    unsigned short skin_h_map, skin_c_map;
    unsigned int bx;

    PCMEM_ALLOC_N(skin_c, size, unsigned char);
    bx = bitdepth - 8;

    //skin h
    for (i = 0; i < size; i++)
    {
        abs_cb = abs(*(pb + i)) >> bx; //8bit unsigned
        abs_cr = abs(*(pr + i)) >> bx; //8bit unsigned
        sign_cb = *(pb + i) >= 0 ? 1 : -1;
        sign_cr = *(pr + i) >= 0 ? 1 : -1;
        tmpcb = sign_cb * abs_cb;
        tmpcr = sign_cr * abs_cr;

        if (tmpcb > 0)
        {
            if (tmpcr == 0)
            {
                *(skin_h + i) = 0;
            }
            else if (tmpcr > 0)
            {
                if (abs_cb > abs_cr)
                {
                    *(skin_h + i) = alut[((abs_cr << 6) + (abs_cb >> 1)) / abs_cb];
                }
                else
                {
                    *(skin_h + i) = 90 - alut[((abs_cb << 6) + (abs_cr >> 1)) / abs_cr];
                }
            }
            else //tmpcr<0
            {
                if (abs_cb < abs_cr)
                {
                    *(skin_h + i) = 270 + alut[((abs_cb << 6) + (abs_cr >> 1)) / abs_cr];
                }
                else
                {
                    *(skin_h + i) = 360 - alut[((abs_cr << 6) + (abs_cb >> 1)) / abs_cb];
                }
            }
        }
        else if (tmpcb<0)
        {
            if (tmpcr == 0)
            {
                *(skin_h + i) = 180;
            }
            else if (tmpcr>0)
            {
                if (abs_cb < abs_cr)
                {
                    *(skin_h + i) = 90 + alut[((abs_cb << 6) + (abs_cr >> 1)) / abs_cr];
                }
                else
                {
                    *(skin_h + i) = 180 - alut[((abs_cb << 6) + (abs_cr >> 1)) / abs_cr];
                }
            }
            else //tmpcr<0
            {
                if (abs_cb > abs_cr)
                {
                    *(skin_h + i) = 180 + alut[((abs_cb << 6) + (abs_cr >> 1)) / abs_cb];
                }
                else
                {
                    *(skin_h + i) = 270 - alut[((abs_cr << 6) + (abs_cb >> 1)) / abs_cr];
                }
            }
        }
        else //cb == 0
        {
            if (tmpcr == 0)
            {
                *(skin_h + i) = 0;
            }
            else if (tmpcr > 0)
            {
                *(skin_h + i) = 90;
            }
            else //tmpcr<0
            {
                *(skin_h + i) = 270;
            }
        }
    }

    //skin c
    for (i = 0; i < size; i++)
    {
        abs_cb = abs(*(pb + i)) >> bx; //8bit unsigned
        abs_cr = abs(*(pr + i)) >> bx; //8bit unsigned
        cmet = abs_cb*abs_cb + abs_cr*abs_cr;

        if (cmet <= 255)
        {
            ment = cmet;
        }
        else if (cmet <= 1023)
        {
            ment = cmet >> 2;
        }
        else if (cmet <= 4095)
        {
            ment = cmet >> 4;
        }
        else if (cmet <= 16383)
        {
            ment = cmet >> 6;
        }
        else //if (cmet<=65535)
        {
            ment = cmet >> 8;
        }

        if (cmet <= 255)
        {
            *(skin_c + i) = slut[ment] >> 4;
        }
        else if (cmet <= 1023)
        {
            *(skin_c + i) = slut[ment] >> 3;
        }
        else if (cmet <= 4095)
        {
            *(skin_c + i) = slut[ment] >> 2;
        }
        else if (cmet <= 16383)
        {
            *(skin_c + i) = slut[ment] >> 1;
        }
        else if (cmet <= 65535)
        {
            *(skin_c + i) = slut[ment];
        }
        else
        {
            *(skin_c + i) = 255;
        }
    }

    //skin_map
    for (i = 0; i < size; i++)
    {
        if (*(skin_h + i) <= hue_red_thr)
        {
            skin_h_map = hue_red_slope*(hue_red_thr - (*(skin_h + i)));
        }
        else if (*(skin_h + i) >= hue_yellow_thr)
        {
            skin_h_map = hue_yellow_slope*(*(skin_h + i) - hue_yellow_thr);
        }
        else
        {
            skin_h_map = 0;
        }

        if (*(skin_c + i) <= chr_low_thr)
        {
            skin_c_map = chr_low_slope*(chr_low_thr - (*(skin_c + i)));
        }
        else if (*(skin_c + i) >= chr_high_thr)
        {
            skin_c_map = chr_high_slope*(*(skin_c + i) - chr_high_thr);
        }
        else
        {
            skin_c_map = 0;
        }

        skin_h_map = max(0, min(255, skin_h_map));
        skin_c_map = max(0, min(255, skin_c_map));

        if (skin_h_map >= skin_c_map)
        {
            *(skinmap + i) = (unsigned char)skin_h_map;
        }
        else
        {
            *(skinmap + i) = (unsigned char)skin_c_map;
        }
    }

dbg_error:
    PCMEM_FREE(skin_c);
    return 0;
}

int c2p(PixelType *cb, PixelType *cr, PixelType *pb, PixelType *pr, int size, unsigned int bitdepth)
{
    int i;
    int offset;
    offset = 1 << (bitdepth - 1);
    for (i = 0; i < size; i++)
    {
        *(pb + i) = *(cb + i) - offset;
        *(pr + i) = *(cr + i) - offset;
    }
    return 0;
}

void FTDModel_RunY(struct FTDModel *model, struct FTDConfig *config, PixelType *in, PixelType *out)
{
    int r;
    FTDModel_Init(model, config, in);
    for (r = 0; r < config->height; ++r)
    {
        model->y = r;
        FTDModel_RunLine(model, config);
        FTDModel_OutputLine(out + config->width * r, model->out + FTD_HALF_WIN_W, config->width);
        FTDModel_RefreshY(model, config, in);
    }
}

void FTDModel_Init(struct FTDModel *model, struct FTDConfig *config, PixelType *in)
{
    int i, r, idx_load_line;
    for (r = 0; r < FTD_WIN_H; ++r)
    {
        idx_load_line = r - FTD_HALF_WIN_H;
        if (idx_load_line < 0)
            idx_load_line = 0;
        if (idx_load_line >= config->height - 1)
            idx_load_line = config->height - 1;
        FTDModel_InputLine(model->pixel[r] + FTD_HALF_WIN_W,
            in + config->width * idx_load_line, config->width);
        FTDModel_LineBorderExtend(model->pixel[r], config->width);
    }
    for (i = -FTD_HALF_WIN_H; i <= FTD_HALF_WIN_H; ++i)
    {
        model->win_pixel[i] = model->pixel[i + FTD_HALF_WIN_H] + FTD_HALF_WIN_W;
    }
    //FTDModel_RefreshStatusY(model, config);
}

void FTDModel_LineBorderExtend(FTDPixel *line, int width)
{
    int i;
    CHECK_PTR(line);
    for (i = 0; i < FTD_HALF_WIN_W; ++i)
    {
        line[i] = line[FTD_HALF_WIN_W];
        line[width + FTD_HALF_WIN_W + i] = line[width + FTD_HALF_WIN_W - 1];
    }
dbg_error:
    return;
}

void FTDModel_RunLine(struct FTDModel *model, struct FTDConfig *config)
{
    int c;
    for (c = 0; c < config->width; ++c)
    {
        model->x = c;
        FTDModel_RefreshX(model, config);
    }
}

void FTDModel_RefreshY(struct FTDModel *model, struct FTDConfig *config, PixelType *in)
{
    int i;
    FTDModel_RollLineBuffer(model, config, in);
    for (i = -FTD_HALF_WIN_H; i <= FTD_HALF_WIN_H; ++i)
    {
        model->win_pixel[i] = model->pixel[i + FTD_HALF_WIN_H] + FTD_HALF_WIN_W;
    }
}

void FTDModel_RefreshX(struct FTDModel *model, struct FTDConfig *config)
{
    int i;
    for (i = -FTD_HALF_WIN_H; i <= FTD_HALF_WIN_H; i++)
    {
        ++model->win_pixel[i];
    }
}

void FTDModel_RollLineBuffer(struct FTDModel *model, struct FTDConfig *config, PixelType *in)
{
    int r, idx_load_line;
    FTDPixel *tmp_pix = model->pixel[0];
    for (r = 0; r < FTD_WIN_H - 1; ++r)
    {
        model->pixel[r] = model->pixel[r + 1];
    }
    model->pixel[FTD_WIN_H - 1] = tmp_pix;

    // load 1 input line.
    idx_load_line = model->y + FTD_HALF_WIN_H + 1;
    if (idx_load_line < 0)
        idx_load_line = 0;
    if (idx_load_line >= config->height - 1)
        idx_load_line = config->height - 1;
    FTDModel_InputLine(model->pixel[FTD_WIN_H - 1] + FTD_HALF_WIN_W,
        in + config->width * idx_load_line, config->width);
    FTDModel_LineBorderExtend(model->pixel[FTD_WIN_H - 1], config->width);
}

void FTDModel_RunUV(struct FTDModel *model, struct FTDConfig *config, PixelType *in, PixelType *out)
{
    int r;
    FTDModel_UVInit(model, config, in);
    for (r = 0; r < config->height; ++r)
    {
        model->y = r;
        FTDModel_RunUVLine(model, config);
        // copy to output buffer
        FTDModel_OutputLine(out + config->cwidth * r,
            model->out, config->cwidth);
        FTDModel_UVRefresh(model, config, in);
    }
}

void FTDModel_UVInit(struct FTDModel *model, struct FTDConfig *config, PixelType *in)
{
    int i, r, idx_load_line;
    for (r = 0; r < FTD_WIN_CH; ++r)
    {
        idx_load_line = r - FTD_HALF_WIN_CH;
        if (idx_load_line < 0)
            idx_load_line = 0;
        if (idx_load_line >= config->cheight - 1)
            idx_load_line = config->cheight - 1;
        FTDModel_InputLine(model->pixel[r] + FTD_HALF_WIN_W,
            in + config->cwidth * idx_load_line, config->cwidth);
        FTDModel_LineBorderExtend(model->pixel[r], config->cwidth);
    }
    for (i = -FTD_HALF_WIN_CH; i <= FTD_HALF_WIN_CH; ++i)
    {
        model->win_pixel[i] = model->pixel[i + FTD_HALF_WIN_CH] + FTD_HALF_WIN_W;
    }
}

void FTDModel_RunUVLine(struct FTDModel *model, struct FTDConfig *config)
{
    int c;
    for (c = 0; c < config->cwidth; ++c)
    {
        model->x = c;
        model->out[c] = model->win_pixel[0][0];
        FTDModel_UVRefreshX(model, config);
    }
}

void FTDModel_UVRefresh(struct FTDModel *model, struct FTDConfig *config, PixelType *in)
{
    int i;
    FTDModel_UVRollLineBuffer(model, config, in);
    for (i = -FTD_HALF_WIN_CH; i <= FTD_HALF_WIN_CH; ++i)
    {
        model->win_pixel[i] = model->pixel[i + FTD_HALF_WIN_CH] + FTD_HALF_WIN_W;
    }
}

void FTDModel_UVRefreshX(struct FTDModel *model, struct FTDConfig *config)
{
    int i;
    for (i = -FTD_HALF_WIN_CH; i <= FTD_HALF_WIN_CH; i++)
    {
        ++model->win_pixel[i];
    }
}

void FTDModel_UVRollLineBuffer(struct FTDModel *model, struct FTDConfig *config, PixelType *in)
{
    int r, idx_load_line;
    FTDPixel *tmp_pix = model->pixel[0];
    for (r = 0; r < FTD_WIN_CH - 1; ++r)
    {
        model->pixel[r] = model->pixel[r + 1];
    }
    model->pixel[FTD_WIN_CH - 1] = tmp_pix;

    // load 1 input line.
    idx_load_line = model->y + FTD_HALF_WIN_CH + 1;
    if (idx_load_line < 0)
        idx_load_line = 0;
    if (idx_load_line >= config->cheight - 1)
        idx_load_line = config->cheight - 1;
    FTDModel_InputLine(model->pixel[FTD_WIN_CH - 1] + FTD_HALF_WIN_W,
        in + config->cwidth * idx_load_line, config->cwidth);
    FTDModel_LineBorderExtend(model->pixel[r], config->cwidth);
}

void FTDModel_InputLine(FTDPixel *dst, PixelType *src, int length)
{
    int i;
    for (i = 0; i < length; ++i)
    {
        *dst++ = (FTDPixel)*src++;
    }
}

void FTDModel_OutputLine(PixelType *dst, FTDPixel *src, int length)
{
    int i;
    for (i = 0; i < length; ++i)
    {
        *dst++ = (PixelType)*src++;
    }
}

void FTDModel_Skinmap_To_Heatmap(unsigned char *heatmap[3], unsigned char *skinmap, int size)
{
    int i;
    for (i = 0; i < size; i++)
    {
        *(heatmap[0] + i) = 255 - *(skinmap + i);
        *(heatmap[1] + i) = 0;
        *(heatmap[2] + i) = 80;
    }
}

void FTDModel_DebugAI(int idx_frame, PixelType *out[3], struct FTDConfig *config)
{
    int tmp_left;
    int tmp_top;
    int tmp_right;
    int tmp_bottom;
    int w, h;
    int width, height;
    int i;
    width = config->width;
    height = config->height;
    for (i = 0; i < config->face_num; i++)
    {
        tmp_left = config->result[idx_frame].left[i];
        tmp_top = config->result[idx_frame].top[i];
        tmp_right = config->result[idx_frame].right[i];
        tmp_bottom = config->result[idx_frame].bottom[i];

        // draw the line for ai face detection debug
        for (w = tmp_left; w < tmp_right; w++)
        {
            *(out[0] + (tmp_top * width) + w) = 255;
            *(out[1] + (tmp_top * width) + w) = 255;
            *(out[2] + (tmp_top * width) + w) = 255;
        }
        for (w = tmp_left; w < tmp_right; w++)
        {
            *(out[0] + (tmp_bottom * width) + w) = 255;
            *(out[1] + (tmp_bottom * width) + w) = 255;
            *(out[2] + (tmp_bottom * width) + w) = 255;
        }
        for (h = tmp_top; h < tmp_bottom; h++)
        {
            *(out[0] + (h * width) + tmp_left) = 255;
            *(out[1] + (h * width) + tmp_left) = 255;
            *(out[2] + (h * width) + tmp_left) = 255;
        }
        for (h = tmp_top; h < tmp_bottom; h++)
        {
            *(out[0] + (h * width) + tmp_right) = 255;
            *(out[1] + (h * width) + tmp_right) = 255;
            *(out[2] + (h * width) + tmp_right) = 255;
        }
    }
}

void FTDModel_GetWholeAlpha(int idx_frame, struct FTDConfig *config, int pb_people)
{
    //scene pb: pb_people 0~100
    config->scene_alpha = int(pb_people * 1.27);
    config->scene_alpha = min(config->scene_alpha, config->scene_alpha_max);
    if (config->valid_face)
    {
        config->face_alpha = min(config->face_alpha + config->alpha_up_step, config->face_alphe_max);
    }
    else
    {
        config->face_alpha = max(config->face_alpha - config->alpha_down_step, 0);
    }
    config->whole_alpha = max(min(config->face_alpha + config->scene_alpha, 255), 0);
    printf("valid_face %d.\n", config->valid_face);
    printf("face_alpha %d.\n", config->face_alpha);
    printf("scene_alpha %d.\n", config->scene_alpha);
    printf("whole_alpha %d.\n", config->whole_alpha);
    //same as De3csc
}



//same as De3csc
void ftd_csc(PixelType *RY, PixelType *GU, PixelType *BV, int size, int *intcoef, int databit, int coefbit)
{
    int ii;
    int maxdata;
    int in0, in1, in2;
    int out0, out1, out2;
    int rounddata, shift;

    maxdata = (1 << (databit + coefbit)) - 1;
    rounddata = 1 << (coefbit - 1);
    shift = (10 - databit);

    for (ii = 0; ii < size; ii++)
    {
        in0 = RY[ii];
        in1 = GU[ii];
        in2 = BV[ii];

        in0 = in0 - intcoef[12];
        in1 = in1 - intcoef[13];
        in2 = in2 - intcoef[14];

        out0 = in0*intcoef[0] + in1*intcoef[1] + in2*intcoef[2] + (intcoef[3] << coefbit) + rounddata;
        out1 = in0*intcoef[4] + in1*intcoef[5] + in2*intcoef[6] + (intcoef[7] << coefbit) + rounddata;
        out2 = in0*intcoef[8] + in1*intcoef[9] + in2*intcoef[10] + (intcoef[11] << coefbit) + rounddata;

        out0 = (out0<0) ? 0 : ((out0>maxdata) ? maxdata : out0);
        out1 = (out1<0) ? 0 : ((out1>maxdata) ? maxdata : out1);
        out2 = (out2<0) ? 0 : ((out2>maxdata) ? maxdata : out2);

        out0 = ((out0) >> coefbit);
        out1 = ((out1) >> coefbit);
        out2 = ((out2) >> coefbit);

        RY[ii] = out0;
        GU[ii] = out1;
        BV[ii] = out2;
    }
    return;
}

//input, x array, y array, slop array, num of array, shift
int pos_curve(int input, int *x, int *y, int *slop, int num, int shift)
{
    int rtvalue;

    if (num == 0)
        return 0;
    else
    {
        if (input < x[0])
        {
            rtvalue = y[0];
        }
        else if (input >= x[num - 1])
        {
            rtvalue = y[num - 1];
        }
        else
        {
            for (int i = 1; i < num; ++i)
            {
                if (input < x[i])
                {
                    rtvalue = (y[i - 1] << shift) + (input - x[i - 1]) * slop[i - 1];
                    rtvalue = rtvalue >> shift;
                    break;
                }
            }
        }
        return rtvalue;
    }
}

void FTDModel_ModifyHSVRange(PixelType *H_buff, PixelType *S_buff, PixelType *V_buff, int idx_frame, struct FTDConfig *config)
{
    int i,j,k;
    PixelType H, S, V;
    int left, right, top, bottom;
    int size;
    int pixel_num=0;
    int h_sum = 0;
    int s_sum = 0;
    int v_sum = 0;
    int sign;
    int h_dist,s_dist;
    int h_adj, s_adj;
    int h_gain_x[2] = { 100, 180 };
    int h_gain_y[2] = { 0, 10 };
    int h_gain_slp = (h_gain_y[1] - h_gain_y[0]) * 256 / (h_gain_x[1] - h_gain_x[0]);
    int s_gain_x[2] = { 10, 30 };
    int s_gain_y[2] = { 0, 10 };
    int s_gain_slp = (s_gain_y[1] - s_gain_y[0]) * 256 / (s_gain_x[1] - s_gain_x[0]);

    config->valid_face = 0;
    // sum up the H, S, V within the face
    for (k = 0; k < config->face_num; k++)
    {
        left  = config->result[idx_frame].left[k];
        right = config->result[idx_frame].right[k];
        top   = config->result[idx_frame].top[k];
        bottom= config->result[idx_frame].bottom[k];

        if (left != 0 || top != 0 || right != 0 || bottom != 0 )
        {
            config->valid_face = 1;
            for (i = top; i < bottom; i++)
            {
                for (j = left; j < right; j++)
                {
                    H = *(H_buff + i * config->width + j);
                    S = *(S_buff + i * config->width + j);
                    V = *(V_buff + i * config->width + j);
                    pixel_num = pixel_num + 1;
                    h_sum = h_sum + H;
                    s_sum = s_sum + S;
                    v_sum = v_sum + V;
                }
            }
        }
    }

    if (config->valid_face)
    {
        h_sum = h_sum / pixel_num;
        s_sum = s_sum / pixel_num;
        v_sum = v_sum / pixel_num;

        //modify the FTD detect range
        h_dist = abs(h_sum - (config->HSV_H_LOW_DEFAULT + config->HSV_H_HIGH_DEFAULT) / 2);
        sign = (h_sum - ((config->HSV_H_LOW_DEFAULT + config->HSV_H_HIGH_DEFAULT) / 2)) > 0 ? 1 : -1;
        h_adj = pos_curve(h_dist, h_gain_x, h_gain_y, &h_gain_slp, 2, 8);
        h_adj = h_adj * sign;

        s_dist = abs(s_sum - (config->HSV_S_LOW_DEFAULT + config->HSV_S_HIGH_DEFAULT) / 2);
        sign = (s_sum - ((config->HSV_S_LOW_DEFAULT + config->HSV_S_HIGH_DEFAULT) / 2)) > 0 ? 1 : -1;
        s_adj = pos_curve(s_dist, s_gain_x, s_gain_y, &s_gain_slp, 2, 8);
        s_adj = s_adj * sign;

        //main process
        config->HSV_H_HIGH = min(config->HSV_H_HIGH_DEFAULT + h_adj, 359);
        config->HSV_H_LOW = max(config->HSV_H_LOW_DEFAULT + h_adj, 0);
        config->HSV_S_HIGH = min(config->HSV_S_HIGH_DEFAULT + s_adj, 255);
        config->HSV_S_LOW = max(config->HSV_S_LOW_DEFAULT + s_adj, 0);
    }
    else
    {
        config->HSV_H_HIGH = config->HSV_H_HIGH_DEFAULT;
        config->HSV_H_LOW = config->HSV_H_LOW_DEFAULT;
        config->HSV_S_HIGH = config->HSV_S_HIGH_DEFAULT;
        config->HSV_S_LOW = config->HSV_S_LOW_DEFAULT;
    }

#if 0
    printf("pixel_num %d.\n", pixel_num);
    printf("h_sum %d.\n", h_sum);
    printf("s_sum %d.\n", s_sum);
    printf("v_sum %d.\n", v_sum);
    printf("HSV_H_HIGH %d.\n", config->HSV_H_HIGH);
    printf("HSV_H_LOW %d.\n", config->HSV_H_LOW);
    printf("HSV_S_HIGH %d.\n", config->HSV_S_HIGH);
    printf("HSV_S_LOW %d.\n", config->HSV_S_LOW);
#endif
}

//merge AI and aipq_result
void FTDModel_MergeAI(int idx_frame, struct FTDConfig *config, unsigned char *skinmap)
{
    int i;
    int size;
    int cur;
    int divide, remain;
    int x, y;
    int tdist;
    int fdist;
    int base_skinmap;
    int tmp_skinmap;

    divide = 0;
    remain = 0;
    size = config->width * config->height;

    for (i = 0; i < size; i++)
    {
        //divide and remain
        cur = i;
        divide = 0;
        remain = cur;
        while (cur >= config->width)
        {
            cur = cur - config->width;
            divide++;
            remain = cur;
        }
        x = remain;
        y = divide;

        tdist = 0;
        base_skinmap = max(min(skinmap[i] + tdist, 255), 0);
        tmp_skinmap = 255 - base_skinmap;
        tmp_skinmap = (tmp_skinmap * config->whole_alpha) >> 8;
        tmp_skinmap = min(max(255 - tmp_skinmap, 0), 255);
        *(skinmap + i) = tmp_skinmap;

        if (i == 1)
            i = i;
    }

dbg_error:
    return;
}

int DistCal(int x, int y, int idx_frame, struct FTDConfig *config)
{
    int min_dist;
    min_dist = FaceDistCal(x, y, idx_frame, config);
    return min_dist;
}

int FaceDistCal(int x, int y, int idx_frame, struct FTDConfig *config)
{
    int i;
    int tmp_dist[50];
    int left, top, right, bottom;
    int radius;
    int center_x, center_y;
    int min_dist = 4096;

    for (i = 0; i < config->face_num; i++)
    {
        left = config->result[idx_frame].left[i];
        top = config->result[idx_frame].top[i];
        right = config->result[idx_frame].right[i];
        bottom = config->result[idx_frame].bottom[i];

        //no meaning data
        if ((left == 0) && (top == 0) && (right == 0) && (bottom == 0))
        {
            tmp_dist[i] = 4096;
        }
        else
        {
            center_x = (left + right)/2;
            center_y = (top + bottom)/2;
            //radius = sqrt((right - center_x)*(right - center_x) + (bottom - center_y)*(bottom - center_y));
            radius = (right - center_x) + (bottom - center_y);
            tmp_dist[i] = max(0, abs(x - center_x) + abs(y - center_y) - radius);
        }
    }

    for (i = 0; i < config->face_num; i++)
    {
        min_dist = min(min_dist, tmp_dist[i]);
    }

    //expand the radius
    min_dist = min_dist >> 3;
    return min_dist;
}

int DistCal_p2a(int x, int y, int left, int top, int right, int bottom)
{
    int disstt;

    if ((x <= left) && (y <= top))
    {
        disstt = (left - x) + (top - y);
    }
    else if ((x > left) && (x < right) && (y < top))
    {
        disstt = top - y;
    }
    else if ((x >= right) && (y <= top))
    {
        disstt = (x - right) + (top - y);
    }
    else if ((x > right) && (y > top) && (y < bottom))
    {
        disstt = x - right;
    }
    else if ((x >= right) && (y >= bottom))
    {
        disstt = (x - right) + (y - bottom);
    }
    else if ((x > left) && (x < right) && (y > bottom))
    {
        disstt = y - bottom;
    }
    else if ((x <= left) && (y >= bottom))
    {
        disstt = (left - x) + (y - bottom);
    }
    else if ((x < left) && (y > top) && (y < bottom))
    {
        disstt = left - x;
    }
    else
    {
        disstt = 0;
    }

    return disstt;
}

//YUV2HSV
void FTDModel_YUV2HSV(PixelType *Y, PixelType *cb, PixelType *cr, PixelType *H, PixelType *S, PixelType *V, int size, unsigned short Delta_LUT[256])
{
    //YUV2RGB
    int i;
    int MAXRGB, MINRGB,DELTA;
    PixelType *R, *G, *B;
    PixelType *TEMPY, *TEMPCB, *TEMPCR;

    MALLOC_AND_INIT(R, size, PixelType);
    MALLOC_AND_INIT(G, size, PixelType);
    MALLOC_AND_INIT(B, size, PixelType);
    MALLOC_AND_INIT(TEMPY, size, PixelType);
    MALLOC_AND_INIT(TEMPCB, size, PixelType);
    MALLOC_AND_INIT(TEMPCR, size, PixelType);

    for (i = 0; i < size; i++)
    {
        if (i == 510005)
            i = i;
        if (i == 510025)
            i = i;
        if (i == 508104)
            i = i;
        if (i == 508004)
            i = i;

        *(TEMPY + i) = (*(Y + i) >> 2)-16;
        *(TEMPCB + i) = (*(cb + i) >> 2)-128;
        *(TEMPCR + i) = (*(cr + i) >> 2)-128;

        *(R + i) = (1192 * (*(TEMPY + i)) + 1836 * (*(TEMPCR + i))) >> 10;
        *(G + i) = (1192 * (*(TEMPY + i)) - 218 * (*(TEMPCB + i)) - 546 * (*(TEMPCR + i))) >> 10;
        *(B + i) = (1192 * (*(TEMPY + i)) + 2163 * (*(TEMPCB + i))) >> 10;

        MAXRGB = max(max((*(R + i)), (*(B + i))), (*(G + i)));
        MINRGB = min(min((*(R + i)), (*(B + i))), (*(G + i)));
        DELTA = MAXRGB - MINRGB;

        if (DELTA == 0)
        {
            *(H + i) = 0;
            *(S + i) = 0;
            *(V + i) = MAXRGB;
        }
        else if ((DELTA != 0) && (MAXRGB == (*(R + i))))
        {
            //*(H + i) = (60 * ((((*(G + i)) - (*(B + i)))) * Delta_LUT[DELTA - 1] + (0 << 9))) >> 9;
            *(H + i) = (60 * ((((*(G + i)) - (*(B + i))))*Delta_LUT[DELTA - 1] + (0 << 12))) >> 12;
            if (MAXRGB == 0)
            {
                *(S + i) = 0;
            }
            else
            {
                *(S + i) = (DELTA * Delta_LUT[MAXRGB - 1]) >> 4;
            }
            *(V + i) = MAXRGB;
        }
        else if ((DELTA != 0) && (MAXRGB == (*(G + i))))
        {
            //*(H + i) = (60 * ((((*(B + i)) - (*(R + i))) * Delta_LUT[DELTA - 1]) + (2 << 9))) >> 9;
            *(H + i) = (60 * ((((*(B + i)) - (*(R + i))))*Delta_LUT[DELTA - 1] + (2 << 12))) >> 12;
            if (MAXRGB == 0)
            {
                *(S + i) = 0;
            }
            else
            {
                *(S + i) = (DELTA * Delta_LUT[MAXRGB - 1]) >> 4;
            }
            *(V + i) = MAXRGB;
        }
        else if ((DELTA != 0) && (MAXRGB == (*(B + i))))
        {
            //*(H + i) = (60 * ((((*(R + i)) - (*(G + i))) * Delta_LUT[DELTA - 1] + (4 << 9))) >> 9;
            *(H + i) = (60 * ((((*(R + i)) - (*(G + i))))*Delta_LUT[DELTA - 1] + (4 << 12))) >> 12;
            if (MAXRGB == 0)
            {
                *(S + i) = 0;
            }
            else
            {
                *(S + i) = (DELTA * Delta_LUT[MAXRGB - 1]) >> 4;
            }
            *(V + i) = MAXRGB;
        }

        while (*(H + i) >= 360)
        {
            *(H + i) = *(H + i) - 360;
        }
        while (*(H + i) < 0)
        {
            *(H + i) = *(H + i) + 360;
        }

        //H (0.360)
        //S 8bit
        //V 8bit
    }

    free(R);
    free(G);
    free(B);
    free(TEMPY);
    free(TEMPCB);
    free(TEMPCR);
    return;
}

int FTDModel_CalSin(int H, int n, unsigned short atan_LUT[16])
{
    int x = 1 << 8, y = 0 << 8, z;
    int x_new, y_new, z_new;
    int i;
    int signx=1, signy=1;

    for (i = 0; i < 2; i++)
    {
        if (H > 90)
        {
            H = H - 180;
            signx *= -1;
            signy *= -1;
        }
        else if (H < -90)
        {
            H = H + 180;
            signx *= -1;
            signy *= -1;
        }
        else
        {
            signx *= 1;
            signy *= 1;
        }
    }

    z = H << 8;
    for (i = 0; i < n; i++)
    {
        if (z > 0)
        {
            x_new = x - (y >> i);
            y_new = y + (x >> i);
            z_new = z - atan_LUT[i];
        }
        else
        {
            x_new = x + (y >> i);
            y_new = y - (x >> i);
            z_new = z + atan_LUT[i];
        }
        x = x_new;
        y = y_new;
        z = z_new;
    }

    x = (x * 622) >> 10;
    y = (y * 622) >> 10;
    x = x*signx;
    y = y*signy;
    return CLAMP(y, 0, 256);
}

int FTDModel_CalCos(int H, int n, unsigned short atan_LUT[16])
{
    int x = 1 << 8, y = 0 << 8, z;
    int x_new, y_new, z_new;
    int i;
    int signx=1, signy=1;

    for (i = 0; i < 2; i++)
    {
        if (H > 90)
        {
            H = H - 180;
            signx *= -1;
            signy *= -1;
        }
        else if (H < -90)
        {
            H = H + 180;
            signx *= -1;
            signy *= -1;
        }
        else
        {
            signx *= 1;
            signy *= 1;
        }
    }

    z = H << 8;
    for (i = 0; i < n; i++)
    {
        if (z > 0)
        {
            x_new = x - (y >> i);
            y_new = y + (x >> i);
            z_new = z - atan_LUT[i];
        }
        else
        {
            x_new = x + (y >> i);
            y_new = y - (x >> i);
            z_new = z + atan_LUT[i];
        }
        x = x_new;
        y = y_new;
        z = z_new;
    }

    x = (x * 622) >> 10;
    y = (y * 622) >> 10;
    x = x*signx;
    y = y*signy;
    return CLAMP(x, 0, 256);
}

void FTDModel_CalMain(PixelType *H, PixelType *S, PixelType *V, int h_low, int h_high, int s_low, int s_high, int v_low, int v_high, int model_h, int model_r, int size, unsigned char *skinmap, unsigned short atan_LUT[16])
{
    int i;
    INT64 dist, dist1, dist2;
    int x1, y1, z1;
    int x2, y2, z2;
    INT64 xx1, xx2, yy1, yy2, zz1, zz2;
    INT64 delta_x, delta_y, delta_z;
    int maxz;
    int bitmove;
    int bitmove_insqrt;
    int bitmove_xy;
    int bitmove_z;
    int bitmove_xyf;
    int bitmove_zf;

    bitmove = 0;
    bitmove_insqrt = 0;
    bitmove_xy = 0;
    bitmove_z = 0;
    bitmove_xyf = 16;
    bitmove_zf = 0;
    maxz = 256;

    for (i = 0; i < size; i++)
    {
        if (i == 510005)
            i = i;
        if (i == 510025)
            i = i;

        // 分支1：H在范围内，S < s_low，V在范围内
        if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) < s_low && *(V + i) >= v_low && *(V + i) <= v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = s_low;
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = s_low;
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支2：H在范围内，S在[s_low, s_high]，V在范围内
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) >= s_low && *(S + i) <= s_high && *(V + i) >= v_low && *(V + i) <= v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = *(S + i);
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = *(S + i);
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支3：H在范围内，S > s_high，V在范围内
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) > s_high && *(V + i) >= v_low && *(V + i) <= v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = s_high;
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = s_high;
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支4：H超出范围，S < s_low，V在范围内
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) < s_low && *(V + i) >= v_low && *(V + i) <= v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = s_low;
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支5：H超出范围，S > s_high，V在范围内
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) > s_high && *(V + i) >= v_low && *(V + i) <= v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = s_high;
            z2 = *(V + i);

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支6：H在范围内，S < s_low，V < v_low
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) < s_low && *(V + i) < v_low)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = s_low;
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = s_low;
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支7：H在范围内，S在[s_low, s_high]，V < v_low
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) >= s_low && *(S + i) <= s_high && *(V + i) < v_low)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = *(S + i);
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = *(S + i);
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支8：H在范围内，S > s_high，V < v_low
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) > s_high && *(V + i) < v_low)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = s_high;
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = s_high;
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支9：H超出范围，S < s_low，V < v_low
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) < s_low && *(V + i) < v_low)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = s_low;
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支10：H超出范围，S在[s_low, s_high]，V < v_low
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) >= s_low && *(S + i) <= s_high && *(V + i) < v_low)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = *(S + i);
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支11：H超出范围，S > s_high，V < v_low
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) > s_high && *(V + i) < v_low)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = s_high;
            z2 = v_low;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支12：H在范围内，S < s_low，V > v_high
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) < s_low && *(V + i) > v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = s_low;
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = s_low;
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支13：H在范围内，S在[s_low, s_high]，V > v_high
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) >= s_low && *(S + i) <= s_high && *(V + i) > v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = *(S + i);
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = *(S + i);
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支14：H在范围内，S > s_high，V > v_high
        else if (*(H + i) > h_low && *(H + i) < h_high && *(S + i) > s_high && *(V + i) > v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_low;
            y2 = s_high;
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = h_high;
            y2 = s_high;
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist2 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = min(dist1, dist2);
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支15：H超出范围，S < s_low，V > v_high
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) < s_low && *(V + i) > v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = s_low;
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支16：H超出范围，S在[s_low, s_high]，V > v_high
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) >= s_low && *(S + i) <= s_high && *(V + i) > v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = *(S + i);
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 分支17：H超出范围，S > s_high，V > v_high
        else if ((*(H + i) <= h_low || *(H + i) >= h_high) && *(S + i) > s_high && *(V + i) > v_high)
        {
            x1 = *(H + i);
            y1 = *(S + i);
            z1 = *(V + i);
            x2 = *(H + i);
            y2 = s_high;
            z2 = v_high;

            xx1 = (model_r * y1 * z1 * FTDModel_CalCos(x1, 16, atan_LUT)) >> bitmove_xyf;
            yy1 = (model_r * y1 * z1 * FTDModel_CalSin(x1, 16, atan_LUT)) >> bitmove_xyf;
            zz1 = (model_h * (maxz - z1)) >> bitmove_zf;

            xx2 = (model_r * y2 * z2 * FTDModel_CalCos(x2, 16, atan_LUT)) >> bitmove_xyf;
            yy2 = (model_r * y2 * z2 * FTDModel_CalSin(x2, 16, atan_LUT)) >> bitmove_xyf;
            zz2 = (model_h * (maxz - z2)) >> bitmove_zf;

            dist1 = Sqrt_forFTD(((((xx1 - xx2) * (xx1 - xx2)) >> bitmove_xy) + (((yy1 - yy2) * (yy1 - yy2)) >> bitmove_xy) + (((zz1 - zz2) * (zz1 - zz2)) >> bitmove_z)) >> bitmove_insqrt);

            dist = dist1;
            dist = dist >> bitmove;
            if (dist > 255)
                dist = 255;
            *(skinmap + i) = (unsigned char)dist;
        }
        // 兜底分支：所有非肤色区域
        else
        {
            dist = 0;
            *(skinmap + i) = (unsigned char)dist;
        }
    }
    return;
}

INT64 Sqrt_forFTD(INT64 X)
{
    INT64 low, high;
    INT64 mid,lastmid;
    low = 0;
    high = X;
    mid = 0;
    lastmid = 9999;
    while (abs(lastmid - mid) > 0)
    {
        lastmid = mid;
        mid = (low + high) >> 1;
        if ((mid*mid) > X)
        {
            high = mid;
        }
        else if ((mid*mid) < X)
        {
            low = mid;
        }
        else
        {
            return mid;
        }
    }
    return mid;
}
