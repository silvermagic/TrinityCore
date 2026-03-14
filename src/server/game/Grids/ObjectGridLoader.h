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
 * @file ObjectGridLoader.h
 * @brief 网格对象加载器定义
 *
 * 本文件定义了一系列用于网格对象加载、卸载和管理的类。
 * 这些类实现了网格系统中的对象生命周期管理。
 *
 * 主要类:
 * - ObjectGridLoader: 加载网格中的游戏对象(生物、游戏对象等)
 * - ObjectGridStoper: 在卸载前停止生物活动
 * - ObjectGridEvacuator: 将生物移回重生点
 * - ObjectGridCleaner: 清理对象并从世界中移除
 * - ObjectGridUnloader: 卸载并删除对象
 *
 * 加载流程:
 * 1. ObjectGridLoader从数据库加载生物和游戏对象到网格
 * 2. 对象被添加到对应的单元格和世界
 *
 * 卸载流程:
 * 1. ObjectGridStoper停止所有战斗和动态对象
 * 2. ObjectGridEvacuator将跨网格生物移回重生点
 * 3. ObjectGridCleaner调用对象的清理方法
 * 4. ObjectGridUnloader删除对象并释放内存
 */

#ifndef TRINITY_OBJECTGRIDLOADER_H
#define TRINITY_OBJECTGRIDLOADER_H

#include "TypeList.h"
#include "Define.h"
#include "GridLoader.h"
#include "GridDefines.h"
#include "Cell.h"

// 前置声明
class MapObject;
class ObjectWorldLoader;

/**
 * @class ObjectGridLoader
 * @brief 网格对象加载器，负责从数据库加载游戏对象到网格
 *
 * ObjectGridLoader负责在网格激活时从数据库加载生物和游戏对象。
 * 它使用访问者模式遍历网格中的每个单元格，加载相应的对象。
 *
 * 加载过程:
 * 1. 遍历网格中的每个单元格
 * 2. 从ObjectMgr获取该单元格的对象GUID列表
 * 3. 从数据库加载每个对象的完整数据
 * 4. 将对象添加到网格和世界
 *
 * 性能考虑:
 * - 批量加载减少数据库查询次数
 * - 异步加载可避免卡顿(如果实现)
 */
class TC_GAME_API ObjectGridLoader
{
    friend class ObjectWorldLoader;  ///< 允许ObjectWorldLoader访问私有成员

    public:
        /**
         * @brief 构造函数
         *
         * @param grid 要加载的网格引用
         * @param map 所属地图指针
         * @param cell 单元格坐标信息
         *
         * 初始化加载器，准备加载指定网格的数据
         */
        ObjectGridLoader(NGridType& grid, Map* map, Cell const& cell)
            : i_cell(cell), i_grid(grid), i_map(map), i_gameObjects(0), i_creatures(0), i_corpses (0)
            { }

        /**
         * @brief 访问游戏对象管理器
         *
         * @param m 游戏对象引用管理器
         *
         * 加载单元格中的所有游戏对象到管理器
         */
        void Visit(GameObjectMapType &m);

        /**
         * @brief 访问生物管理器
         *
         * @param m 生物引用管理器
         *
         * 加载单元格中的所有生物到管理器
         */
        void Visit(CreatureMapType &m);

        /**
         * @brief 访问尸体管理器(空实现)
         *
         * @note 尸体由ObjectWorldLoader加载
         */
        void Visit(CorpseMapType &) const { }

        /**
         * @brief 访问动态对象管理器(空实现)
         *
         * @note 动态对象由其他机制管理
         */
        void Visit(DynamicObjectMapType&) const { }

        /**
         * @brief 加载整个网格
         *
         * 遍历网格中的所有单元格，加载每个单元格的对象。
         *
         * 调用时机: 网格激活时，由地图管理器调用
         *
         * 加载顺序:
         * 1. 遍历8x8个单元格
         * 2. 每个单元格加载生物和游戏对象
         * 3. 加载尸体(非骨骼)
         *
         * 性能注意: 可能需要较长时间，取决于网格中对象数量
         */
        void LoadN(void);

        /**
         * @brief 设置对象的单元格位置
         *
         * @param obj 地图对象指针
         * @param cellCoord 单元格坐标
         *
         * 静态方法，用于设置对象的当前单元格坐标。
         *
         * 调用时机: 对象加载时设置其所属单元格
         */
        static void SetObjectCell(MapObject* obj, CellCoord const& cellCoord);

    private:
        Cell i_cell;              ///< 当前单元格
        NGridType &i_grid;        ///< 要加载的网格引用
        Map* i_map;               ///< 所属地图指针
        uint32 i_gameObjects;     ///< 已加载的游戏对象计数
        uint32 i_creatures;       ///< 已加载的生物计数
        uint32 i_corpses;         ///< 已加载的尸体计数
};

/**
 * @class ObjectGridStoper
 * @brief 网格停止器，在网格卸载前停止生物活动
 *
 * 在网格卸载前，停止所有生物的战斗和动态对象。
 * 这确保生物不会在卸载过程中继续攻击或施法。
 *
 * 停止操作:
 * - 移除所有动态对象(法术效果区域等)
 * - 停止战斗状态
 *
 * 调用时机: 网格从ACTIVE转为REMOVAL状态时
 */
class TC_GAME_API ObjectGridStoper
{
    public:
        /**
         * @brief 访问生物管理器
         *
         * @param m 生物引用管理器
         *
         * 停止所有生物的战斗和动态对象
         */
        void Visit(CreatureMapType &m);

        /**
         * @brief 访问其他类型管理器(空实现)
         *
         * @tparam T 管理器类型
         *
         * 对非生物对象不做任何处理
         */
        template<class T> void Visit(GridRefManager<T> &) { }
};

/**
 * @class ObjectGridEvacuator
 * @brief 网格撤离器，将跨网格对象移回重生点
 *
 * 在网格卸载前，检查网格中的生物和游戏对象的重生点。
 * 如果重生点在其他网格，将对象移动到重生点所在网格，
 * 防止对象在原重生点网格未加载时无法重生。
 *
 * 使用场景:
 * - 生物被拉离原重生点到其他网格
 * - 网格卸载时生物不在重生点
 *
 * 调用时机: 网格卸载前，在ObjectGridStoper之后
 */
class TC_GAME_API ObjectGridEvacuator
{
    public:
        /**
         * @brief 访问生物管理器
         *
         * @param m 生物引用管理器
         *
         * 将重生点在其他网格的生物移回重生点
         */
        void Visit(CreatureMapType &m);

        /**
         * @brief 访问游戏对象管理器
         *
         * @param m 游戏对象引用管理器
         *
         * 将重生点在其他网格的游戏对象移回重生点
         */
        void Visit(GameObjectMapType &m);

        /**
         * @brief 访问其他类型管理器(空实现)
         *
         * @tparam T 管理器类型
         */
        template<class T> void Visit(GridRefManager<T> &) { }
};

/**
 * @class ObjectGridCleaner
 * @brief 网格清理器，清理对象并从世界移除
 *
 * 在网格卸载过程中，调用所有对象的CleanupsBeforeDelete方法。
 * 这允许对象在删除前执行必要的清理工作。
 *
 * 清理操作:
 * - 取消正在施放的法术
 * - 解除队伍关系
 * - 移除增益/减益效果
 * - 通知脚本系统
 *
 * 调用时机: 网格卸载过程中，在ObjectGridEvacuator之后
 */
class ObjectGridCleaner
{
    public:
        /**
         * @brief 访问任意类型管理器
         *
         * @tparam T 对象类型
         * @param m 引用管理器
         *
         * 对管理器中的所有对象调用CleanupsBeforeDelete
         */
        template<class T> void Visit(GridRefManager<T> &);
};

/**
 * @class ObjectGridUnloader
 * @brief 网格卸载器，删除对象并释放内存
 *
 * 网格卸载的最后阶段，删除所有对象并释放内存。
 * 尸体由Map管理，不在此处删除。
 *
 * 卸载流程:
 * 1. 遍历管理器中的所有对象
 * 2. 调用CleanupsBeforeDelete(双重保险)
 * 3. 删除对象(自动从管理器解除链接)
 *
 * 调用时机: 网格卸载的最后阶段
 *
 * 注意事项:
 * - 有些生物可能在CleanupsBeforeDelete中召唤其他临时生物
 * - 删除对象会自动解除与管理器的链接
 */
class ObjectGridUnloader
{
    public:
        /**
         * @brief 访问尸体管理器(空实现)
         *
         * @note 尸体由Map统一管理删除
         */
        void Visit(CorpseMapType& /*m*/) { }    // corpses are deleted with Map

        /**
         * @brief 访问其他类型管理器
         *
         * @tparam T 对象类型
         * @param m 引用管理器
         *
         * 删除管理器中的所有对象
         */
        template<class T> void Visit(GridRefManager<T> &m);
};
#endif
