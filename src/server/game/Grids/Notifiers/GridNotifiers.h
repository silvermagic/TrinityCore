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
 * @file GridNotifiers.h
 * @brief 网格通知器模块 - 提供网格对象访问和操作的核心通知器实现
 *
 * 本文件定义了 TrinityCore 网格系统中的通知器（Notifier）类族，用于：
 * - 遍历网格中的对象并执行特定操作
 * - 处理玩家可见性更新和对象重定位通知
 * - 实现消息广播和距离检测
 * - 提供各种对象搜索器和检查器
 *
 * 设计模式：
 * - 访问者模式（Visitor Pattern）：通过 Visit() 方法遍历网格对象
 * - 策略模式（Strategy Pattern）：通过模板参数定制检查和操作逻辑
 *
 * 核心类型：
 * - 可见性通知器：处理对象可见性变化
 * - 重定位通知器：处理对象移动事件
 * - 消息投递器：广播网络消息
 * - 对象搜索器：查找满足条件的对象
 * - 条件检查器：提供各种筛选条件
 *
 * @see GridRefManager
 * @see Cell
 * @see Map
 */

#ifndef TRINITY_GRIDNOTIFIERS_H
#define TRINITY_GRIDNOTIFIERS_H

#include "Creature.h"
#include "Corpse.h"
#include "CreatureAI.h"
#include "DynamicObject.h"
#include "GameObject.h"
#include "Group.h"
#include "Player.h"
#include "Spell.h"
#include "SpellInfo.h"
#include "UnitAI.h"
#include "UpdateData.h"

/**
 * @namespace Trinity
 * @brief TrinityCore 核心命名空间，包含游戏核心功能的实现
 */
namespace Trinity
{
    /**
     * @struct VisibleNotifier
     * @brief 可见性通知器 - 处理玩家视野范围内的对象可见性更新
     *
     * 当玩家移动或网格中的对象状态变化时，需要更新玩家客户端的对象可见性列表。
     * 此通知器负责：
     * - 检测新进入玩家视野的对象
     * - 移除离开玩家视野的对象
     * - 生成更新数据包发送给客户端
     *
     * 工作流程：
     * 1. Visit() 遍历网格中的对象，检查是否在玩家视野内
     * 2. 更新 vis_guids 集合，记录当前可见对象
     * 3. SendToSelf() 将更新数据发送给玩家
     *
     * @note 性能关键路径，每帧每个移动的玩家都会调用
     */
    struct TC_GAME_API VisibleNotifier
    {
        Player &i_player;                   ///< 需要更新可见性的玩家引用
        UpdateData i_data;                  ///< 累积的更新数据包
        std::set<Unit*> i_visibleNow;       ///< 当前帧新可见的单位集合
        GuidUnorderedSet vis_guids;         ///< 当前可见对象的 GUID 集合（用于比对变化）

        /**
         * @brief 构造可见性通知器
         * @param player 需要更新可见性的玩家
         * @note 初始化 vis_guids 为玩家当前的客户端 GUID 列表
         */
        VisibleNotifier(Player &player) : i_player(player), vis_guids(player.m_clientGUIDs) { }

        /**
         * @brief 访问网格对象管理器，检查可见性
         * @tparam T 对象类型（Player, Creature, GameObject 等）
         * @param m 网格对象引用管理器
         */
        template<class T> void Visit(GridRefManager<T> &m);

        /**
         * @brief 将累积的更新数据发送给玩家自己
         */
        void SendToSelf(void);
    };

    /**
     * @struct VisibleChangesNotifier
     * @brief 可见性变化通知器 - 通知周围对象某对象发生了可见性相关变化
     *
     * 当一个 WorldObject 的外观或状态发生变化时（如装备变更、变形等），
     * 需要通知周围能看见它的玩家更新显示。
     *
     * 与 VisibleNotifier 的区别：
     * - VisibleNotifier：玩家移动时更新自己看到什么
     * - VisibleChangesNotifier：对象变化时通知别人看到什么
     */
    struct VisibleChangesNotifier
    {
        WorldObject &i_object;              ///< 发生变化的对象

        /**
         * @brief 构造可见性变化通知器
         * @param object 发生变化的世界对象
         */
        explicit VisibleChangesNotifier(WorldObject &object) : i_object(object) { }

        template<class T> void Visit(GridRefManager<T> &) { }
        void Visit(PlayerMapType &);        ///< 访问玩家集合，通知变化
        void Visit(CreatureMapType &);      ///< 访问生物集合，通知变化
        void Visit(DynamicObjectMapType &); ///< 访问动态对象集合，通知变化
    };

    /**
     * @struct PlayerRelocationNotifier
     * @brief 玩家重定位通知器 - 处理玩家移动时的通知逻辑
     *
     * 继承自 VisibleNotifier，在玩家位置更新时触发：
     * 1. 父类的可见性更新（新进入/离开视野的对象）
     * 2. 额外的生物和玩家交互检测（如 agro 范围检测）
     *
     * @see VisibleNotifier
     */
    struct TC_GAME_API PlayerRelocationNotifier : public VisibleNotifier
    {
        /**
         * @brief 构造玩家重定位通知器
         * @param player 移动的玩家
         */
        PlayerRelocationNotifier(Player &player) : VisibleNotifier(player) { }

        template<class T> void Visit(GridRefManager<T> &m) { VisibleNotifier::Visit(m); }
        void Visit(CreatureMapType &);      ///< 处理与生物的交互（如进入 agro 范围）
        void Visit(PlayerMapType &);        ///< 处理与其他玩家的交互
    };

    /**
     * @struct CreatureRelocationNotifier
     * @brief 生物重定位通知器 - 处理生物移动时的通知逻辑
     *
     * 当生物位置变化时，需要通知周围能看见它的玩家。
     * 同时可能触发 AI 相关的位置感知逻辑。
     */
    struct TC_GAME_API CreatureRelocationNotifier
    {
        Creature &i_creature;               ///< 移动的生物

        /**
         * @brief 构造生物重定位通知器
         * @param c 移动的生物
         */
        CreatureRelocationNotifier(Creature &c) : i_creature(c) { }

        template<class T> void Visit(GridRefManager<T> &) { }
        void Visit(CreatureMapType &);      ///< 通知周围生物
        void Visit(PlayerMapType &);        ///< 通知周围玩家
    };

    /**
     * @struct DelayedUnitRelocation
     * @brief 延迟单位重定位处理器 - 处理延迟的单位位置更新
     *
     * 某些情况下，单位的位置更新需要延迟处理以避免在同一帧内
     * 多次更新导致的性能问题或逻辑冲突。
     * 此通知器在下一帧处理累积的重定位请求。
     */
    struct TC_GAME_API DelayedUnitRelocation
    {
        Map &i_map;                         ///< 当前地图实例
        Cell &cell;                         ///< 当前单元格
        CellCoord &p;                       ///< 单元格坐标
        const float i_radius;               ///< 搜索半径

        /**
         * @brief 构造延迟重定位处理器
         * @param c 单元格引用
         * @param pair 单元格坐标
         * @param map 地图实例
         * @param radius 搜索半径
         */
        DelayedUnitRelocation(Cell &c, CellCoord &pair, Map &map, float radius) :
            i_map(map), cell(c), p(pair), i_radius(radius) { }

        template<class T> void Visit(GridRefManager<T> &) { }
        void Visit(CreatureMapType &);      ///< 处理延迟的生物重定位
        void Visit(PlayerMapType   &);      ///< 处理延迟的玩家重定位
    };

    /**
     * @struct AIRelocationNotifier
     * @brief AI 重定位通知器 - 在单位移动时触发 AI 逻辑
     *
     * 当单位移动时，需要通知 AI 系统进行相应的处理：
     * - 检测新进入感知范围的目标
     * - 更新巡逻路径
     * - 触发移动相关的 AI 事件
     */
    struct TC_GAME_API AIRelocationNotifier
    {
        Unit &i_unit;                       ///< 移动的单位
        bool isCreature;                    ///< 是否为生物（非玩家）

        /**
         * @brief 构造 AI 重定位通知器
         * @param unit 移动的单位
         */
        explicit AIRelocationNotifier(Unit &unit) : i_unit(unit), isCreature(unit.GetTypeId() == TYPEID_UNIT)  { }

        template<class T> void Visit(GridRefManager<T> &) { }
        void Visit(CreatureMapType &);      ///< 通知生物 AI 处理移动
    };

    /**
     * @struct GridUpdater
     * @brief 网格更新器 - 对网格中的所有对象执行 Update() 调用
     *
     * 每个游戏循环中，需要对网格中的所有活动对象调用 Update() 方法，
     * 让对象执行自己的更新逻辑（如 AI 思考、定时器处理等）。
     *
     * @note 此更新器不更新玩家和尸体（它们有独立的更新流程）
     */
    struct GridUpdater
    {
        GridType &i_grid;                   ///< 要更新的网格
        uint32 i_timeDiff;                  ///< 自上次更新以来经过的时间（毫秒）

        /**
         * @brief 构造网格更新器
         * @param grid 要更新的网格
         * @param diff 时间差（毫秒）
         */
        GridUpdater(GridType &grid, uint32 diff) : i_grid(grid), i_timeDiff(diff) { }

        /**
         * @brief 更新对象管理器中的所有对象
         * @tparam T 对象类型
         * @param m 对象引用管理器
         */
        template<class T> void updateObjects(GridRefManager<T> &m)
        {
            for (typename GridRefManager<T>::iterator iter = m.begin(); iter != m.end(); ++iter)
                iter->GetSource()->Update(i_timeDiff);
        }

        void Visit(PlayerMapType &m) { updateObjects<Player>(m); }
        void Visit(CreatureMapType &m){ updateObjects<Creature>(m); }
        void Visit(GameObjectMapType &m) { updateObjects<GameObject>(m); }
        void Visit(DynamicObjectMapType &m) { updateObjects<DynamicObject>(m); }
        void Visit(CorpseMapType &m) { updateObjects<Corpse>(m); }
    };

    /**
     * @struct MessageDistDeliverer
     * @brief 消息距离投递器 - 向指定范围内的玩家广播消息
     *
     * 用于向源对象周围一定距离内的玩家发送网络消息包。
     * 支持多种过滤条件：
     * - 距离限制
     * - 阵营过滤（仅同阵营）
     * - 跳过特定接收者
     * - 2D/3D 距离计算选择
     *
     * 典型用途：
     * - 广播聊天消息
     * - 广播表情和动画
     * - 广播环境音效
     */
    struct TC_GAME_API MessageDistDeliverer
    {
        WorldObject const* i_source;        ///< 消息源对象
        WorldPacket const* i_message;       ///< 要发送的消息包
        uint32 i_phaseMask;                 ///< 相位掩码（用于相位检测）
        float i_distSq;                     ///< 距离的平方（避免开方运算）
        uint32 team;                        ///< 阵营 ID（0 表示不过滤阵营）
        Player const* skipped_receiver;     ///< 要跳过的接收者（通常是发送者自己）
        bool required3dDist;                ///< 是否使用 3D 距离（否则使用 2D）

        /**
         * @brief 构造消息距离投递器
         * @param src 消息源对象
         * @param msg 要发送的消息包
         * @param dist 最大距离
         * @param own_team_only 是否仅发送给同阵营玩家
         * @param skipped 要跳过的接收者
         * @param req3dDist 是否使用 3D 距离检测
         */
        MessageDistDeliverer(WorldObject const* src, WorldPacket const* msg, float dist, bool own_team_only = false, Player const* skipped = nullptr, bool req3dDist = false)
            : i_source(src), i_message(msg), i_phaseMask(src->GetPhaseMask()), i_distSq(dist * dist)
            , team(0)
            , skipped_receiver(skipped)
            , required3dDist(req3dDist)
        {
            // 如果仅发送给同阵营，获取源对象的阵营
            if (own_team_only)
                if (Player const* player = src->ToPlayer())
                    team = player->GetTeam();
        }

        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(DynamicObjectMapType &m);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) { }

        /**
         * @brief 向单个玩家发送消息
         * @param player 目标玩家
         *
         * 过滤条件：
         * - 不发送给自己
         * - 阵营检查
         * - 跳过指定接收者
         * - 必须能看见源对象
         */
        void SendPacket(Player* player)
        {
            // never send packet to self
            if (player == i_source || (team && player->GetTeam() != team) || skipped_receiver == player)
                return;

            if (!player->HaveAtClient(i_source))
                return;

            player->SendDirectMessage(i_message);
        }
    };

    /**
     * @struct MessageDistDelivererToHostile
     * @brief 敌对消息投递器 - 向敌对阵营玩家广播消息
     *
     * 专门用于向源对象的敌对目标发送消息。
     * 典型用途：
     * - PVP 战斗消息
     * - 敌对阵营特定事件通知
     */
    struct TC_GAME_API MessageDistDelivererToHostile
    {
        Unit* i_source;                     ///< 消息源单位
        WorldPacket const* i_message;       ///< 要发送的消息包
        uint32 i_phaseMask;                 ///< 相位掩码
        float i_distSq;                     ///< 距离的平方

        /**
         * @brief 构造敌对消息投递器
         * @param src 源单位
         * @param msg 消息包
         * @param dist 最大距离
         */
        MessageDistDelivererToHostile(Unit* src, WorldPacket const* msg, float dist)
            : i_source(src), i_message(msg), i_phaseMask(src->GetPhaseMask()), i_distSq(dist * dist)
        {
        }

        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(DynamicObjectMapType &m);
        template<class SKIP> void Visit(GridRefManager<SKIP> &) { }

        /**
         * @brief 向敌对玩家发送消息
         * @param player 目标玩家
         *
         * 过滤条件：
         * - 不发送给自己
         * - 必须能看见源对象
         * - 必须是敌对关系
         */
        void SendPacket(Player* player)
        {
            // never send packet to self
            if (player == i_source || !player->HaveAtClient(i_source) || player->IsFriendlyTo(i_source))
                return;

            player->SendDirectMessage(i_message);
        }
    };

    /**
     * @struct ObjectUpdater
     * @brief 对象更新器 - 对网格中的非玩家对象执行更新
     *
     * 与 GridUpdater 类似，但专门用于更新非玩家对象。
     * 玩家和尸体有独立的更新流程，不由此更新器处理。
     */
    struct ObjectUpdater
    {
        uint32 i_timeDiff;                  ///< 时间差（毫秒）

        /**
         * @brief 构造对象更新器
         * @param diff 时间差（毫秒）
         */
        explicit ObjectUpdater(const uint32 diff) : i_timeDiff(diff) { }

        template<class T> void Visit(GridRefManager<T> &m);
        void Visit(PlayerMapType &) { }     ///< 空实现 - 玩家有独立更新流程
        void Visit(CorpseMapType &) { }     ///< 空实现 - 尸体有独立更新流程
    };

    // ============================================================================
    // 搜索器、列表搜索器和执行器
    // ============================================================================

    // ------------------------- WorldObject 搜索器和执行器 -------------------------

    /**
     * @class ContainerInserter
     * @brief 容器插入器基类 - 提供向任意容器插入元素的通用接口
     *
     * 使用类型擦除技术，允许 ListSearcher 类模板支持各种容器类型
     * （std::vector, std::list 等）而无需在基类中暴露容器类型。
     *
     * @tparam Type 要插入的元素类型
     */
    template<typename Type>
    class ContainerInserter
    {
        using InserterType = void(*)(void*, Type&&); ///< 插入函数指针类型

        void* ref;                         ///< 容器指针（类型擦除）
        InserterType inserter;             ///< 插入函数指针

    protected:
        /**
         * @brief 构造容器插入器
         * @tparam T 容器类型
         * @param ref_ 容器引用
         */
        template<typename T>
        ContainerInserter(T& ref_) : ref(&ref_)
        {
            // 使用 lambda 生成类型擦除的插入函数
            inserter = [](void* containerRaw, Type&& object)
            {
                T* container = reinterpret_cast<T*>(containerRaw);
                container->insert(container->end(), std::move(object));
            };
        }

        /**
         * @brief 向容器插入元素
         * @param object 要插入的对象
         */
        void Insert(Type object)
        {
            inserter(ref, std::move(object));
        }
    };

    /**
     * @struct WorldObjectSearcher
     * @brief 世界对象搜索器 - 查找第一个满足条件的世界对象
     *
     * 遍历网格中的对象，找到第一个通过检查条件的对象后立即停止。
     * 用于快速查找单个对象。
     *
     * @tparam Check 检查条件类型（可调用对象）
     */
    template<class Check>
    struct WorldObjectSearcher
    {
        uint32 i_mapTypeMask;               ///< 对象类型掩码（过滤要搜索的对象类型）
        uint32 i_phaseMask;                 ///< 相位掩码（用于相位检测）
        WorldObject* &i_object;             ///< 输出参数：找到的对象
        Check &i_check;                     ///< 检查条件

        /**
         * @brief 构造世界对象搜索器
         * @param searcher 搜索源对象（用于获取相位掩码）
         * @param result 输出参数：找到的对象引用
         * @param check 检查条件
         * @param mapTypeMask 对象类型掩码，默认搜索所有类型
         */
        WorldObjectSearcher(WorldObject const* searcher, WorldObject* & result, Check& check, uint32 mapTypeMask = GRID_MAP_TYPE_MASK_ALL)
            : i_mapTypeMask(mapTypeMask), i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(GameObjectMapType &m);
        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(CorpseMapType &m);
        void Visit(DynamicObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct WorldObjectLastSearcher
     * @brief 世界对象最后搜索器 - 查找最后一个满足条件的世界对象
     *
     * 与 WorldObjectSearcher 类似，但遍历所有对象，返回最后一个满足条件的。
     * 用于需要找"最近"对象的场景（配合距离递减检查条件）。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct WorldObjectLastSearcher
    {
        uint32 i_mapTypeMask;               ///< 对象类型掩码
        uint32 i_phaseMask;                 ///< 相位掩码
        WorldObject* &i_object;             ///< 输出参数：找到的对象
        Check &i_check;                     ///< 检查条件

        /**
         * @brief 构造世界对象最后搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         * @param mapTypeMask 对象类型掩码
         */
        WorldObjectLastSearcher(WorldObject const* searcher, WorldObject* & result, Check& check, uint32 mapTypeMask = GRID_MAP_TYPE_MASK_ALL)
            :  i_mapTypeMask(mapTypeMask), i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(GameObjectMapType &m);
        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(CorpseMapType &m);
        void Visit(DynamicObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct WorldObjectListSearcher
     * @brief 世界对象列表搜索器 - 收集所有满足条件的世界对象
     *
     * 遍历网格中的所有对象，将满足条件的对象收集到容器中。
     * 用于批量查询场景。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct WorldObjectListSearcher : ContainerInserter<WorldObject*>
    {
        uint32 i_mapTypeMask;               ///< 对象类型掩码
        uint32 i_phaseMask;                 ///< 相位掩码
        Check& i_check;                     ///< 检查条件

        /**
         * @brief 构造世界对象列表搜索器
         * @tparam Container 容器类型
         * @param searcher 搜索源对象
         * @param container 存储结果的容器
         * @param check 检查条件
         * @param mapTypeMask 对象类型掩码
         */
        template<typename Container>
        WorldObjectListSearcher(WorldObject const* searcher, Container& container, Check & check, uint32 mapTypeMask = GRID_MAP_TYPE_MASK_ALL)
            : ContainerInserter<WorldObject*>(container),
              i_mapTypeMask(mapTypeMask), i_phaseMask(searcher->GetPhaseMask()), i_check(check) { }

        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);
        void Visit(CorpseMapType &m);
        void Visit(GameObjectMapType &m);
        void Visit(DynamicObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct WorldObjectWorker
     * @brief 世界对象执行器 - 对网格中的每个对象执行操作
     *
     * 不收集对象，而是对每个满足条件的对象执行指定操作。
     * 用于批量处理场景（如广播消息、批量更新等）。
     *
     * @tparam Do 操作类型（可调用对象）
     */
    template<class Do>
    struct WorldObjectWorker
    {
        uint32 i_mapTypeMask;               ///< 对象类型掩码
        uint32 i_phaseMask;                 ///< 相位掩码
        Do const& i_do;                     ///< 要执行的操作

        /**
         * @brief 构造世界对象执行器
         * @param searcher 搜索源对象
         * @param _do 要执行的操作
         * @param mapTypeMask 对象类型掩码
         */
        WorldObjectWorker(WorldObject const* searcher, Do const& _do, uint32 mapTypeMask = GRID_MAP_TYPE_MASK_ALL)
            : i_mapTypeMask(mapTypeMask), i_phaseMask(searcher->GetPhaseMask()), i_do(_do) { }

        /**
         * @brief 访问游戏对象并执行操作
         * @param m 游戏对象管理器
         */
        void Visit(GameObjectMapType &m)
        {
            if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_GAMEOBJECT))
                return;
            for (GameObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        /**
         * @brief 访问玩家并执行操作
         * @param m 玩家管理器
         */
        void Visit(PlayerMapType &m)
        {
            if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_PLAYER))
                return;
            for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        /**
         * @brief 访问生物并执行操作
         * @param m 生物管理器
         */
        void Visit(CreatureMapType &m)
        {
            if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CREATURE))
                return;
            for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        /**
         * @brief 访问尸体并执行操作
         * @param m 尸体管理器
         */
        void Visit(CorpseMapType &m)
        {
            if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_CORPSE))
                return;
            for (CorpseMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        /**
         * @brief 访问动态对象并执行操作
         * @param m 动态对象管理器
         */
        void Visit(DynamicObjectMapType &m)
        {
            if (!(i_mapTypeMask & GRID_MAP_TYPE_MASK_DYNAMICOBJECT))
                return;
            for (DynamicObjectMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    // ------------------------- GameObject 搜索器 -------------------------

    /**
     * @struct GameObjectSearcher
     * @brief 游戏对象搜索器 - 查找第一个满足条件的游戏对象
     *
     * 专门用于搜索 GameObject 类型对象，忽略其他类型。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct GameObjectSearcher
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        GameObject* &i_object;              ///< 输出参数：找到的游戏对象
        Check &i_check;                     ///< 检查条件

        /**
         * @brief 构造游戏对象搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         */
        GameObjectSearcher(WorldObject const* searcher, GameObject* & result, Check& check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(GameObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct GameObjectLastSearcher
     * @brief 游戏对象最后搜索器 - 查找最后一个满足条件的游戏对象
     *
     * 遍历所有游戏对象，返回最后一个满足检查条件的对象。
     * 检查条件可以在每次调用时改变要求（如逐步缩小距离）。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct GameObjectLastSearcher
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        GameObject* &i_object;              ///< 输出参数：找到的游戏对象
        Check& i_check;                     ///< 检查条件

        /**
         * @brief 构造游戏对象最后搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         */
        GameObjectLastSearcher(WorldObject const* searcher, GameObject* & result, Check& check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(GameObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct GameObjectListSearcher
     * @brief 游戏对象列表搜索器 - 收集所有满足条件的游戏对象
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct GameObjectListSearcher : ContainerInserter<GameObject*>
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Check& i_check;                     ///< 检查条件

        /**
         * @brief 构造游戏对象列表搜索器
         * @tparam Container 容器类型
         * @param searcher 搜索源对象
         * @param container 存储结果的容器
         * @param check 检查条件
         */
        template<typename Container>
        GameObjectListSearcher(WorldObject const* searcher, Container& container, Check & check)
            : ContainerInserter<GameObject*>(container),
              i_phaseMask(searcher->GetPhaseMask()), i_check(check) { }

        void Visit(GameObjectMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct GameObjectWorker
     * @brief 游戏对象执行器 - 对每个游戏对象执行操作
     *
     * @tparam Functor 操作类型（可调用对象）
     */
    template<class Functor>
    struct GameObjectWorker
    {
        /**
         * @brief 构造游戏对象执行器
         * @param searcher 搜索源对象
         * @param func 要执行的函数对象
         */
        GameObjectWorker(WorldObject const* searcher, Functor& func)
            : _func(func), _phaseMask(searcher->GetPhaseMask()) { }

        /**
         * @brief 访问游戏对象并执行操作
         * @param m 游戏对象管理器
         */
        void Visit(GameObjectMapType& m)
        {
            for (GameObjectMapType::iterator itr = m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(_phaseMask))
                    _func(itr->GetSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }

    private:
        Functor& _func;                     ///< 要执行的函数对象
        uint32 _phaseMask;                  ///< 相位掩码
    };

    // ------------------------- Unit 搜索器 -------------------------

    /**
     * @struct UnitSearcher
     * @brief 单位搜索器 - 查找第一个满足条件的单位（生物或玩家）
     *
     * 搜索 Creature 和 Player 类型对象。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct UnitSearcher
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Unit* &i_object;                    ///< 输出参数：找到的单位
        Check & i_check;                    ///< 检查条件

        /**
         * @brief 构造单位搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         */
        UnitSearcher(WorldObject const* searcher, Unit* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(CreatureMapType &m);
        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct UnitLastSearcher
     * @brief 单位最后搜索器 - 查找最后一个满足条件的单位
     *
     * 检查条件可以在每次调用时改变要求。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct UnitLastSearcher
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Unit* &i_object;                    ///< 输出参数：找到的单位
        Check & i_check;                    ///< 检查条件

        /**
         * @brief 构造单位最后搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         */
        UnitLastSearcher(WorldObject const* searcher, Unit* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(CreatureMapType &m);
        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct UnitListSearcher
     * @brief 单位列表搜索器 - 收集所有满足条件的单位
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct UnitListSearcher : ContainerInserter<Unit*>
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Check& i_check;                     ///< 检查条件

        /**
         * @brief 构造单位列表搜索器
         * @tparam Container 容器类型
         * @param searcher 搜索源对象
         * @param container 存储结果的容器
         * @param check 检查条件
         */
        template<typename Container>
        UnitListSearcher(WorldObject const* searcher, Container& container, Check& check)
            : ContainerInserter<Unit*>(container),
              i_phaseMask(searcher->GetPhaseMask()), i_check(check) { }

        void Visit(PlayerMapType &m);
        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    // ------------------------- Creature 搜索器 -------------------------

    /**
     * @struct CreatureSearcher
     * @brief 生物搜索器 - 查找第一个满足条件的生物
     *
     * 仅搜索 Creature 类型对象，不包括玩家。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct CreatureSearcher
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Creature* &i_object;                ///< 输出参数：找到的生物
        Check & i_check;                    ///< 检查条件

        /**
         * @brief 构造生物搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         */
        CreatureSearcher(WorldObject const* searcher, Creature* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct CreatureLastSearcher
     * @brief 生物最后搜索器 - 查找最后一个满足条件的生物
     *
     * 检查条件可以在每次调用时改变要求。
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct CreatureLastSearcher
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Creature* &i_object;                ///< 输出参数：找到的生物
        Check & i_check;                    ///< 检查条件

        /**
         * @brief 构造生物最后搜索器
         * @param searcher 搜索源对象
         * @param result 输出参数
         * @param check 检查条件
         */
        CreatureLastSearcher(WorldObject const* searcher, Creature* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    /**
     * @struct CreatureListSearcher
     * @brief 生物列表搜索器 - 收集所有满足条件的生物
     *
     * @tparam Check 检查条件类型
     */
    template<class Check>
    struct CreatureListSearcher : ContainerInserter<Creature*>
    {
        uint32 i_phaseMask;                 ///< 相位掩码
        Check& i_check;                     ///< 检查条件

        template<typename Container>
        CreatureListSearcher(WorldObject const* searcher, Container& container, Check & check)
            : ContainerInserter<Creature*>(container),
              i_phaseMask(searcher->GetPhaseMask()), i_check(check) { }

        void Visit(CreatureMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    template<class Do>
    struct CreatureWorker
    {
        uint32 i_phaseMask;
        Do& i_do;

        CreatureWorker(WorldObject const* searcher, Do& _do)
            : i_phaseMask(searcher->GetPhaseMask()), i_do(_do) { }

        void Visit(CreatureMapType &m)
        {
            for (CreatureMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    // Player searchers

    template<class Check>
    struct PlayerSearcher
    {
        uint32 i_phaseMask;
        Player* &i_object;
        Check & i_check;

        PlayerSearcher(WorldObject const* searcher, Player* & result, Check & check)
            : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check) { }

        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    template<class Check>
    struct PlayerListSearcher : ContainerInserter<Player*>
    {
        uint32 i_phaseMask;
        Check& i_check;

        template<typename Container>
        PlayerListSearcher(WorldObject const* searcher, Container& container, Check & check)
            : ContainerInserter<Player*>(container),
              i_phaseMask(searcher->GetPhaseMask()), i_check(check) { }

        template<typename Container>
        PlayerListSearcher(uint32 phaseMask, Container& container, Check & check)
            : ContainerInserter<Player*>(container),
              i_phaseMask(phaseMask), i_check(check) { }

        void Visit(PlayerMapType &m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    template<class Check>
    struct PlayerLastSearcher
    {
        uint32 i_phaseMask;
        Player* &i_object;
        Check& i_check;

        PlayerLastSearcher(WorldObject const* searcher, Player*& result, Check& check) : i_phaseMask(searcher->GetPhaseMask()), i_object(result), i_check(check)
        {
        }

        void Visit(PlayerMapType& m);

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    template<class Do>
    struct PlayerWorker
    {
        uint32 i_phaseMask;
        Do& i_do;

        PlayerWorker(WorldObject const* searcher, Do& _do)
            : i_phaseMask(searcher->GetPhaseMask()), i_do(_do) { }

        void Visit(PlayerMapType &m)
        {
            for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_phaseMask))
                    i_do(itr->GetSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    template<class Do>
    struct PlayerDistWorker
    {
        WorldObject const* i_searcher;
        float i_dist;
        Do& i_do;

        PlayerDistWorker(WorldObject const* searcher, float _dist, Do& _do)
            : i_searcher(searcher), i_dist(_dist), i_do(_do) { }

        void Visit(PlayerMapType &m)
        {
            for (PlayerMapType::iterator itr=m.begin(); itr != m.end(); ++itr)
                if (itr->GetSource()->InSamePhase(i_searcher) && itr->GetSource()->IsWithinDist(i_searcher, i_dist))
                    i_do(itr->GetSource());
        }

        template<class NOT_INTERESTED> void Visit(GridRefManager<NOT_INTERESTED> &) { }
    };

    // CHECKS && DO classes

    // WorldObject check classes

    class TC_GAME_API AnyDeadUnitObjectInRangeCheck
    {
        public:
            AnyDeadUnitObjectInRangeCheck(WorldObject* searchObj, float range) : i_searchObj(searchObj), i_range(range) { }
            bool operator()(Player* u);
            bool operator()(Corpse* u);
            bool operator()(Creature* u);
            template<class NOT_INTERESTED> bool operator()(NOT_INTERESTED*) { return false; }
        protected:
            WorldObject const* const i_searchObj;
            float i_range;
    };

    class TC_GAME_API AnyDeadUnitSpellTargetInRangeCheck : public AnyDeadUnitObjectInRangeCheck, public WorldObjectSpellTargetCheck
    {
        public:
            AnyDeadUnitSpellTargetInRangeCheck(WorldObject* searchObj, float range, SpellInfo const* spellInfo, SpellTargetCheckTypes check)
                : AnyDeadUnitObjectInRangeCheck(searchObj, range), WorldObjectSpellTargetCheck(searchObj, searchObj, spellInfo, check, nullptr)
            { }
            bool operator()(Player* u);
            bool operator()(Corpse* u);
            bool operator()(Creature* u);
            template<class NOT_INTERESTED> bool operator()(NOT_INTERESTED*) { return false; }
    };

    // WorldObject do classes

    class RespawnDo
    {
        public:
            RespawnDo() { }
            void operator()(Creature* u) const { u->Respawn(); }
            void operator()(GameObject* u) const { u->Respawn(); }
            void operator()(WorldObject*) const { }
            void operator()(Corpse*) const { }
    };

    // GameObject checks

    class GameObjectFocusCheck
    {
        public:
            GameObjectFocusCheck(WorldObject const* caster, uint32 focusId) : _caster(caster), _focusId(focusId) { }

            bool operator()(GameObject* go) const
            {
                if (go->GetGOInfo()->type != GAMEOBJECT_TYPE_SPELL_FOCUS)
                    return false;

                if (go->GetGOInfo()->spellFocus.focusId != _focusId)
                    return false;

                if (!go->isSpawned())
                    return false;

                float const dist = go->GetGOInfo()->spellFocus.dist;
                return go->IsWithinDistInMap(_caster, dist);
            }

        private:
            WorldObject const* _caster;
            uint32 _focusId;
    };

    // Find the nearest Fishing hole and return true only if source object is in range of hole
    class NearestGameObjectFishingHole
    {
        public:
            NearestGameObjectFishingHole(WorldObject const& obj, float range) : i_obj(obj), i_range(range) { }

            bool operator()(GameObject* go)
            {
                if (go->GetGOInfo()->type == GAMEOBJECT_TYPE_FISHINGHOLE && go->isSpawned() && i_obj.IsWithinDistInMap(go, i_range) && i_obj.IsWithinDistInMap(go, (float)go->GetGOInfo()->fishinghole.radius))
                {
                    i_range = i_obj.GetDistance(go);
                    return true;
                }
                return false;
            }

        private:
            WorldObject const& i_obj;
            float i_range;

            // prevent clone
            NearestGameObjectFishingHole(NearestGameObjectFishingHole const&) = delete;
    };

    class NearestGameObjectCheck
    {
        public:
            NearestGameObjectCheck(WorldObject const& obj) : i_obj(obj), i_range(999.f) { }

            bool operator()(GameObject* go)
            {
                if (i_obj.IsWithinDistInMap(go, i_range))
                {
                    i_range = i_obj.GetDistance(go);        // use found GO range as new range limit for next check
                    return true;
                }
                return false;
            }

        private:
            WorldObject const& i_obj;
            float i_range;

            // prevent clone this object
            NearestGameObjectCheck(NearestGameObjectCheck const&) = delete;
    };

    // Success at unit in range, range update for next check (this can be use with GameobjectLastSearcher to find nearest GO)
    class NearestGameObjectEntryInObjectRangeCheck
    {
        public:
            NearestGameObjectEntryInObjectRangeCheck(WorldObject const& obj, uint32 entry, float range, bool spawnedOnly = true) : i_obj(obj), i_entry(entry), i_range(range), i_spawnedOnly(spawnedOnly) { }

            bool operator()(GameObject* go)
            {
                if ((!i_spawnedOnly || go->isSpawned()) && go->GetEntry() == i_entry && go->GetGUID() != i_obj.GetGUID() && i_obj.IsWithinDistInMap(go, i_range))
                {
                    i_range = i_obj.GetDistance(go);        // use found GO range as new range limit for next check
                    return true;
                }
                return false;
            }

        private:
            WorldObject const& i_obj;
            uint32 i_entry;
            float  i_range;
            bool   i_spawnedOnly;

            // prevent clone this object
            NearestGameObjectEntryInObjectRangeCheck(NearestGameObjectEntryInObjectRangeCheck const&) = delete;
    };

    // Success at unit in range, range update for next check (this can be use with GameobjectLastSearcher to find nearest unspawned GO)
    class NearestUnspawnedGameObjectEntryInObjectRangeCheck
    {
    public:
        NearestUnspawnedGameObjectEntryInObjectRangeCheck(WorldObject const& obj, uint32 entry, float range) : i_obj(obj), i_entry(entry), i_range(range) { }

        bool operator()(GameObject* go)
        {
            if (!go->isSpawned() && go->GetEntry() == i_entry && go->GetGUID() != i_obj.GetGUID() && i_obj.IsWithinDistInMap(go, i_range))
            {
                i_range = i_obj.GetDistance(go);        // use found GO range as new range limit for next check
                return true;
            }
            return false;
        }

    private:
        WorldObject const& i_obj;
        uint32 i_entry;
        float  i_range;

        // prevent clone this object
        NearestUnspawnedGameObjectEntryInObjectRangeCheck(NearestUnspawnedGameObjectEntryInObjectRangeCheck const&) = delete;
    };

    // Success at unit in range, range update for next check (this can be use with GameobjectLastSearcher to find nearest GO with a certain type)
    class NearestGameObjectTypeInObjectRangeCheck
    {
        public:
            NearestGameObjectTypeInObjectRangeCheck(WorldObject const& obj, GameobjectTypes type, float range) : i_obj(obj), i_type(type), i_range(range) { }

            bool operator()(GameObject* go)
            {
                if (go->GetGoType() == i_type && i_obj.IsWithinDistInMap(go, i_range))
                {
                    i_range = i_obj.GetDistance(go);        // use found GO range as new range limit for next check
                    return true;
                }
                return false;
            }

        private:
            WorldObject const& i_obj;
            GameobjectTypes i_type;
            float i_range;

            // prevent clone this object
            NearestGameObjectTypeInObjectRangeCheck(NearestGameObjectTypeInObjectRangeCheck const&) = delete;
    };

    // Unit checks

    class MostHPMissingInRange
    {
        public:
            MostHPMissingInRange(Unit const* obj, float range, uint32 hp) : i_obj(obj), i_range(range), i_hp(hp) { }

            bool operator()(Unit* u)
            {
                if (u->IsAlive() && u->IsInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) && u->GetMaxHealth() - u->GetHealth() > i_hp)
                {
                    i_hp = u->GetMaxHealth() - u->GetHealth();
                    return true;
                }
                return false;
            }

        private:
            Unit const* i_obj;
            float i_range;
            uint32 i_hp;
    };

    class MostHPPercentMissingInRange
    {
    public:
        MostHPPercentMissingInRange(Unit const* obj, float range, uint32 minHpPct, uint32 maxHpPct) : i_obj(obj), i_range(range), i_minHpPct(minHpPct), i_maxHpPct(maxHpPct), i_hpPct(101.f) { }

        bool operator()(Unit* u)
        {
            if (u->IsAlive() && u->IsInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) && i_minHpPct <= u->GetHealthPct() && u->GetHealthPct() <= i_maxHpPct && u->GetHealthPct() < i_hpPct)
            {
                i_hpPct = u->GetHealthPct();
                return true;
            }
            return false;
        }

    private:
        Unit const* i_obj;
        float i_range;
        float i_minHpPct, i_maxHpPct, i_hpPct;
    };

    class FriendlyBelowHpPctEntryInRange
    {
        public:
            FriendlyBelowHpPctEntryInRange(Unit const* obj, uint32 entry, float range, uint8 pct, bool excludeSelf) : i_obj(obj), i_entry(entry), i_range(range), i_pct(pct), i_excludeSelf(excludeSelf) { }

            bool operator()(Unit* u)
            {
                if (i_excludeSelf && i_obj->GetGUID() == u->GetGUID())
                    return false;
                if (u->GetEntry() == i_entry && u->IsAlive() && u->IsInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) && u->HealthBelowPct(i_pct))
                    return true;
                return false;
            }

        private:
            Unit const* i_obj;
            uint32 i_entry;
            float i_range;
            uint8 i_pct;
            bool i_excludeSelf;
    };

    class MostHPMissingGroupInRange
    {
        public:
            MostHPMissingGroupInRange(Unit const* obj, float range, uint32 hp) : i_obj(obj), i_range(range), i_hp(hp) { }

            bool operator()(Unit* u)
            {
                if (i_obj == u)
                    return false;

                Player* player = nullptr;
                if (u->GetTypeId() == TYPEID_PLAYER)
                    player = u->ToPlayer();
                else if (u->IsPet() && u->GetOwner())
                    player = u->GetOwner()->ToPlayer();

                if (!player)
                    return false;

                Group* group = player->GetGroup();
                if (!group || !group->IsMember(i_obj->IsPet() ? i_obj->GetOwnerGUID() : i_obj->GetGUID()))
                    return false;

                if (u->IsAlive() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) && u->GetMaxHealth() - u->GetHealth() > i_hp)
                {
                    i_hp = u->GetMaxHealth() - u->GetHealth();
                    return true;
                }

                return false;
            }

        private:
            Unit const* i_obj;
            float i_range;
            uint32 i_hp;
    };

    class FriendlyCCedInRange
    {
        public:
            FriendlyCCedInRange(Unit const* obj, float range) : i_obj(obj), i_range(range) { }

            bool operator()(Unit* u) const
            {
                if (u->IsAlive() && u->IsInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) &&
                    (u->IsFeared() || u->IsCharmed() || u->IsRooted() || u->HasUnitState(UNIT_STATE_STUNNED) || u->HasUnitState(UNIT_STATE_CONFUSED)))
                {
                    return true;
                }
                return false;
            }

        private:
            Unit const* i_obj;
            float i_range;
    };

    class FriendlyMissingBuffInRange
    {
        public:
            FriendlyMissingBuffInRange(Unit const* obj, float range, uint32 spellid) : i_obj(obj), i_range(range), i_spell(spellid) { }

            bool operator()(Unit* u) const
            {
                if (u->IsAlive() && u->IsInCombat() && !i_obj->IsHostileTo(u) && i_obj->IsWithinDistInMap(u, i_range) && !u->HasAura(i_spell))
                    return true;

                return false;
            }

        private:
            Unit const* i_obj;
            float i_range;
            uint32 i_spell;
    };

    class AnyUnfriendlyUnitInObjectRangeCheck
    {
        public:
            AnyUnfriendlyUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range) : i_obj(obj), i_funit(funit), i_range(range) { }

            bool operator()(Unit* u) const
            {
                if (u->IsAlive() && i_obj->IsWithinDistInMap(u, i_range) && !i_funit->IsFriendlyTo(u))
                    return true;

                return false;
            }

        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;
    };

    class NearestAttackableNoTotemUnitInObjectRangeCheck
    {
        public:
            NearestAttackableNoTotemUnitInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) { }

            bool operator()(Unit* u)
            {
                if (!u->IsAlive())
                    return false;

                if (u->GetCreatureType() == CREATURE_TYPE_NON_COMBAT_PET)
                    return false;

                if (u->GetTypeId() == TYPEID_UNIT && u->ToCreature()->IsTotem())
                    return false;

                if (!u->isTargetableForAttack(false))
                    return false;

                if (!i_obj->IsWithinDistInMap(u, i_range) || !i_obj->IsValidAttackTarget(u))
                    return false;

                i_range = i_obj->GetDistance(*u);
                return true;
            }

        private:
            WorldObject const* i_obj;
            float i_range;
    };

    class AnyFriendlyUnitInObjectRangeCheck
    {
        public:
            AnyFriendlyUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range, bool playerOnly = false, bool incOwnRadius = true, bool incTargetRadius = true)
                : i_obj(obj), i_funit(funit), i_range(range), i_playerOnly(playerOnly), i_incOwnRadius(incOwnRadius), i_incTargetRadius(incTargetRadius) { }

            bool operator()(Unit* u) const
            {
                if (!u->IsAlive())
                    return false;

                float searchRadius = i_range;
                if (i_incOwnRadius)
                    searchRadius += i_obj->GetCombatReach();
                if (i_incTargetRadius)
                    searchRadius += u->GetCombatReach();

                if (!u->IsInMap(i_obj) || !u->InSamePhase(i_obj) || !u->IsWithinDoubleVerticalCylinder(i_obj, searchRadius, searchRadius))
                    return false;

                if (!i_funit->IsFriendlyTo(u))
                    return false;

                return !i_playerOnly || u->GetTypeId() == TYPEID_PLAYER;
            }

        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;
            bool i_playerOnly;
            bool i_incOwnRadius;
            bool i_incTargetRadius;
    };

    class AnyGroupedUnitInObjectRangeCheck
    {
        public:
            AnyGroupedUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range, bool raid, bool playerOnly = false, bool incOwnRadius = true, bool incTargetRadius = true)
                : _source(obj), _refUnit(funit), _range(range), _raid(raid), _playerOnly(playerOnly), i_incOwnRadius(incOwnRadius), i_incTargetRadius(incTargetRadius) { }

            bool operator()(Unit* u) const
            {
                if (_playerOnly && u->GetTypeId() != TYPEID_PLAYER)
                    return false;

                if (_raid)
                {
                    if (!_refUnit->IsInRaidWith(u))
                        return false;
                }
                else if (!_refUnit->IsInPartyWith(u))
                    return false;

                if (_refUnit->IsHostileTo(u))
                    return false;

                if (!u->IsAlive())
                    return false;

                float searchRadius = _range;
                if (i_incOwnRadius)
                    searchRadius += _source->GetCombatReach();
                if (i_incTargetRadius)
                    searchRadius += u->GetCombatReach();

                return u->IsInMap(_source) && u->InSamePhase(_source) && u->IsWithinDoubleVerticalCylinder(_source, searchRadius, searchRadius);
            }

        private:
            WorldObject const* _source;
            Unit const* _refUnit;
            float _range;
            bool _raid;
            bool _playerOnly;
            bool i_incOwnRadius;
            bool i_incTargetRadius;
    };

    class AnyUnitInObjectRangeCheck
    {
        public:
            AnyUnitInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) { }

            bool operator()(Unit* u) const
            {
                if (u->IsAlive() && i_obj->IsWithinDistInMap(u, i_range))
                    return true;

                return false;
            }

        private:
            WorldObject const* i_obj;
            float i_range;
    };

    // Success at unit in range, range update for next check (this can be use with UnitLastSearcher to find nearest unit)
    class NearestAttackableUnitInObjectRangeCheck
    {
        public:
            NearestAttackableUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range) : i_obj(obj), i_funit(funit), i_range(range) { }

            bool operator()(Unit* u)
            {
                if (u->isTargetableForAttack() && i_obj->IsWithinDistInMap(u, i_range) &&
                    (i_funit->IsInCombatWith(u) || i_funit->IsHostileTo(u)) && i_obj->CanSeeOrDetect(u))
                {
                    i_range = i_obj->GetDistance(u);        // use found unit range as new range limit for next check
                    return true;
                }

                return false;
            }

        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            float i_range;

            // prevent clone this object
            NearestAttackableUnitInObjectRangeCheck(NearestAttackableUnitInObjectRangeCheck const&) = delete;
    };

    class AnyAoETargetUnitInObjectRangeCheck
    {
        public:
            AnyAoETargetUnitInObjectRangeCheck(WorldObject const* obj, Unit const* funit, float range, SpellInfo const* spellInfo = nullptr, bool incOwnRadius = true, bool incTargetRadius = true)
                : i_obj(obj), i_funit(funit), _spellInfo(spellInfo), i_range(range), i_incOwnRadius(incOwnRadius), i_incTargetRadius(incTargetRadius)
            {
            }

            bool operator()(Unit* u) const
            {
                // Check contains checks for: live, uninteractible, non-attackable flags, flight check and GM check, ignore totems
                if (u->GetTypeId() == TYPEID_UNIT && u->IsTotem())
                    return false;

                if (_spellInfo && _spellInfo->HasAttribute(SPELL_ATTR3_ONLY_TARGET_PLAYERS) && u->GetTypeId() != TYPEID_PLAYER)
                    return false;

                if (!i_funit->IsValidAttackTarget(u, _spellInfo))
                    return false;

                float searchRadius = i_range;
                if (i_incOwnRadius)
                    searchRadius += i_obj->GetCombatReach();
                if (i_incTargetRadius)
                    searchRadius += u->GetCombatReach();

                return u->IsInMap(i_obj) && u->InSamePhase(i_obj) && u->IsWithinDoubleVerticalCylinder(i_obj, searchRadius, searchRadius);
            }

        private:
            WorldObject const* i_obj;
            Unit const* i_funit;
            SpellInfo const* _spellInfo;
            float i_range;
            bool i_incOwnRadius;
            bool i_incTargetRadius;
    };

    // do attack at call of help to friendly crearture
    class CallOfHelpCreatureInRangeDo
    {
        public:
            CallOfHelpCreatureInRangeDo(Unit* funit, Unit* enemy, float range)
                : i_funit(funit), i_enemy(enemy), i_range(range) { }

            void operator()(Creature* u) const
            {
                if (u == i_funit)
                    return;

                if (!u->CanAssistTo(i_funit, i_enemy, false))
                    return;

                // too far
                // Don't use combat reach distance, range must be an absolute value, otherwise the chain aggro range will be too big
                if (!u->IsWithinDistInMap(i_funit, i_range, true, false, false))
                    return;

                // only if see assisted creature's enemy
                if (!u->IsWithinLOSInMap(i_enemy))
                    return;

                u->EngageWithTarget(i_enemy);
            }
        private:
            Unit* const i_funit;
            Unit* const i_enemy;
            float i_range;
    };

    // Creature checks

    class NearestHostileUnitCheck
    {
        public:
            explicit NearestHostileUnitCheck(Creature const* creature, float dist = 0.f, bool playerOnly = false) : me(creature), i_playerOnly(playerOnly)
            {
                m_range = (dist == 0.f ? 9999.f : dist);
            }

            bool operator()(Unit* u)
            {
                if (!me->IsWithinDistInMap(u, m_range))
                    return false;

                if (!me->IsValidAttackTarget(u))
                    return false;

                if (i_playerOnly && u->GetTypeId() != TYPEID_PLAYER)
                    return false;

                m_range = me->GetDistance(u);   // use found unit range as new range limit for next check
                return true;
            }

        private:
            Creature const* me;
            float m_range;
            bool i_playerOnly;
            NearestHostileUnitCheck(NearestHostileUnitCheck const&) = delete;
    };

    class NearestHostileUnitInAttackDistanceCheck
    {
        public:
            explicit NearestHostileUnitInAttackDistanceCheck(Creature const* creature, float dist = 0.f) : me(creature)
            {
                m_range = (dist == 0.f ? 9999.f : dist);
                m_force = (dist == 0.f ? false : true);
            }

            bool operator()(Unit* u)
            {
                if (!me->IsWithinDistInMap(u, m_range))
                    return false;

                if (!me->CanSeeOrDetect(u))
                    return false;

                if (m_force)
                {
                    if (!me->IsValidAttackTarget(u))
                        return false;
                }
                else if (!me->CanStartAttack(u, false))
                    return false;

                m_range = me->GetDistance(u);   // use found unit range as new range limit for next check
                return true;
            }

        private:
            Creature const* me;
            float m_range;
            bool m_force;
            NearestHostileUnitInAttackDistanceCheck(NearestHostileUnitInAttackDistanceCheck const&) = delete;
    };

    class NearestHostileUnitInAggroRangeCheck
    {
        public:
            explicit NearestHostileUnitInAggroRangeCheck(Creature const* creature, bool useLOS = false, bool ignoreCivilians = false) : _me(creature), _useLOS(useLOS), _ignoreCivilians(ignoreCivilians) { }

            bool operator()(Unit* u) const
            {
                if (!u->IsHostileTo(_me))
                    return false;

                if (!u->IsWithinDistInMap(_me, _me->GetAggroRange(u)))
                    return false;

                if (!_me->IsValidAttackTarget(u))
                    return false;

                if (_useLOS && !u->IsWithinLOSInMap(_me))
                    return false;

                // pets in aggressive do not attack civilians
                if (_ignoreCivilians)
                    if (Creature* c = u->ToCreature())
                        if (c->IsCivilian())
                            return false;

                return true;
            }

        private:
            Creature const* _me;
            bool _useLOS;
            bool _ignoreCivilians;
            NearestHostileUnitInAggroRangeCheck(NearestHostileUnitInAggroRangeCheck const&) = delete;
    };

    class AnyAssistCreatureInRangeCheck
    {
        public:
            AnyAssistCreatureInRangeCheck(Unit* funit, Unit* enemy, float range)
                : i_funit(funit), i_enemy(enemy), i_range(range) { }

            bool operator()(Creature* u) const
            {
                if (u == i_funit)
                    return false;

                if (!u->CanAssistTo(i_funit, i_enemy))
                    return false;

                // too far
                // Don't use combat reach distance, range must be an absolute value, otherwise the chain aggro range will be too big
                if (!i_funit->IsWithinDistInMap(u, i_range, true, false, false))
                    return false;

                // only if see assisted creature
                if (!i_funit->IsWithinLOSInMap(u))
                    return false;

                return true;
            }

        private:
            Unit* const i_funit;
            Unit* const i_enemy;
            float i_range;
    };

    class NearestAssistCreatureInCreatureRangeCheck
    {
        public:
            NearestAssistCreatureInCreatureRangeCheck(Creature* obj, Unit* enemy, float range)
                : i_obj(obj), i_enemy(enemy), i_range(range) { }

            bool operator()(Creature* u)
            {
                if (u == i_obj)
                    return false;
                if (!u->CanAssistTo(i_obj, i_enemy))
                    return false;

                // Don't use combat reach distance, range must be an absolute value, otherwise the chain aggro range will be too big
                if (!i_obj->IsWithinDistInMap(u, i_range, true, false, false))
                    return false;

                if (!i_obj->IsWithinLOSInMap(u))
                    return false;

                i_range = i_obj->GetDistance(u);            // use found unit range as new range limit for next check
                return true;
            }

        private:
            Creature* const i_obj;
            Unit* const i_enemy;
            float i_range;

            // prevent clone this object
            NearestAssistCreatureInCreatureRangeCheck(NearestAssistCreatureInCreatureRangeCheck const&) = delete;
    };

    // Success at unit in range, range update for next check (this can be use with CreatureLastSearcher to find nearest creature)
    class NearestCreatureEntryWithLiveStateInObjectRangeCheck
    {
        public:
            NearestCreatureEntryWithLiveStateInObjectRangeCheck(WorldObject const& obj, uint32 entry, bool alive, float range)
                : i_obj(obj), i_entry(entry), i_alive(alive), i_range(range) { }

            bool operator()(Creature* u)
            {
                if (u->getDeathState() != DEAD && u->GetEntry() == i_entry && u->IsAlive() == i_alive && u->GetGUID() != i_obj.GetGUID() && i_obj.IsWithinDistInMap(u, i_range))
                {
                    i_range = i_obj.GetDistance(u);         // use found unit range as new range limit for next check
                    return true;
                }
                return false;
            }

        private:
            WorldObject const& i_obj;
            uint32 i_entry;
            bool   i_alive;
            float  i_range;

            // prevent clone this object
            NearestCreatureEntryWithLiveStateInObjectRangeCheck(NearestCreatureEntryWithLiveStateInObjectRangeCheck const&) = delete;
    };

    class AnyPlayerInObjectRangeCheck
    {
        public:
            AnyPlayerInObjectRangeCheck(WorldObject const* obj, float range, bool reqAlive = true) : _obj(obj), _range(range), _reqAlive(reqAlive) { }

            bool operator()(Player* u) const
            {
                if (_reqAlive && !u->IsAlive())
                    return false;

                if (!_obj->IsWithinDistInMap(u, _range))
                    return false;

                return true;
            }

        private:
            WorldObject const* _obj;
            float _range;
            bool _reqAlive;
    };

    class AnyPlayerInPositionRangeCheck
    {
    public:
        AnyPlayerInPositionRangeCheck(Position const* pos, float range, bool reqAlive = true) : _pos(pos), _range(range), _reqAlive(reqAlive) { }
        bool operator()(Player* u)
        {
            if (_reqAlive && !u->IsAlive())
                return false;

            if (!u->IsWithinDist3d(_pos, _range))
                return false;

            return true;
        }

    private:
        Position const* _pos;
        float _range;
        bool _reqAlive;
    };

    class NearestPlayerInObjectRangeCheck
    {
        public:
            NearestPlayerInObjectRangeCheck(WorldObject const* obj, float range) : i_obj(obj), i_range(range) { }

            bool operator()(Player* u)
            {
                if (u->IsAlive() && i_obj->IsWithinDistInMap(u, i_range))
                {
                    i_range = i_obj->GetDistance(u);
                    return true;
                }

                return false;
            }
        private:
            WorldObject const* i_obj;
            float i_range;

            NearestPlayerInObjectRangeCheck(NearestPlayerInObjectRangeCheck const&) = delete;
    };

    class AllFriendlyCreaturesInGrid
    {
        public:
            AllFriendlyCreaturesInGrid(Unit const* obj) : unit(obj) { }

            bool operator()(Unit* u) const
            {
                if (u->IsAlive() && u->IsVisible() && u->IsFriendlyTo(unit))
                    return true;

                return false;
            }

        private:
            Unit const* unit;
    };

    class AllGameObjectsWithEntryInRange
    {
        public:
            AllGameObjectsWithEntryInRange(WorldObject const* object, uint32 entry, float maxRange) : m_pObject(object), m_uiEntry(entry), m_fRange(maxRange) { }

            bool operator()(GameObject* go) const
            {
                if ((!m_uiEntry || go->GetEntry() == m_uiEntry) && m_pObject->IsWithinDist(go, m_fRange, false))
                    return true;

                return false;
            }

        private:
            WorldObject const* m_pObject;
            uint32 m_uiEntry;
            float m_fRange;
    };

    class AllCreaturesOfEntryInRange
    {
        public:
            AllCreaturesOfEntryInRange(WorldObject const* object, uint32 entry, float maxRange = 0.0f) : m_pObject(object), m_uiEntry(entry), m_fRange(maxRange) { }

            bool operator()(Unit* unit) const
            {
                if (m_uiEntry)
                {
                    if (unit->GetEntry() != m_uiEntry)
                        return false;
                }

                if (m_fRange)
                {
                    if (m_fRange > 0.0f && !m_pObject->IsWithinDist(unit, m_fRange, false))
                        return false;
                    if (m_fRange < 0.0f && m_pObject->IsWithinDist(unit, m_fRange, false))
                        return false;
                }

                return true;
            }

        private:
            WorldObject const* m_pObject;
            uint32 m_uiEntry;
            float m_fRange;
    };

    class PlayerAtMinimumRangeAway
    {
        public:
            PlayerAtMinimumRangeAway(Unit const* unit, float fMinRange) : unit(unit), fRange(fMinRange) { }

            bool operator()(Player* player) const
            {
                //No threat list check, must be done explicit if expected to be in combat with creature
                if (!player->IsGameMaster() && player->IsAlive() && !unit->IsWithinDist(player, fRange, false))
                    return true;

                return false;
            }

        private:
            Unit const* unit;
            float fRange;
    };

    class GameObjectInRangeCheck
    {
        public:
            GameObjectInRangeCheck(float _x, float _y, float _z, float _range, uint32 _entry = 0) :
              x(_x), y(_y), z(_z), range(_range), entry(_entry) { }

            bool operator()(GameObject* go) const
            {
                if (!entry || (go->GetGOInfo() && go->GetGOInfo()->entry == entry))
                    return go->IsInRange(x, y, z, range);
                else return false;
            }

        private:
            float x, y, z, range;
            uint32 entry;
    };

    class AllWorldObjectsInRange
    {
        public:
            AllWorldObjectsInRange(WorldObject const* object, float maxRange) : m_pObject(object), m_fRange(maxRange) { }

            bool operator()(WorldObject* go) const
            {
                return m_pObject->IsWithinDist(go, m_fRange, false) && m_pObject->InSamePhase(go);
            }

        private:
            WorldObject const* m_pObject;
            float m_fRange;
    };

    class ObjectTypeIdCheck
    {
        public:
            ObjectTypeIdCheck(TypeID typeId, bool equals) : _typeId(typeId), _equals(equals) { }

            bool operator()(WorldObject* object) const
            {
                return (object->GetTypeId() == _typeId) == _equals;
            }

        private:
            TypeID _typeId;
            bool _equals;
    };

    class ObjectGUIDCheck
    {
        public:
            ObjectGUIDCheck(ObjectGuid GUID) : _GUID(GUID) { }

            bool operator()(WorldObject* object) const
            {
                return object->GetGUID() == _GUID;
            }

        private:
            ObjectGuid _GUID;
    };

    class UnitAuraCheck
    {
        public:
            UnitAuraCheck(bool present, uint32 spellId, ObjectGuid casterGUID = ObjectGuid::Empty) : _present(present), _spellId(spellId), _casterGUID(casterGUID) { }

            bool operator()(Unit* unit) const
            {
                return unit->HasAura(_spellId, _casterGUID) == _present;
            }

            bool operator()(WorldObject* object) const
            {
                return object->ToUnit() && object->ToUnit()->HasAura(_spellId, _casterGUID) == _present;
            }

        private:
            bool _present;
            uint32 _spellId;
            ObjectGuid _casterGUID;
    };

    // Player checks and do

    // Prepare using Builder localized packets with caching and send to player
    template<class Builder>
    class LocalizedPacketDo
    {
        public:
            explicit LocalizedPacketDo(Builder& builder) : i_builder(builder) { }

            ~LocalizedPacketDo()
            {
                for (size_t i = 0; i < i_data_cache.size(); ++i)
                    delete i_data_cache[i];
            }

            void operator()(Player* p);

        private:
            Builder& i_builder;
            std::vector<WorldPacket*> i_data_cache;         // 0 = default, i => i-1 locale index
    };

    // Prepare using Builder localized packets with caching and send to player
    template<class Builder>
    class LocalizedPacketListDo
    {
        public:
            typedef std::vector<WorldPacket*> WorldPacketList;
            explicit LocalizedPacketListDo(Builder& builder) : i_builder(builder) { }

            ~LocalizedPacketListDo()
            {
                for (size_t i = 0; i < i_data_cache.size(); ++i)
                    for (size_t j = 0; j < i_data_cache[i].size(); ++j)
                        delete i_data_cache[i][j];
            }
            void operator()(Player* p);

        private:
            Builder& i_builder;
            std::vector<WorldPacketList> i_data_cache;
                                                            // 0 = default, i => i-1 locale index
    };
}
#endif
