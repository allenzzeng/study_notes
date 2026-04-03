#ifndef BIDIRECTION_H
#define BIDIRECTION_H

#include "../adscaler_config.h"
#include "adscaler_model.h"

// 方向结构体，用于表示像素的方向索引
struct Direct
{
    // 3位无符号整数，用于存储方向索引
    unsigned int idx : 3;
};

// 双向结构，用于图像缩放中的双向插值计算
struct BiDirection
{
    // 主方向
    struct Direct main_dir;
    // 次主方向
    struct Direct sub_main_dir;
    // 次方向
    struct Direct sub_dir;
    // 6个方向数组
    struct Direct dir6[4];

    // 4x6窗口的差值数组
    int diff4x6[4];
    // 4x4窗口的差值数组
    int diff4x4[8];
    // 子权重
    int sub_weights;
    // 平坦权重
    int flat_weights;

    // 权重位宽
    int w_bits;
    // 62/63阈值切换标志
    int th_62_63_switch;
    // 参数a
    int a;
    // 参数b
    int b;
    // 子方向阈值
    int th_sub;
    // 平坦区域阈值
    int th_flat;
    // 异常参数a
    int abnormal_a;
    // 异常参数b
    int abnormal_b;
    // 异常参数c
    int abnormal_c;
    // 异常参数d
    int abnormal_d;
    // 异常参数e
    int abnormal_e;
    // 异常参数f
    int abnormal_f;
    // 异常参数g
    int abnormal_g;
    // 异常参数h
    int abnormal_h;
    // 异常参数i
    int abnormal_i;
    // 不稳定参数a
    int unstable_a;
    // 不稳定参数b
    int unstable_b;

    // 调试信息输出标志
    int dump_debug;
};

// 创建一个新的BiDirection结构体
struct BiDirection *BiDirection_New(struct BiDirectionConfig *biconfig);
// 释放BiDirection结构体
void BiDirection_Free(struct BiDirection **bidir);
// 初始化BiDirection结构体
void BiDirection_Init(struct BiDirection *bidir);
// 运行双向插值计算
void BiDirection_Run(struct BiDirection *bidir, AdscalerPixel *win_pixel[4]);
// 计算4x6窗口的相似度
void BiDirection_CalcSim4x6(struct BiDirection *bidir, AdscalerPixel *win[4]);
// 计算4x4窗口的相似度
void BiDirection_CalcSim4x4(struct BiDirection *bidir, AdscalerPixel *win[4]);
// 对6个方向进行排序
void BiDirection_SortDir6(struct BiDirection *bidir, AdscalerPixel *win[4]);
// 决定主方向和次方向
void BiDirection_DecideDir(struct BiDirection *bidir);
// 计算梯度（被注释掉的函数）
//void BiDirection_CalcGradient(struct BiDirection *bidir, AdscalerPixel *win[4]);
// 计算权重
void BiDirection_CalcWeights(struct BiDirection *bidir);

#endif