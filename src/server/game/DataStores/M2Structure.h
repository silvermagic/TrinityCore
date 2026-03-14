/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file M2Structure.h
 * @brief M2 模型文件格式结构定义
 *
 * 本文件定义了 WoW 中 M2 模型文件格式的数据结构。M2 文件是 WoW 使用的 3D 模型格式，
 * 包含角色、生物、物品等游戏对象的三维模型数据。
 *
 * M2 文件包含：
 * - 模型几何数据（顶点、面片）
 * - 骨骼动画系统
 * - 纹理和材质信息
 * - 粒子和光效系统
 * - 摄像机定义
 * - 附加点（attachments）
 *
 * 参考资料：https://wowdev.wiki
 */

#ifndef TRINITY_M2STRUCTURE_H
#define TRINITY_M2STRUCTURE_H

#include <G3D/Vector3.h>
#include <G3D/AABox.h>

/**
 * @brief M2 文件结构定义
 *
 * 本命名空间包含 M2 模型文件格式的所有数据结构定义。
 * 这些结构直接映射到 M2 文件的二进制布局。
 */

/**
 * @brief M2 样条关键帧模板结构
 * @tparam T 关键帧数据类型（通常为 float 或 Vector3）
 *
 * 用于动画插值的样条关键帧，包含三个控制点用于三次样条插值。
 * 在骨骼动画、摄像机移动等场景中使用。
 */
#pragma pack(push, 1)
template<typename T>
struct M2SplineKey
{
    T p0;  ///< 样条控制点 0（起始点）
    T p1;  ///< 样条控制点 1（中间控制点）
    T p2;  ///< 样条控制点 2（结束点）
};

/**
 * @brief M2 模型文件头结构
 *
 * M2 文件的主头部结构，包含模型的所有元数据和各数据块的偏移量。
 * 这是读取 M2 文件的入口点，通过此结构可以访问模型的所有数据。
 *
 * 文件结构：
 * - 文件头（本结构）
 * - 各种数据块（通过偏移量访问）
 */
struct M2Header
{
    char   Magic[4];               ///< 魔数标识，固定为 "MD20"
    uint32 Version;                ///< 格式版本号
    uint32 lName;                  ///< 模型名称长度（包含结尾的 \0）
    uint32 ofsName;                ///< 模型名称的文件偏移量（名称应唯一，用于模型重载）
    uint32 GlobalModelFlags;       ///< 全局模型标志位
                                   /**<
                                    * 标志位定义：
                                    * - 0x0001: tilt x（X 轴倾斜）
                                    * - 0x0002: tilt y（Y 轴倾斜）
                                    * - 0x0008: 头部包含 2 个额外字段
                                    * - 0x0020: 加载 .phys 数据（MoP+）
                                    * - 0x0080: 具有 _lod .skin 文件（MoP?+）
                                    * - 0x0100: 与摄像机相关
                                    */
    uint32 nGlobalSequences;       ///< 全局序列数量
    uint32 ofsGlobalSequences;     ///< 全局序列数组偏移量（时间戳列表）
    uint32 nAnimations;            ///< 动画数量
    uint32 ofsAnimations;          ///< 动画信息数组偏移量
    uint32 nAnimationLookup;       ///< 动画查找表数量
    uint32 ofsAnimationLookup;     ///< 动画查找表偏移量（将全局 ID 映射到动画序列块）
    uint32 nBones;                 ///< 骨骼数量（最大值 MAX_BONES = 0x100）
    uint32 ofsBones;               ///< 骨骼信息数组偏移量
    uint32 nKeyBoneLookup;         ///< 关键骨骼查找表数量
    uint32 ofsKeyBoneLookup;       ///< 关键骨骼查找表偏移量
    uint32 nVertices;              ///< 顶点数量
    uint32 ofsVertices;            ///< 顶点数组偏移量
    uint32 nViews;                 ///< 视图（LOD）数量（现在存储在 .skin 文件中）
    uint32 nSubmeshAnimations;     ///< 子网格动画数量
    uint32 ofsSubmeshAnimations;   ///< 子网格动画偏移量（子网格颜色和透明度动画定义）
    uint32 nTextures;              ///< 纹理数量
    uint32 ofsTextures;            ///< 纹理数组偏移量
    uint32 nTransparency;          ///< 透明度数据数量
    uint32 ofsTransparency;        ///< 透明度数组偏移量
    uint32 nUVAnimation;           ///< UV 动画数量
    uint32 ofsUVAnimation;         ///< UV 动画偏移量
    uint32 nTexReplace;            ///< 可替换纹理数量
    uint32 ofsTexReplace;          ///< 可替换纹理偏移量
    uint32 nRenderFlags;           ///< 渲染标志数量
    uint32 ofsRenderFlags;         ///< 渲染标志偏移量（混合模式/渲染标志）
    uint32 nBoneLookupTable;       ///< 骨骼查找表大小
    uint32 ofsBoneLookupTable;     ///< 骨骼查找表偏移量
    uint32 nTexLookup;             ///< 纹理查找表大小
    uint32 ofsTexLookup;           ///< 纹理查找表偏移量
    uint32 nTexUnits;              ///< 纹理单元数量（可能在 Cata 版本中移除）
    uint32 ofsTexUnits;            ///< 纹理单元偏移量
    uint32 nTransLookup;           ///< 透明度查找表大小
    uint32 ofsTransLookup;         ///< 透明度查找表偏移量
    uint32 nUVAnimLookup;          ///< UV 动画查找表大小
    uint32 ofsUVAnimLookup;        ///< UV 动画查找表偏移量
    G3D::AABox BoundingBox;        ///< 包围盒（Bounding Box）
                                   /**<
                                    * 模型的轴对齐包围盒。
                                    * 注意：最大摄像机高度约为 min/max([1].z, 2.0277779f) - 0.16f
                                    */
    float  BoundingSphereRadius;   ///< 包围球半径
    G3D::AABox CollisionBox;       ///< 碰撞盒（用于物理碰撞检测）
    float  CollisionSphereRadius;  ///< 碰撞球半径
    uint32 nBoundingTriangles;     ///< 边界三角形数量
    uint32 ofsBoundingTriangles;   ///< 边界三角形偏移量（边界体积，类似于旧的 ofsViews）
    uint32 nBoundingVertices;      ///< 边界顶点数量
    uint32 ofsBoundingVertices;    ///< 边界顶点偏移量
    uint32 nBoundingNormals;       ///< 边界法线数量
    uint32 ofsBoundingNormals;     ///< 边界法线偏移量
    uint32 nAttachments;           ///< 附加点数量
    uint32 ofsAttachments;         ///< 附加点偏移量（用于武器挂载等）
    uint32 nAttachLookup;          ///< 附加点查找表大小
    uint32 ofsAttachLookup;        ///< 附加点查找表偏移量
    uint32 nEvents;                ///< 事件数量
    uint32 ofsEvents;              ///< 事件偏移量（用于死亡时播放声音等）
    uint32 nLights;                ///< 光源数量
    uint32 ofsLights;              ///< 光源偏移量（主要用于登录界面，也用于魔杖和一些装饰物）
    uint32 nCameras;               ///< 摄像机数量（格式在版本 271 中有所改变！）
    uint32 ofsCameras;             ///< 摄像机偏移量（大多数模型有摄像机用于角色界面显示）
    uint32 nCameraLookup;          ///< 摄像机查找表大小
    uint32 ofsCameraLookup;        ///< 摄像机查找表偏移量
    uint32 nRibbonEmitters;        ///< 飘带发射器数量
    uint32 ofsRibbonEmitters;      ///< 飘带发射器偏移量（旋转效果，见时光之穴入口的光迹）
    uint32 nParticleEmitters;      ///< 粒子发射器数量
    uint32 ofsParticleEmitters;    ///< 粒子发射器偏移量
                                   /**<
                                    * 粒子系统用于：
                                    * - 法术效果
                                    * - 武器特效
                                    * - 装饰物
                                    * - 登录界面
                                    * 例如：刀刃上的血滴效果
                                    */
    uint32 nBlendMaps;             ///< 混合贴图数量
                                   /**<
                                    * 仅当 (flags & 0x8) != 0 时存在。
                                    * 设置后，纹理混合会被相关数组覆盖。
                                    * 参见 M2/WotLK#Blend_mode_overrides
                                    */
    uint32 ofsBlendMaps;           ///< 混合贴图偏移量（指向 nBlendMaps 个 uint16 的数组，来自 WoD 信息）
};

/**
 * @brief M2 数组结构
 *
 * M2 文件中用于引用数据块的通用数组结构。
 * 包含元素数量和相对偏移量，用于定位文件中的各种数据块。
 */
struct M2Array
{
    uint32_t number;           ///< 数组元素数量
    uint32_t offset_elements;  ///< 数组元素的偏移量（相对于文件起始位置）
};

/**
 * @brief M2 轨道结构
 *
 * 用于存储随时间变化的数据，支持不同类型的插值。
 * 广泛应用于动画系统，如骨骼变换、透明度、纹理坐标等。
 */
struct M2Track
{
    uint16_t interpolation_type;  ///< 插值类型
                                  /**<
                                   * 插值类型定义：
                                   * - 0: 无插值（常量）
                                   * - 1: 线性插值
                                   * - 2: 三次样条插值
                                   */
    uint16_t global_sequence;     ///< 全局序列索引（0xFFFF 表示不使用全局序列）
    M2Array timestamps;           ///< 时间戳数组（关键帧的时间点）
    M2Array values;               ///< 值数组（关键帧的数据值）
};

/**
 * @brief M2 摄像机结构
 *
 * 定义 M2 模型中的摄像机，用于角色选择界面、过场动画等场景。
 * 支持摄像机位置和目标位置的动画轨道。
 */
struct M2Camera
{
    uint32_t type;  ///< 摄像机类型
                    /**<
                     * 类型值：
                     * - 0: 肖像摄像机（Portrait）
                     * - 1: 角色信息摄像机（Character Info）
                     * - -1: 其他（飞越摄像机等）
                     * 注意：在查找表中反向引用
                     */
    float fov;      ///< 视场角（Field of View）
                    /**<
                     * 不是弧度也不是度数。
                     * 乘以 35 得到度数值。
                     */
    float far_clip;             ///< 远裁剪距离
    float near_clip;            ///< 近裁剪距离
    M2Track positions;          ///< 摄像机位置动画轨道（应为 3*3 浮点数）
    G3D::Vector3 position_base; ///< 摄像机基础位置
    M2Track target_positions;   ///< 目标位置动画轨道（应为 3*3 浮点数）
    G3D::Vector3 target_position_base; ///< 目标基础位置
    M2Track rolldata;           ///< 翻滚数据轨道（摄像机翻滚效果，范围 0 到 2*Pi）
};
#pragma pack(pop)

#endif
