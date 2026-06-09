void ScalerModel_NormalInterpolation(struct ScalerModel* model, ScaleDownConfig* config)
{
    int s, i, r;
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
