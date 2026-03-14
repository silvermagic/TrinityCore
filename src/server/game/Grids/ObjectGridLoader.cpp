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
 * @file ObjectGridLoader.cpp
 * @brief 网格对象加载器实现
 *
 * 本文件实现了网格对象的加载、卸载和管理功能。
 * 这些函数在网格生命周期中被地图管理器调用。
 *
 * 主要功能:
 * - 从数据库加载游戏对象和生物到网格
 * - 处理跨网格对象的重定位
 * - 清理和卸载网格中的对象
 */

#include "ObjectGridLoader.h"
#include "CellImpl.h"
#include "Corpse.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "DynamicObject.h"
#include "Log.h"
#include "GameObject.h"
#include "GameTime.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "World.h"
#include "ScriptMgr.h"

/**
 * @brief 访问生物管理器，将跨网格生物移回重生点
 *
 * 在卸载网格时，检查网格中的生物的重生点是否在其他网格。
 * 如果是，则将生物移动到重生点所在的网格，避免重生点网格未加载时无法重生。
 *
 * @param m 生物引用管理器
 *
 * 实现细节:
 * - 遍历网格中的所有生物
 * - 断言检查是否为宠物(宠物不应该被撤离)
 * - 调用CreatureRespawnRelocation移动到重生点
 *
 * 注意: 使用迭代器遍历时先递增，因为后续操作可能修改链表
 */
void ObjectGridEvacuator::Visit(CreatureMapType &m)
{
    // creature in unloading grid can have respawn point in another grid
    // if it will be unloaded then it will not respawn in original grid until unload/load original grid
    // move to respawn point to prevent this case. For player view in respawn grid this will be normal respawn.
    // 正在卸载的网格中的生物可能在其他网格有重生点
    // 如果它被卸载，则在原始网格加载前不会在原始网格重生
    // 移动到重生点以防止这种情况。对于玩家来说，这看起来就像是正常的重生。

    for (CreatureMapType::iterator iter = m.begin(); iter != m.end();)
    {
        Creature* c = iter->GetSource();
        ++iter;  // 先递增迭代器，因为后续操作可能修改链表

        // 宠物不应该被撤离(宠物跟随主人，没有独立重生点)
        ASSERT(!c->IsPet() && "ObjectGridRespawnMover must not be called for pets");
        // 将生物移回重生点，true表示强制移动
        c->GetMap()->CreatureRespawnRelocation(c, true);
    }
}

/**
 * @brief 访问游戏对象管理器，将跨网格游戏对象移回重生点
 *
 * 与生物撤离类似，处理游戏对象的重定位。
 *
 * @param m 游戏对象引用管理器
 *
 * 使用场景: 矿石、草药等可采集对象被移动到其他网格
 */
void ObjectGridEvacuator::Visit(GameObjectMapType &m)
{
    // gameobject in unloading grid can have respawn point in another grid
    // if it will be unloaded then it will not respawn in original grid until unload/load original grid
    // move to respawn point to prevent this case. For player view in respawn grid this will be normal respawn.
    // 正在卸载的网格中的游戏对象可能在其他网格有重生点

    for (GameObjectMapType::iterator iter = m.begin(); iter != m.end();)
    {
        GameObject* go = iter->GetSource();
        ++iter;  // 先递增迭代器

        // 将游戏对象移回重生点
        go->GetMap()->GameObjectRespawnRelocation(go, true);
    }
}

/**
 * @class ObjectWorldLoader
 * @brief 世界对象加载器，用于加载尸体等世界对象
 *
 * 与ObjectGridLoader不同，ObjectWorldLoader处理世界对象(如尸体)。
 * 尸体存储在世界对象容器中，而不是网格对象容器中。
 *
 * 加载流程:
 * 1. 从地图获取指定单元格的尸体列表
 * 2. 将尸体添加到世界和网格容器
 *
 * 注意事项:
 * - 尸体分为可复活尸体和骨骼两种
 * - 可复活尸体存储在世界对象容器
 * - 骨骼存储在网格对象容器
 */
class ObjectWorldLoader
{
    public:
        /**
         * @brief 构造函数
         *
         * @param gloader 父加载器引用，共享数据
         */
        explicit ObjectWorldLoader(ObjectGridLoader& gloader)
            : i_cell(gloader.i_cell), i_map(gloader.i_map), i_grid(gloader.i_grid), i_corpses(gloader.i_corpses)
            { }

        /**
         * @brief 访问尸体管理器
         *
         * @param m 尸体引用管理器(未使用)
         *
         * 加载单元格中的所有尸体。
         * 尸体列表从Map获取，而不是从数据库。
         */
        void Visit(CorpseMapType &m);

        /**
         * @brief 访问其他类型管理器(空实现)
         */
        template<class T> void Visit(GridRefManager<T>&) { }

    private:
        Cell i_cell;              ///< 当前单元格
        Map* i_map;               ///< 所属地图
        NGridType& i_grid;        ///< 目标网格
    public:
        uint32& i_corpses;        ///< 尸体计数引用(与父加载器共享)
};

/**
 * @brief 设置对象的单元格坐标
 *
 * @param obj 地图对象指针
 * @param cellCoord 单元格坐标
 *
 * 设置对象的当前单元格，用于后续的网格定位和更新。
 */
void ObjectGridLoader::SetObjectCell(MapObject* obj, CellCoord const& cellCoord)
{
    Cell cell(cellCoord);
    obj->SetCurrentCell(cell);
}

/**
 * @brief 辅助函数，将对象添加到网格和世界
 *
 * @tparam T 对象类型
 * @param cell 单元格坐标
 * @param m 引用管理器
 * @param count 计数器引用
 * @param map 地图指针
 * @param obj 要添加的对象
 *
 * 执行步骤:
 * 1. 将对象添加到网格管理器
 * 2. 设置对象的单元格坐标
 * 3. 将对象添加到世界
 * 4. 如果是活动对象，添加到活动对象列表
 * 5. 递增计数器
 */
template <class T>
void AddObjectHelper(CellCoord &cell, GridRefManager<T> &m, uint32 &count, Map* map, T *obj)
{
    // 将对象添加到网格管理器
    obj->AddToGrid(m);
    // 设置对象的单元格坐标
    ObjectGridLoader::SetObjectCell(obj, cell);
    // 将对象添加到世界(触发OnAddToWorld等事件)
    obj->AddToWorld();
    // 如果是活动对象，添加到地图的活动对象列表
    if (obj->isActiveObject())
        map->AddToActive(obj);

    ++count;
}

/**
 * @brief 辅助函数，从数据库加载对象
 *
 * @tparam T 对象类型
 * @param guid_set GUID集合
 * @param cell 单元格坐标
 * @param m 引用管理器
 * @param count 计数器引用
 * @param map 地图指针
 *
 * 加载流程:
 * 1. 遍历GUID集合
 * 2. 检查是否应该在网格加载时生成
 * 3. 创建对象并从数据库加载
 * 4. 调用AddObjectHelper完成添加
 *
 * 注意事项:
 * - 如果有重生计时器，跳过加载
 * - 如果加载失败，删除对象并继续下一个
 */
template <class T>
void LoadHelper(CellGuidSet const& guid_set, CellCoord &cell, GridRefManager<T> &m, uint32 &count, Map* map)
{
    for (CellGuidSet::const_iterator i_guid = guid_set.begin(); i_guid != guid_set.end(); ++i_guid)
    {
        // Don't spawn at all if there's a respawn timer
        // 如果有重生计时器，则根本不生成
        ObjectGuid::LowType guid = *i_guid;
        // 检查该对象是否应该在网格加载时生成
        if (!map->ShouldBeSpawnedOnGridLoad<T>(guid))
            continue;

        T* obj = new T;
        //TC_LOG_INFO("misc", "DEBUG: LoadHelper from table: {} for (guid: {}) Loading", table, guid);
        // 从数据库加载对象数据
        if (!obj->LoadFromDB(guid, map, false, false))
        {
            // 加载失败，删除对象
            delete obj;
            continue;
        }
        // 成功加载，添加到网格和世界
        AddObjectHelper(cell, m, count, map, obj);
    }
}

/**
 * @brief 访问游戏对象管理器，加载游戏对象
 *
 * @param m 游戏对象引用管理器
 *
 * 从ObjectMgr获取单元格的游戏对象GUID列表，然后加载每个对象。
 */
void ObjectGridLoader::Visit(GameObjectMapType &m)
{
    CellCoord cellCoord = i_cell.GetCellCoord();
    // 从ObjectMgr获取该单元格的游戏对象GUID列表
    if (CellObjectGuids const* cell_guids = sObjectMgr->GetCellObjectGuids(i_map->GetId(), i_map->GetSpawnMode(), cellCoord.GetId()))
        LoadHelper(cell_guids->gameobjects, cellCoord, m, i_gameObjects, i_map);
}

/**
 * @brief 访问生物管理器，加载生物
 *
 * @param m 生物引用管理器
 *
 * 从ObjectMgr获取单元格的生物GUID列表，然后加载每个生物。
 */
void ObjectGridLoader::Visit(CreatureMapType &m)
{
    CellCoord cellCoord = i_cell.GetCellCoord();
    // 从ObjectMgr获取该单元格的生物GUID列表
    if (CellObjectGuids const* cell_guids = sObjectMgr->GetCellObjectGuids(i_map->GetId(), i_map->GetSpawnMode(), cellCoord.GetId()))
        LoadHelper(cell_guids->creatures, cellCoord, m, i_creatures, i_map);
}

/**
 * @brief 访问尸体管理器，加载尸体
 *
 * @param m 尸体引用管理器(未使用)
 *
 * 从地图获取单元格的尸体列表并添加到网格。
 * 与生物和游戏对象不同，尸体不存储在数据库中。
 */
void ObjectWorldLoader::Visit(CorpseMapType& /*m*/)
{
    CellCoord cellCoord = i_cell.GetCellCoord();
    // 从地图获取该单元格的尸体列表
    if (std::unordered_set<Corpse*> const* corpses = i_map->GetCorpsesInCell(cellCoord.GetId()))
    {
        for (Corpse* corpse : *corpses)
        {
            // 将尸体添加到世界
            corpse->AddToWorld();
            // 获取对应的单元格
            GridType& cell = i_grid.GetGridType(i_cell.CellX(), i_cell.CellY());
            // 根据尸体类型添加到不同的容器
            if (corpse->IsStoredInWorldObjectGridContainer())
                // 可复活的尸体存储在世界对象容器
                cell.AddWorldObject(corpse);
            else
                // 骨骼存储在网格对象容器
                cell.AddGridObject(corpse);

            ++i_corpses;
        }
    }
}

/**
 * @brief 加载整个网格的所有单元格
 *
 * 遍历网格中的所有单元格(8x8=64个)，依次加载每个单元格的对象。
 *
 * 加载流程:
 * 1. 重置计数器
 * 2. 遍历所有单元格
 * 3. 对每个单元格:
 *    a. 加载生物和游戏对象(网格对象)
 *    b. 加载尸体(世界对象)
 * 4. 记录日志
 *
 * 调用时机: 网格激活时
 */
void ObjectGridLoader::LoadN(void)
{
    // 重置计数器
    i_gameObjects = 0; i_creatures = 0; i_corpses = 0;
    i_cell.data.Part.cell_y = 0;

    // 遍历所有单元格
    for (uint32 x = 0; x < MAX_NUMBER_OF_CELLS; ++x)
    {
        i_cell.data.Part.cell_x = x;
        for (uint32 y = 0; y < MAX_NUMBER_OF_CELLS; ++y)
        {
            i_cell.data.Part.cell_y = y;

            //Load creatures and game objects
            // 加载生物和游戏对象
            {
                // 创建访问者并访问单元格
                TypeContainerVisitor<ObjectGridLoader, GridTypeMapContainer> visitor(*this);
                i_grid.VisitGrid(x, y, visitor);
            }

            //Load corpses (not bones)
            // 加载尸体(非骨骼)
            {
                ObjectWorldLoader worker(*this);
                TypeContainerVisitor<ObjectWorldLoader, WorldTypeMapContainer> visitor(worker);
                i_grid.VisitGrid(x, y, visitor);
            }
        }
    }
    // 记录加载日志
    TC_LOG_DEBUG("maps", "{} GameObjects, {} Creatures, and {} Corpses/Bones loaded for grid {} on map {}",
        i_gameObjects, i_creatures, i_corpses, i_grid.GetGridId(), i_map->GetId());
}

/**
 * @brief 访问管理器，删除所有对象
 *
 * @tparam T 对象类型
 * @param m 引用管理器
 *
 * 删除流程:
 * 1. 循环直到管理器为空
 * 2. 获取第一个对象
 * 3. 调用CleanupsBeforeDelete(双重保险)
 * 4. 删除对象(自动解除链接)
 *
 * 注意事项:
 * - CleanupsBeforeDelete可能在删除时召唤其他临时生物
 * - 例如：烈焰巨兽炮塔33139在生物删除时被召唤
 * - 删除对象会自动从管理器解除链接
 */
template<class T>
void ObjectGridUnloader::Visit(GridRefManager<T> &m)
{
    while (!m.isEmpty())
    {
        T *obj = m.getFirst()->GetSource();
        //Some creatures may summon other temp summons in CleanupsBeforeDelete()
        //So we need this even after cleaner (maybe we can remove cleaner)
        //Example: Flame Leviathan Turret 33139 is summoned when a creature is deleted
        // 有些生物可能在CleanupsBeforeDelete中召唤其他临时召唤物
        // 所以即使在cleaner之后我们也需要这个(也许可以移除cleaner)
        // 例如：烈焰巨兽炮塔33139在生物删除时被召唤
        /// @todo Check if that script has the correct logic. Do we really need to summons something before deleting?
        /// 检查那个脚本是否有正确的逻辑。我们真的需要在删除前召唤什么吗？
        obj->CleanupsBeforeDelete();
        ///- object will get delinked from the manager when deleted
        /// 对象删除时会自动从管理器解除链接
        delete obj;
    }
}

/**
 * @brief 访问生物管理器，停止所有生物活动
 *
 * @param m 生物引用管理器
 *
 * 在网格卸载前停止所有生物的战斗和动态对象。
 */
void ObjectGridStoper::Visit(CreatureMapType &m)
{
    // stop any fights at grid de-activation and remove dynobjects created at cast by creatures
    // 在网格停用时停止所有战斗，并移除生物施法创建的动态对象
    for (CreatureMapType::iterator iter = m.begin(); iter != m.end(); ++iter)
    {
        // 移除所有动态对象(法术效果区域等)
        iter->GetSource()->RemoveAllDynObjects();
        // 如果在战斗中，停止战斗
        if (iter->GetSource()->IsInCombat())
            iter->GetSource()->CombatStop();
    }
}

/**
 * @brief 访问管理器，清理所有对象
 *
 * @tparam T 对象类型
 * @param m 引用管理器
 *
 * 对每个对象调用CleanupsBeforeDelete方法。
 */
template<class T>
void ObjectGridCleaner::Visit(GridRefManager<T> &m)
{
    for (typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
        iter->GetSource()->CleanupsBeforeDelete();
}

// 模板显式实例化，确保链接正确
template void ObjectGridUnloader::Visit(CreatureMapType &);
template void ObjectGridUnloader::Visit(GameObjectMapType &);
template void ObjectGridUnloader::Visit(DynamicObjectMapType &);

template void ObjectGridCleaner::Visit(CreatureMapType &);
template void ObjectGridCleaner::Visit<GameObject>(GameObjectMapType &);
template void ObjectGridCleaner::Visit<DynamicObject>(DynamicObjectMapType &);
template void ObjectGridCleaner::Visit<Corpse>(CorpseMapType &);
