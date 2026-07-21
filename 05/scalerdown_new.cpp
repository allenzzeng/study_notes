// ==============================================================
// 内存管理：移除窗口行缓存，新增水平中间图与单行处理缓存
// ==============================================================
struct ScalerModel *ScalerModel_New( ScaleDownConfig *config) {
    int i;
    struct ScalerModel *model;

    PCMEM_ALLOC(model, struct ScalerModel);

    // 单行水平处理缓存：带左右边界扩展（替代原 pixel 窗口数组）
    PCMEM_ALLOC_N(model->horz_line_buf, config->in_width[0] + 2 * ADSCALER_WIN_W, ScalerPixel);
    
    // 水平插值后完整中间图：in_height 行 × out_width 列（两阶段核心缓存）
    PCMEM_ALLOC_N(model->h_full_buf, config->in_height[0] * config->out_width[0], ScalerPixel);

    // 垂直插值单行结果缓存
    PCMEM_ALLOC_N(model->nmintrp_out, config->out_width[0], ScalerPixel);

    // 输出坐标预计算缓存
    PCMEM_ALLOC_N(model->x_intgs, config->out_width[0], int);
    PCMEM_ALLOC_N(model->y_intgs, config->out_height[0], int);
    PCMEM_ALLOC_N(model->x_fracs, config->out_width[0], int);
    PCMEM_ALLOC_N(model->y_fracs, config->out_height[0], int);

    // 输出行缓存
    PCMEM_ALLOC_N(model->out, config->out_width[0], ScalerPixel);
dbg_error:
    return model;
}

void ScalerModel_Free(struct ScalerModel **model)
{
    int i;
    CHECK_PTR(model);
    CHECK_PTR(*model);

    PCMEM_FREE((*model)->horz_line_buf);
    PCMEM_FREE((*model)->h_full_buf);

    PCMEM_FREE((*model)->nmintrp_out);

    PCMEM_FREE((*model)->x_intgs);
    PCMEM_FREE((*model)->y_intgs);
    PCMEM_FREE((*model)->x_fracs);
    PCMEM_FREE((*model)->y_fracs);

    PCMEM_FREE((*model)->out);
    PCMEM_FREE(*model);
dbg_error:
    return;
}

// ==============================================================
// 位宽配置（保持原逻辑不变）
// ==============================================================
void ScalerModel_SetBitdepth(struct ScalerModel *model,const std::vector<int>& bitdepth)
{
    for (int i = 0; i < NUM_CHN; ++i)
    {
        model->bit_width[i] = bitdepth[i];
        model->pixel_step[i] = sizeof(PixelType);
    }
}

// ==============================================================
// 通道调度（保持原逻辑不变）
// ==============================================================
void ScalerModel_Run(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config, 
    PixelType *in[3], 
    PixelType *out[3]
) {
    for (int chn = 0; chn < NUM_CHN; ++chn) {
        model->chn = chn;
        ScalerModel_RunChannel(model, config, in[chn], out[chn]);
    }
}

// ==============================================================
// 单通道主流程：先全水平插值，再全垂直插值
// ==============================================================
void ScalerModel_RunChannel(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config, 
    PixelType *in, 
    PixelType *out
) {
    // 预生成所有输出点的整数+小数坐标（原逻辑复用）
    ScalerModel_GenOutputCoors(model, config);
    
    // 阶段1：对所有输入行做水平插值，生成完整中间图
    ScalerModel_HorzInterpFull(model, config, in);
    
    // 阶段2：对中间图做垂直插值，逐行输出最终结果
    ScalerModel_VertInterpFull(model, config, out);
}

// ==============================================================
// 坐标生成（完全复用原逻辑，保证插值位置一致）
// ==============================================================
void ScalerModel_GenOutputCoors(struct ScalerModel *model, struct ScaleDownConfig *config)
{
    __int64 coor_y, coor_x, tmp_loc;
    int i;

    coor_y = config->vphase[model->chn];
    coor_x = config->hphase[model->chn];

    for (i = 0; i < config->out_width[model->chn]; ++i)
    {
        tmp_loc = coor_x;
        if (tmp_loc < 0)
        {
            tmp_loc = (tmp_loc >> LOCFRACBIT) << LOCFRACBIT;
        }
        model->x_intgs[i] = tmp_loc >> LOCFRACBIT;
        model->x_fracs[i] = tmp_loc - (model->x_intgs[i] << LOCFRACBIT);
        model->x_intgs[i] = CLAMP(model->x_intgs[i],
            -ADSCALER_WIN_W - HORZ_TAB_LEFT, config->in_width[0] + 2 * ADSCALER_WIN_W - HORZ_TAB_RIGHT);
        coor_x += config->hstep[model->chn];
    }

    for (i = 0; i < config->out_height[model->chn]; ++i)
    {
        tmp_loc = coor_y;
        if (tmp_loc < 0)
        {
            tmp_loc = (tmp_loc >> LOCFRACBIT) << LOCFRACBIT;
        }
        if ((tmp_loc >> LOCFRACBIT) >= (config->in_height[model->chn]-1))
        {
            tmp_loc = (config->in_height[model->chn]-1);
            tmp_loc <<= LOCFRACBIT;
        }
        model->y_intgs[i] = tmp_loc >> LOCFRACBIT;
        model->y_fracs[i] = tmp_loc - (model->y_intgs[i] << LOCFRACBIT);
        coor_y += config->vstep[model->chn];
    }
}

// ==============================================================
// 阶段1：全图水平插值
// ==============================================================
void ScalerModel_HorzInterpFull(struct ScalerModel* model, ScaleDownConfig* config, PixelType *in)
{
    int j, c, i;
    int sx;
    int cumsum;
    char htab[HORZ_TAB];
    char* htab_ptr;
    short sign;
    int hfilt_max;
    const int in_w = config->in_width[model->chn];
    const int in_h = config->in_height[model->chn];
    const int out_w = config->out_width[model->chn];
    ScalerPixel *horz_line = model->horz_line_buf;
    ScalerPixel *h_full = model->h_full_buf;

    hfilt_max = (1 << (model->bit_width[model->chn] + 9)) - 1;

    // 逐行处理输入：加载→边界扩展→水平插值→写入中间图
    for (j = 0; j < in_h; ++j)
    {
        // 加载当前输入行到边界缓存的中间位置
        ScalerModel_InputLine(
            horz_line + ADSCALER_WIN_W, 
            in + in_w * j, 
            in_w
        );
        // 左右边界重复扩展（与原逻辑完全一致）
        ScalerModel_LineBorderExtend(horz_line, in_w);

        ScalerPixel *dst_line = h_full + j * out_w;
        
        // 逐列做水平插值
        for (c = 0; c < out_w; ++c)
        {
            // 调试断点，与原逻辑位置对齐
            if (model->chn == config->dbg_chn && c == config->dbg_x && j == config->dbg_y)
            {
                c = c;
            }

            ScalerPixel* win_ptr = horz_line + model->x_intgs[c] + ADSCALER_WIN_W;
            sx = model->x_fracs[c] >> HALFPHASEBIT;
            Scaler_GetNormalScalerCoefs(htab, HORZ_TAB, sx, config->nm_horz_coefs[model->chn]);
            htab_ptr = htab - HORZ_TAB_LEFT;
            cumsum = 0;

            for (i = HORZ_TAB_LEFT; i <= HORZ_TAB_RIGHT; ++i)
            {
                cumsum += htab_ptr[i] * win_ptr[i];
            }

            // 舍入、钳位、位宽截断，与原逻辑完全一致
            sign = (cumsum >= 0) ? 1 : -1;
            if (sign == 1)
            {
                cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
                cumsum = (cumsum > hfilt_max) ? hfilt_max : cumsum;
                dst_line[c] = (unsigned short)(cumsum >> SCALER_COEFS_BITS);
                dst_line[c] = CLAMP(dst_line[c],0,0x3ff);
            }
            else
            {
                dst_line[c] = 0;
            }
        }
      if (config->sim_gen_en == 1 && config->dump_debug == 1)
          {
              if (!fp_h[model->chn]) {
                  char fn[300];
                  if (model->chn == 0)
                      sprintf(fn, "%shscaler_out_y.txt", config->dump_path);
                  if (model->chn == 1)
                      sprintf(fn, "%shscaler_out_u.txt", config->dump_path);
                  if (model->chn == 2)
                      sprintf(fn, "%shscaler_out_v.txt", config->dump_path);
                  fopen_s(&fp_h[model->chn], fn, "a");
              }
              for (int c = 0; c < out_w; ++c)
              {
                  unsigned int val = (dst_line[c]) & 0x00000fff;
                  fprintf(fp_h[model->chn], "%03x\n", val);
              }
              if (fp_h[model->chn]) {
                  fclose(fp_h[model->chn]);
                  fp_h[model->chn] = NULL;
              }
          }
    }
}

// ==============================================================
// 阶段2：全图垂直插值 + 逐行输出
// ==============================================================
void ScalerModel_VertInterpFull(struct ScalerModel* model, ScaleDownConfig* config, PixelType *out)
{
    int j, c, r;
    int sy;
    int cumsum;
    char vtab[VERT_TAB];
    char* vtab_ptr;
    short sign;
    int vfilt_max;
    const int in_h = config->in_height[model->chn];
    const int out_w = config->out_width[model->chn];
    const int out_h = config->out_height[model->chn];
    ScalerPixel *h_full = model->h_full_buf;
    ScalerPixel *nmintrp_out = model->nmintrp_out;

    vfilt_max = (1 << (model->bit_width[model->chn] + 6)) - 1;

    // 逐行输出
    for (j = 0; j < out_h; ++j)
    {
        model->out_y = j;
        // 调试断点，与原逻辑位置对齐
        if (model->out_y == config->dbg_y && model->chn == config->dbg_chn) {
            j = j; 
        }

        const int y_intg = model->y_intgs[j];
        const int y_frac = model->y_fracs[j];
        sy = y_frac >> HALFPHASEBIT;
        Scaler_GetNormalScalerCoefs(vtab, VERT_TAB, sy, config->nm_vert_coefs[model->chn]);
        vtab_ptr = vtab - VERT_TAB_UP;

        // 逐列做垂直插值
        for (c = 0; c < out_w; ++c)
        {
            if (model->chn == config->dbg_chn && c == config->dbg_x && model->out_y == config->dbg_y)
            {
                c = c;
            }

            cumsum = 0;
            for (r = VERT_TAB_UP; r <= VERT_TAB_DOWN; ++r)
            {
                // 行索引映射：与原窗口行的对应关系完全一致，保证插值位置对齐
                const int h_out_idx = r - ADSCALER_WIN_UP;
                int row_idx = y_intg + h_out_idx - ADSCALER_HALF_WIN_H + 1;
                
                // 垂直边界钳位：与原行加载逻辑完全一致
                if (row_idx < 0)
                    row_idx = 0;
                if (row_idx >= in_h - 1)
                    row_idx = in_h - 1;

                cumsum += vtab_ptr[r] * h_full[row_idx * out_w + c];
            }

            // 舍入、钳位、位宽截断，与原逻辑完全一致
            sign = (cumsum >= 0) ? 1 : -1;
            if (sign == 1)
            {
                cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
                cumsum = (cumsum > vfilt_max) ? vfilt_max : cumsum;
                nmintrp_out[c] = sign * (cumsum >> SCALER_COEFS_BITS);
            }
            else {
                nmintrp_out[c] = 0;
            }
        }

        // 输出钳位到位宽范围，与原 RunLine 逻辑一致
        for (c = 0; c < out_w; ++c)
        {
            int intrp_out = nmintrp_out[c];
            intrp_out = CLAMP(intrp_out, 0, (1 << model->bit_width[model->chn]) - 1);
            model->out[c] = intrp_out;
        }

        ScalerModel_OutputLine(
            out + out_w * j, 
            model->out, 
            out_w,
            model->pixel_step[model->chn]
        );
    }
}

// ==============================================================
// 系数提取（完全复用原逻辑，保证插值权重一致）
// ==============================================================
void Scaler_GetNormalScalerCoefs(char coefs[], int tabs, int sx, unsigned int coefs_table[])
{
    int num_table_unit, offset;
    char *coefs_ptr;
    int i;
    unsigned int table_unit;

    num_table_unit = tabs >> 2;
    offset = sx * num_table_unit;
    coefs_ptr = coefs;

    for (i = 0; i < num_table_unit; ++i)
    {
        table_unit = coefs_table[offset + i];
        *coefs_ptr++ = (((table_unit & 0xff)) << 24) >> 24;
        *coefs_ptr++ = ((((table_unit & 0xff00) >> 8)) << 24) >> 24;
        *coefs_ptr++ = ((((table_unit & 0xff0000) >> 16)) << 24) >> 24;
        *coefs_ptr++ = ((((table_unit & 0xff000000) >> 24)) << 24) >> 24;
    }
}

// ==============================================================
// 工具函数：行边界扩展（完全复用原逻辑）
// ==============================================================
void ScalerModel_LineBorderExtend(ScalerPixel *line, int width)
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

// ==============================================================
// 工具函数：输入行拷贝（完全复用原逻辑）
// ==============================================================
void ScalerModel_InputLine(ScalerPixel *dst, PixelType *src, int length)
{
    int i;
    for (i = 0; i < length; ++i)
    {
        *dst++ = (ScalerPixel)*src++;
    }
}

// ==============================================================
// 工具函数：输出行拷贝（完全复用原逻辑）
// ==============================================================
void ScalerModel_OutputLine(PixelType *dst, ScalerPixel *src, int length, int pixel_step)
{
    int i;
    for (i = 0; i < length; ++i)
    {
        *dst = (ScalerPixel)*src;
        ++src;++dst;
    }
}
