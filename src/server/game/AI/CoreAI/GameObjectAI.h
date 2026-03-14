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
 * @file GameObjectAI.h
 *
 * @brief 游戏对象AI模块
 *
 * 本文件定义了游戏对象（GameObject）的人工智能系统。
 * 游戏对象AI负责处理各种可交互对象的逻辑，例如：
 * - 任务物品
 * - 矿石、草药等采集对象
 * - 宝箱
 * - 机关和开关
 * - 各类交互式建筑
 *
 * 与CreatureAI不同，GameObjectAI主要处理玩家交互事件，
 * 而不是战斗和移动行为。
 */

#ifndef TRINITY_GAMEOBJECTAI_H
#define TRINITY_GAMEOBJECTAI_H

#include "Define.h"
#include "ObjectGuid.h"
#include "Optional.h"
#include "QuestDef.h"

class Creature;
class GameObject;
class Unit;
class SpellInfo;
class WorldObject;

/**
 * @brief 游戏对象AI基类
 *
 * 所有游戏对象AI的基类，提供游戏对象行为的虚拟接口。
 * 游戏对象AI负责处理：
 * - 玩家交互事件（对话、任务、采集等）
 * - 游戏对象状态变化
 * - 法术命中事件
 * - 召唤生物管理
 *
 * 大多数函数提供空实现，子类应根据需要覆盖相应函数。
 */
class TC_GAME_API GameObjectAI
{
    protected:
        GameObject* const me; ///< 关联的游戏对象指针，在构造函数中初始化，不可更改

    public:
        /**
         * @brief 构造函数
         * @param go 关联的游戏对象指针
         */
        explicit GameObjectAI(GameObject* go) : me(go) { }

        /**
         * @brief 虚析构函数
         */
        virtual ~GameObjectAI() { }

        /**
         * @brief 更新AI逻辑
         * @param diff 距离上次更新的时间差（毫秒）
         *
         * 每帧调用，用于处理需要持续更新的游戏对象逻辑。
         * 默认为空实现，大多数游戏对象不需要每帧更新。
         */
        virtual void UpdateAI(uint32 /*diff*/) { }

        /**
         * @brief 初始化AI
         *
         * 在AI创建后调用，用于执行初始化逻辑。
         * 默认调用Reset()函数。
         */
        virtual void InitializeAI() { Reset(); }

        /**
         * @brief 重置AI状态
         *
         * 重置游戏对象的所有状态到初始状态。
         * 在游戏对象重置或重生时调用。
         */
        virtual void Reset() { }

        /**
         * @brief 执行动作
         * @param param 动作参数，用于传递动作类型或标识符
         *
         * 用于AI之间或脚本与AI之间传递动作指令。
         * 是一种通用的命令模式实现。
         */
        virtual void DoAction(int32 /*param = 0 */) { }

        /**
         * @brief 设置GUID
         * @param guid 要设置的GUID值
         * @param id 标识符，用于区分不同的GUID存储位置
         *
         * 用于在AI中存储特定对象的GUID引用。
         */
        virtual void SetGUID(ObjectGuid const& /*guid*/, int32 /*id = 0 */) { }

        /**
         * @brief 获取GUID
         * @param id 标识符，用于区分不同的GUID存储位置
         * @return 存储的GUID，如果没有则返回空GUID
         */
        virtual ObjectGuid GetGUID(int32 /*id = 0 */) const { return ObjectGuid::Empty; }

        /**
         * @brief 检查此AI是否适用于指定游戏对象
         * @param go 要检查的游戏对象
         * @return 权限值，默认返回PERMIT_BASE_NO表示不自动选择
         */
        static int32 Permissible(GameObject const* go);

        /**
         * @brief 获取对话框状态
         * @param player 请求对话框状态的玩家
         * @return 对话框状态（任务可用、任务完成等），空表示无特殊状态
         *
         * 当请求玩家与游戏对象之间的对话框状态时调用。
         * 用于显示任务标记（黄色感叹号、灰色问号等）。
         */
        virtual Optional<QuestGiverStatus> GetDialogStatus(Player* /*player*/) { return {}; }

        /**
         * @brief 玩家打开八卦菜单
         * @param player 打开八卦菜单的玩家
         * @return true表示已处理，false表示继续默认处理
         *
         * 当玩家打开与游戏对象的八卦对话框时调用。
         * 这是游戏对象交互的主要入口点。
         */
        virtual bool OnGossipHello(Player* /*player*/) { return false; }

        /**
         * @brief 玩家选择八卦菜单项
         * @param player 选择菜单项的玩家
         * @param menuId 菜单ID
         * @param gossipListId 八卦菜单项ID
         * @return true表示已处理，false表示继续默认处理
         *
         * 当玩家在游戏对象的八卦菜单中选择某个选项时调用。
         */
        virtual bool OnGossipSelect(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/) { return false; }

        /**
         * @brief 玩家选择带代码输入的八卦菜单项
         * @param player 选择菜单项的玩家
         * @param menuId 菜单ID
         * @param gossipListId 八卦菜单项ID
         * @param code 玩家输入的代码字符串
         * @return true表示已处理，false表示继续默认处理
         *
         * 当玩家在八卦菜单中选择需要输入文本的选项时调用。
         * 常用于密码验证、命名等场景。
         */
        virtual bool OnGossipSelectCode(Player* /*player*/, uint32 /*menuId*/, uint32 /*gossipListId*/, char const* /*code*/) { return false; }

        /**
         * @brief 玩家接受任务
         * @param player 接受任务的玩家
         * @param quest 被接受的任务对象
         *
         * 当玩家从游戏对象接受任务时调用。
         */
        virtual void OnQuestAccept(Player* /*player*/, Quest const* /*quest*/) { }

        /**
         * @brief 玩家完成任务领取奖励
         * @param player 完成任务的玩家
         * @param quest 被完成的任务对象
         * @param opt 选择的奖励物品索引，或0表示无选择
         *
         * 当玩家完成任务并领取奖励时调用。
         * opt参数用于多选奖励物品的任务。
         */
        virtual void OnQuestReward(Player* /*player*/, Quest const* /*quest*/, uint32 /*opt*/) { }

        /**
         * @brief 玩家报告使用（点击游戏对象）
         * @param player 点击游戏对象的玩家
         * @return true表示已处理并阻止成就追踪，false表示继续默认处理
         *
         * 当玩家点击游戏对象时调用，在OnGossipHello之前触发。
         * 返回true会阻止相关的成就追踪。
         */
        virtual bool OnReportUse(Player* /*player*/) { return false; }

        /**
         * @brief 游戏对象被摧毁
         * @param attacker 摧毁者（可能为nullptr）
         * @param eventId 事件ID
         *
         * 当游戏对象被摧毁时调用。
         */
        virtual void Destroyed(WorldObject* /*attacker*/, uint32 /*eventId*/) { }

        /**
         * @brief 游戏对象受到伤害
         * @param attacker 造成伤害的攻击者
         * @param eventId 事件ID
         *
         * 当游戏对象受到伤害时调用。
         */
        virtual void Damaged(WorldObject* /*attacker*/, uint32 /*eventId*/) { }

        /**
         * @brief 获取数据
         * @param id 数据标识符
         * @return 存储的数据值
         *
         * 用于从AI获取特定数据值，常用于脚本间通信。
         */
        virtual uint32 GetData(uint32 /*id*/) const { return 0; }

        /**
         * @brief 设置64位数据
         * @param id 数据标识符
         * @param value 要设置的64位值
         *
         * 用于在AI中存储64位数据。
         */
        virtual void SetData64(uint32 /*id*/, uint64 /*value*/) { }

        /**
         * @brief 获取64位数据
         * @param id 数据标识符
         * @return 存储的64位数据值
         *
         * 用于从AI获取64位数据值。
         */
        virtual uint64 GetData64(uint32 /*id*/) const { return 0; }

        /**
         * @brief 设置数据
         * @param id 数据标识符
         * @param value 要设置的值
         *
         * 用于在AI中存储32位数据，常用于存储状态或进度。
         */
        virtual void SetData(uint32 /*id*/, uint32 /*value*/) { }

        /**
         * @brief 游戏事件触发
         * @param start true表示事件开始，false表示事件结束
         * @param eventId 游戏事件ID
         *
         * 当游戏事件开始或结束时调用。
         * 用于响应服务器端的全局事件。
         */
        virtual void OnGameEvent(bool /*start*/, uint16 /*eventId*/) { }

        /**
         * @brief 拾取状态变化
         * @param state 新的拾取状态
         * @param unit 触发状态变化的单位
         *
         * 当游戏对象的拾取状态改变时调用。
         * 例如：宝箱被打开、采集对象被采集等。
         */
        virtual void OnLootStateChanged(uint32 /*state*/, Unit* /*unit*/) { }

        /**
         * @brief 游戏对象状态变化
         * @param state 新的状态
         *
         * 当游戏对象的内部状态改变时调用。
         * 状态包括：激活、停用、运输中等。
         */
        virtual void OnStateChanged(uint32 /*state*/) { }

        /**
         * @brief 事件通知
         * @param eventId 事件ID
         *
         * 用于通知AI某个特定事件已发生。
         * 是一种通用的事件通知机制。
         */
        virtual void EventInform(uint32 /*eventId*/) { }

        /**
         * @brief 法术命中游戏对象
         * @param caster 施法者
         * @param spellInfo 法术信息
         *
         * 当法术命中游戏对象时调用。
         * 用于处理法术对游戏对象的特殊效果。
         */
        virtual void SpellHit(WorldObject* /*caster*/, SpellInfo const* /*spellInfo*/) { }

        /**
         * @brief 法术命中目标
         * @param target 被命中的目标
         * @param spellInfo 法术信息
         *
         * 当游戏对象施放的法术命中目标时调用。
         */
        virtual void SpellHitTarget(WorldObject* /*target*/, SpellInfo const* /*spellInfo*/) { }

        /**
         * @brief 成功召唤生物
         * @param summon 被召唤的生物
         *
         * 当游戏对象成功召唤其他生物时调用。
         * 用于管理召唤物，如设置所有者、初始化状态等。
         */
        virtual void JustSummoned(Creature* /*summon*/) { }

        /**
         * @brief 召唤物消失
         * @param summon 消失的召唤物
         *
         * 当游戏对象召唤的生物消失时调用。
         */
        virtual void SummonedCreatureDespawn(Creature* /*summon*/) { }

        /**
         * @brief 召唤物死亡
         * @param summon 死亡的召唤物
         * @param killer 击杀者
         *
         * 当游戏对象召唤的生物被杀死时调用。
         */
        virtual void SummonedCreatureDies(Creature* /*summon*/, Unit* /*killer*/) { }
};

/**
 * @brief 空游戏对象AI类
 *
 * 这是一个特殊的游戏对象AI实现，用于那些不需要任何AI行为的游戏对象。
 * 所有函数都是空实现，不执行任何操作。
 *
 * 当游戏对象不需要任何特殊逻辑时，系统会自动分配此AI。
 * 这避免了nullptr检查，并提供了统一的接口。
 */
class TC_GAME_API NullGameObjectAI : public GameObjectAI
{
    public:
        /**
         * @brief 构造函数
         * @param go 关联的游戏对象指针
         */
        explicit NullGameObjectAI(GameObject* go);

        /**
         * @brief 更新AI逻辑（空实现）
         * @param diff 距离上次更新的时间差（毫秒）
         */
        void UpdateAI(uint32 /*diff*/) override { }

        /**
         * @brief 检查此AI是否适用于指定游戏对象
         * @param go 要检查的游戏对象
         * @return 返回PERMIT_BASE_IDLE，表示这是一个空闲AI
         */
        static int32 Permissible(GameObject const* go);
};
#endif
