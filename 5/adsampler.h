#ifndef ADSAMPLER_H
#define ADSAMPLER_H

#include "bidirection.h"
#include "adscaler_model.h"

// 采样窗口宽度定义
#define SAMPLES_W 6
// 采样窗口高度定义
#define SAMPLES_H 6

// 采样器结构体，用于图像缩放中的采样操作
struct Adsampler
{
    // 主方向采样像素数组（SAMPLES_H x SAMPLES_W）
    AdscalerPixel main_samples[SAMPLES_H][SAMPLES_W];
    // 次方向采样像素数组（SAMPLES_H x SAMPLES_W）
    AdscalerPixel sub_samples[SAMPLES_H][SAMPLES_W];
};

// 创建一个新的采样器实例
struct Adsampler *Adsampler_New(void);

// 释放采样器实例
void Adsampler_Free(struct Adsampler **sampler);

// 执行采样操作
void Adsampler_Sampling(struct Adsampler *sampler, struct BiDirection *bidir, AdscalerPixel *win_pixel[4]);

// 执行主方向采样
void Adsampler_MainSampling(struct Adsampler *sampler, unsigned int main_dir_idx, AdscalerPixel *win_pixel[4]);

// 执行次方向采样
void Adsampler_SubSampling(struct Adsampler *sampler, unsigned int sub_dir_idx, AdscalerPixel *win_pixel[4]);

// 获取主方向采样结果
void Adsampler_GetMainSamples(struct Adsampler *sampler, struct BiDirection *bidir, int region_idx, AdscalerPixel *main_samples[ADSCALER_WIN_H]);

// 获取次方向采样结果
void Adsampler_GetSubSamples(struct Adsampler *sampler, struct BiDirection *bidir, int region_idx, AdscalerPixel *sub_samples[ADSCALER_WIN_H]);

#endif