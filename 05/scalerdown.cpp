struct ScalerModel *ScalerModel_New( ScaleDownConfig *config) {
    int i;
    struct ScalerModel *model;

    PCMEM_ALLOC(model, struct ScalerModel);


    for (i = 0; i < ADSCALER_WIN_H; ++i) {
        PCMEM_ALLOC_N(model->pixel[i], config->in_width[0] + 2 * ADSCALER_WIN_W, ScalerPixel);
        PCMEM_ALLOC_N(model->h_nmintrp_out[i], config->out_width[0], ScalerPixel);
        PCMEM_ALLOC_N(model->h_out[i], config->out_width[0], ScalerPixel);
    }

    PCMEM_ALLOC_N(model->v_nmintrp_out, config->in_width[0] + 2 * ADSCALER_WIN_W, ScalerPixel);
    PCMEM_ALLOC_N(model->nmintrp_out, config->out_width[0], ScalerPixel);

    PCMEM_ALLOC_N(model->x_intgs, config->out_width[0], int);
    PCMEM_ALLOC_N(model->y_intgs, config->out_height[0], int);
    PCMEM_ALLOC_N(model->x_fracs, config->out_width[0], int);
    PCMEM_ALLOC_N(model->y_fracs, config->out_height[0], int);

    PCMEM_ALLOC_N(model->out, config->out_width[0], ScalerPixel);
dbg_error:
    return model;
}


void ScalerModel_Free(struct ScalerModel **model)
{
    int i;
    CHECK_PTR(model);
    CHECK_PTR(*model);

    for (i = 0; i < ADSCALER_WIN_H; ++i)
    {
        PCMEM_FREE((*model)->pixel[i]);
        PCMEM_FREE((*model)->h_nmintrp_out[i]);
        PCMEM_FREE((*model)->h_out[i]);
    }

    PCMEM_FREE((*model)->nmintrp_out);
    PCMEM_FREE((*model)->v_nmintrp_out);

    PCMEM_FREE((*model)->x_intgs);
    PCMEM_FREE((*model)->y_intgs);
    PCMEM_FREE((*model)->x_fracs);
    PCMEM_FREE((*model)->y_fracs);

    PCMEM_FREE((*model)->out);
    PCMEM_FREE(*model);
dbg_error:
    return;
}
void ScalerModel_SetBitdepth(struct ScalerModel *model,const std::vector<int>& bitdepth) //?
{
    for (int i = 0; i < NUM_CHN; ++i)
    {
        model->bitdepth[i] = bitdepth[i];
        model->pixel_step[i] = sizeof(PixelType);
    }
}
void ScalerModel_Run(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config, 
    PixelType *in[3], 
    PixelType *out[3]
) {
    // 遍历每个通道（NUM_CHN 为通道数量宏定义，如 3 表示 Y/U/V）
    for (int chn = 0; chn < NUM_CHN; ++chn) {
        // 设置当前处理通道
        model->chn = chn;
        // 调用单通道处理函数
        ScalerModel_RunChannel(model, config, in[chn], out[chn]);
    }
}

// 单通道处理核心逻辑：负责单个通道的图像缩放流程
void ScalerModel_RunChannel(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config, 
    PixelType *in, 
    PixelType *out
) {
    int r; // 循环变量，用于遍历输出图像的行
    ScalerModel_Init(model, config, in);

    for (r = 0; r < config->out_height[model->chn]; ++r) {
        model->out_y = r;
        if (model->out_y == config->dbg_y && model->chn == config->dbg_chn) {
            r = r; 
        }

        ScalerModel_RefreshY(model, config, in);
        ScalerModel_NormalInterpolation(model, config);
        ScalerModel_RunLine(model, config);
        ScalerModel_OutputLine(
            out + config->out_width[model->chn] * r , 
            model->out, 
            config->out_width[model->chn],model->pixel_step[model->chn]
        );
    }
}

void ScalerModel_Init(
    struct ScalerModel *model,    // 算法模型（包含缓存、参数等状态）
    struct ScaleDownConfig *config,  // 配置参数（分辨率、通道等）
    PixelType *in                    // 输入图像数据指针
) {
    // 循环变量：r 遍历窗口行，idx_load_line 计算实际加载的行索引
    int r, idx_load_line;

    // 1. 生成输出坐标（整数+小数部分）
    //    作用：计算缩放后每个输出像素对应的输入图像坐标（用于插值）
    ScalerModel_GenOutputCoors(model, config);

    // 2. 初始化模型的坐标参数（取第 0 个输出像素的坐标值）
    //    说明：y_intg/x_intg 是坐标整数部分，y_frac/x_frac 是小数部分
    model->y_intg = model->y_intgs[0];  
    model->x_intg = model->x_intgs[0];  
    model->y_frac = model->y_fracs[0];  
    model->x_frac = model->x_fracs[0];  

    for (r = 0; r < ADSCALER_WIN_H; ++r) {
        idx_load_line = model->y_intg + r - ADSCALER_HALF_WIN_H + 1;
        if (idx_load_line < 0) {
            idx_load_line = 0;  // 小于 0 时取第一行
        }
        if (idx_load_line >= config->in_height[model->chn] - 1) {
            idx_load_line = config->in_height[model->chn] - 1;  // 超过时取最后一行
        }
        ScalerModel_InputLine(
            model->pixel[r] + ADSCALER_WIN_W, 
            in + config->in_width[model->chn] * idx_load_line , 
            config->in_width[model->chn]
        );

        // 6. 对加载的行数据进行边缘扩展
        //    作用：处理图像边缘时，通过复制边缘像素避免越界访问
        ScalerModel_LineBorderExtend(model->pixel[r], config->in_width[model->chn]);
    }
}


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
        //if ((tmp_loc >> LOCFRACBIT) >= (config->width[model->chn]-1))
       // {
       //     tmp_loc = (config->width[model->chn]-1);
       //     tmp_loc <<= LOCFRACBIT;
       // }
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

void ScalerModel_RunLine(struct ScalerModel *model, struct ScaleDownConfig *config)
{
    int c;
    int intrp_out;

    for (c = 0; c < config->out_width[model->chn]; ++c)
    {
        model->out_x = c;
        if (model->chn == config->dbg_chn && model->out_x == config->dbg_x && model->out_y == config->dbg_y)
        {
            c = c;
        }
        ScalerModel_RefreshX(model, config);
            intrp_out = model->nmintrp_out[c];
        intrp_out = CLAMP(intrp_out, 0, (1 << model->bit_width[model->chn]) - 1);
            model->out[c] = intrp_out;
    }
}



void ScalerModel_NormalInterpolation(struct ScalerModel* model, ScaleDownConfig* config)
{
    int c, i, r;
    int sx;
    int cumsum;
    char htab[HORZ_TAB];
    char* htab_ptr;
    short sign;
    int hfilt_max;
    ScalerPixel* h_out[ADSCALER_WIN_H];

    for (r = 0; r < ADSCALER_WIN_H; ++r)
    {
        h_out[r] = model->h_out[r];
    }
    hfilt_max = (1 << (model->bit_width[model->chn] + 9)) - 1;

    int start_r;
    if (model->out_y == 0)
    {
        start_r = 0;
    }
    else {
        start_r = ADSCALER_WIN_H - model->num_rolllines;
        if (start_r < 0) start_r = 0;
    }

    for (r = start_r; r < ADSCALER_WIN_H; ++r)
    {
        for (c = 0; c < config->out_width[model->chn]; ++c)
        {
            if (model->chn == config->dbg_chn && c == config->dbg_x && model->out_y == config->dbg_y)
            {
                c = c;
            }
            ScalerPixel* win_ptr = model->pixel[r] + model->x_intgs[c] + ADSCALER_WIN_W;
            sx = model->x_fracs[c] >> HALFPHASEBIT;
            Scaler_GetNormalScalerCoefs(htab, HORZ_TAB, sx, config->nm_horz_coefs[model->chn]);
            htab_ptr = htab - HORZ_TAB_LEFT;
            cumsum = 0;

            for (i = HORZ_TAB_LEFT; i <= HORZ_TAB_RIGHT; ++i)
            {
                cumsum += htab_ptr[i] * win_ptr[i];
            }
            // clip to unsigned
            sign = (cumsum >= 0) ? 1 : -1;
            if (sign == 1)
            {
                cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
                cumsum = (cumsum > hfilt_max) ? hfilt_max : cumsum;
                h_out[r][c] = (unsigned short)(cumsum >> SCALER_COEFS_BITS);
            }
            else
            {
                h_out[r][c] = 0;
            }
        }
    }

    int vfilt_max = (1 << (model->bit_width[model->chn] + 6)) - 1;
    int sy = model->y_frac >> HALFPHASEBIT;
    char vtab[VERT_TAB];
    char* vtab_ptr = vtab - VERT_TAB_UP;
    Scaler_GetNormalScalerCoefs(vtab, VERT_TAB, sy, config->nm_vert_coefs[model->chn]);

    for (c = 0; c < config->out_width[model->chn]; ++c)
    {
        cumsum = 0;
        for (r = VERT_TAB_UP; r <= VERT_TAB_DOWN; ++r)
        {
            cumsum += vtab_ptr[r] * h_out[r - ADSCALER_WIN_UP][c];
        }
        // clip to 17bit signed
        sign = (cumsum >= 0) ? 1 : -1;
        if (sign == 1)
        {
            cumsum = abs(cumsum) + (1 << (SCALER_COEFS_BITS - 1));
            cumsum = (cumsum > vfilt_max) ? vfilt_max : cumsum;
            model->nmintrp_out[c] = sign * (cumsum >> SCALER_COEFS_BITS);
        }
        else {
            model->nmintrp_out[c] = 0;
        }
    }
}


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

void ScalerModel_RefreshY(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config, 
    PixelType *in
) {
    // 计算需要滚动的行数（垂直方向坐标差）
    model->num_rolllines = model->y_intgs[model->out_y] - model->y_intg;
    // 更新垂直方向坐标（整数+小数部分）
    model->y_intg = model->y_intgs[model->out_y];
    model->y_frac = model->y_fracs[model->out_y];
    model->x_intg = model->x_intgs[0];
    model->x_frac = model->x_fracs[0];

    if (model->num_rolllines > 0) {
        ScalerModel_RollLineBuffer(model, config, in);
    }
    ScalerModel_RefreshStatusY(model, config);
}

void ScalerModel_RefreshStatusY(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config
) {
    int i, j, m, n;

    // 遍历垂直窗口的每一行（ADSCALER_WIN_H 为窗口高度）
    for (i = 0; i < ADSCALER_WIN_H; ++i) {
        model->win_pixel[i] = model->pixel[i] + model->x_intg + ADSCALER_WIN_W;
    }
    for (m = 0; m < 2; ++m) {
        for (n = -1; n < 1; ++n) {
            // 初始化局部 min/max 为当前窗口像素值
            model->local_min2x2[m][n + 1] = model->win_pixel[m][n];
            model->local_max2x2[m][n + 1] = model->win_pixel[m][n];
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

void ScalerModel_RefreshX(
    struct ScalerModel *model, 
    struct ScaleDownConfig *config
) {
    int i;
    model->x_stride = model->x_intgs[model->out_x] - model->x_intg;
    model->x_intg = model->x_intgs[model->out_x];
    model->x_frac = model->x_fracs[model->out_x];

    // 需要更新列偏移时，调整窗口像素地址
    if (model->x_stride > 0) {
        for (i = 0; i < ADSCALER_WIN_H; ++i) {
            model->win_pixel[i] += model->x_stride;
        }
    }

}

void ScalerModel_RollLineBuffer(struct ScalerModel *model, struct ScaleDownConfig *config, PixelType *in)
{
    int roll, r, idx_load_line;
    ScalerPixel *tmp_pix;
    ScalerPixel *tmp_h_norm_out;
    ScalerPixel *tmp_h_out;

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
            tmp_h_norm_out = model->h_out[0];
            for (r = 0; r < ADSCALER_WIN_H - 1; ++r)
            {
                model->pixel[r] = model->pixel[r + 1];
                model->h_nmintrp_out[r] = model->h_nmintrp_out[r + 1];
                model->h_out[r] = model->h_out[r + 1];
            }
            model->pixel[ADSCALER_LASTROW] = tmp_pix;
            model->h_nmintrp_out[ADSCALER_LASTROW] = tmp_h_norm_out;
            model->h_out[ADSCALER_LASTROW] = tmp_h_out;
        }
}
        // load num_rolllines input line.
        for (roll = 0; roll < model->num_rolllines; ++roll)
        {
            idx_load_line = model->y_intg + ADSCALER_HALF_WIN_H - roll;
            if (idx_load_line < 0)
                idx_load_line = 0;
            if (idx_load_line >= config->in_height[model->chn] - 1)
                idx_load_line = config->in_height[model->chn] - 1;

            ScalerModel_InputLine(model->pixel[ADSCALER_LASTROW - roll] + ADSCALER_WIN_W,
                in + config->in_width[model->chn] * idx_load_line,
                config->in_width[model->chn]);

            ScalerModel_LineBorderExtend(model->pixel[ADSCALER_LASTROW - roll], config->in_width[model->chn]);
        }
    
}

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

void ScalerModel_InputLine(ScalerPixel *dst, PixelType *src, int length)
{
    int i;
        for (i = 0; i < length; ++i)
        {
            *dst++ = (ScalerPixel)*src++;
        }
}

void ScalerModel_OutputLine(PixelType *dst, ScalerPixel *src, int length, int pixel_step)
{
    int i;
        for (i = 0; i < length; ++i)
        {
            *dst = (ScalerPixel)*src;
            ++src;++dst;
            
        }
}
