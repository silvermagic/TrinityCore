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
 * @file ScriptedEscortAI.h
 * @brief 护送AI模块头文件
 *
 * 本模块提供了护送任务的AI实现,用于处理NPC护送玩家沿预定路径移动的任务场景。
 * 主要功能包括:
 * - 路径点管理和移动控制
 * - 战斗状态下的自动返回机制
 * - 玩家距离检测和任务失败处理
 * - 支持循环路径和即时重生
 * - 任务失败时的自动处理
 */

#ifndef TRINITY_SCRIPTEDESCORTAI_H
#define TRINITY_SCRIPTEDESCORTAI_H

#include "ScriptedCreature.h"
#include "WaypointDefines.h"

class Quest;

/** 默认最大玩家距离常量,玩家超过此距离将导致护送失败 */
#define DEFAULT_MAX_PLAYER_DISTANCE 100

/**
 * @brief 护送状态枚举
 *
 * 定义了护送任务的各种状态,使用位掩码实现状态组合
 */
enum EscortState : uint32
{
    STATE_ESCORT_NONE       = 0x00, ///< 无护送任务进行中
    STATE_ESCORT_ESCORTING  = 0x01, ///< 护送任务进行中
    STATE_ESCORT_RETURNING  = 0x02, ///< 护送者战斗后返回中
    STATE_ESCORT_PAUSED     = 0x04  ///< 护送暂停,不会移动到下一个路径点
};

/**
 * @brief 护送AI类
 *
 * 提供NPC护送任务的AI实现,继承自ScriptedAI。
 * 支持路径点移动、战斗辅助、玩家距离检测等功能。
 *
 * 主要职责:
 * - 管理护送NPC沿路径点的移动
 * - 处理战斗状态和脱战后返回
 * - 检测玩家距离并在超限时处理护送失败
 * - 支持任务关联和任务失败处理
 * - 提供路径循环和即时重生选项
 */
struct TC_GAME_API EscortAI : public ScriptedAI
{
    public:
        /**
         * @brief 构造函数
         * @param creature 关联的生物对象指针
         */
        explicit EscortAI(Creature* creature);

        /**
         * @brief 析构函数
         */
        ~EscortAI() { }

        /**
         * @brief 初始化AI
         *
         * 在AI创建时调用,用于初始化护送状态和设置初始延迟
         *
         * @note 调用时机: AI实例创建后立即调用
         */
        void InitializeAI() override;

        /**
         * @brief 视线范围内的单位检测
         * @param who 进入视线范围的单位
         *
         * 当单位进入视线范围时调用,用于检测是否需要协助玩家战斗
         *
         * @note 调用时机: 每个游戏周期检测视线范围内的单位
         * @note 性能注意: 会进行距离和视线检测,需优化调用频率
         */
        void MoveInLineOfSight(Unit* who) override;

        /**
         * @brief 生物死亡回调
         * @param killer 击杀者(未使用)
         *
         * 当护送NPC死亡时调用,如果有关联任务则使玩家任务失败
         *
         * @note 调用时机: 生物死亡时
         */
        void JustDied(Unit*) override;

        /**
         * @brief 返回上一个路径点
         *
         * 用于战斗结束后返回战斗前的位置
         *
         * @note 通常在EnterEvadeMode中调用
         */
        void ReturnToLastPoint();

        /**
         * @brief 进入脱战模式
         * @param why 脱战原因
         *
         * 当生物脱离战斗时调用,根据护送状态决定返回路径点或回家
         *
         * @note 调用时机: 生物脱离战斗时
         */
        void EnterEvadeMode(EvadeReason /*why*/ = EVADE_REASON_OTHER) override;

        /**
         * @brief 移动完成通知
         * @param type 移动类型
         * @param id 路径点ID
         *
         * 当生物完成一个移动动作时调用,用于处理路径点到达逻辑
         *
         * @note 调用时机: 移动生成器完成移动时
         */
        void MovementInform(uint32, uint32) override;

        /**
         * @brief 更新AI主函数
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 内部更新函数,处理路径点移动、暂停计时器和玩家距离检测,
         * 然后调用UpdateEscortAI进行派生类的自定义逻辑
         *
         * @note 调用时机: 每个游戏周期调用
         * @note 性能注意: 包含计时器操作和距离检测,注意性能影响
         */
        void UpdateAI(uint32 diff) override;

        /**
         * @brief 更新护送AI(可重写)
         * @param diff 距离上次更新的时间差(毫秒)
         *
         * 虚函数,供派生类实现自定义的更新逻辑,如技能、脚本事件等
         *
         * @note 调用时机: 由UpdateAI内部调用
         * @note 性能注意: 每帧调用,避免重计算
         */
        virtual void UpdateEscortAI(uint32 diff);

        /**
         * @brief 添加路径点
         * @param id 路径点ID
         * @param x X坐标
         * @param y Y坐标
         * @param z Z坐标
         * @param orientation 朝向(默认0)
         * @param waitTime 等待时间(默认0)
         *
         * 向路径中添加一个新的路径点,坐标会被标准化处理
         *
         * @note 调用时机: 在Start之前手动构建路径时
         */
        void AddWaypoint(uint32 id, float x, float y, float z, float orientation = 0.f, Milliseconds waitTime = 0s);

        /**
         * @brief 开始护送
         * @param isActiveAttacker 是否主动攻击(已过时,由阵营决定)
         * @param run 是否跑步(默认false,步行)
         * @param playerGUID 触发护送的玩家GUID
         * @param quest 关联的任务对象(可为nullptr)
         * @param instantRespawn 是否即时重生(默认false)
         * @param canLoopPath 是否循环路径(默认false)
         * @param resetWaypoints 是否重置路径点(默认true)
         *
         * 启动护送任务,设置护送参数并开始沿路径移动
         *
         * @note 调用时机: 玩家接受护送任务或触发护送事件时
         * @note 性能注意: 会清空移动生成器并初始化路径
         */
        void Start(bool isActiveAttacker = true, bool run = false, ObjectGuid playerGUID = ObjectGuid::Empty, Quest const* quest = nullptr, bool instantRespawn = false, bool canLoopPath = false, bool resetWaypoints = true);

        /**
         * @brief 设置移动方式(跑/走)
         * @param on true为跑步,false为步行(默认true)
         *
         * 切换生物的移动方式,并更新所有路径点的移动类型
         *
         * @note 调用时机: 需要改变移动速度时
         */
        void SetRun(bool on = true);

        /**
         * @brief 设置护送暂停状态
         * @param on true为暂停,false为继续
         *
         * 暂停或继续护送移动,暂停时不移动到下一个路径点
         *
         * @note 调用时机: 需要在护送过程中停止移动时
         */
        void SetEscortPaused(bool on);

        /**
         * @brief 设置暂停计时器
         * @param timer 暂停时长
         *
         * 设置路径点之间的暂停时间
         */
        void SetPauseTimer(Milliseconds timer) { _pauseTimer = timer; }

        /**
         * @brief 检查是否具有指定护送状态
         * @param escortState 要检查的状态(位掩码)
         * @return 如果具有该状态返回true,否则返回false
         */
        bool HasEscortState(uint32 escortState) { return (_escortState & escortState) != 0; }

        /**
         * @brief 检查是否正在护送
         * @return 如果正在护送返回true,否则返回false
         */
        bool IsEscorted() const override { return !_playerGUID.IsEmpty(); }

        /**
         * @brief 设置最大玩家距离
         * @param newMax 新的最大距离
         *
         * 设置玩家离开护送NPC的最大允许距离,超过此距离护送将失败
         */
        void SetMaxPlayerDistance(float newMax) { _maxPlayerDistance = newMax; }

        /**
         * @brief 获取最大玩家距离
         * @return 当前设置的最大玩家距离
         */
        float GetMaxPlayerDistance() const { return _maxPlayerDistance; }

        /**
         * @brief 设置护送结束时是否消失
         * @param despawn true为消失,false为保留
         */
        void SetDespawnAtEnd(bool despawn) { _despawnAtEnd = despawn; }

        /**
         * @brief 设置玩家过远时是否消失
         * @param despawn true为消失,false为保留
         */
        void SetDespawnAtFar(bool despawn) { _despawnAtFar = despawn; }

        /**
         * @brief 获取是否为主动攻击者(已过时)
         * @return 当前设置
         * @deprecated 此功能已过时,由阵营决定攻击行为
         */
        bool IsActiveAttacker() const { return _activeAttacker; }

        /**
         * @brief 设置是否为主动攻击者(已过时)
         * @param enable true为主动攻击
         * @deprecated 此功能已过时,由阵营决定攻击行为
         */
        void SetActiveAttacker(bool enable) { _activeAttacker = enable; }

        /**
         * @brief 获取事件触发者的GUID
         * @return 触发护送的玩家GUID
         */
        ObjectGuid GetEventStarterGUID() const { return _playerGUID; }

    protected:
        /**
         * @brief 获取护送玩家对象
         * @return 玩家指针,如果玩家不存在返回nullptr
         */
        Player* GetPlayerForEscort();

    private:
        /**
         * @brief 协助玩家战斗
         * @param who 敌对单位
         * @return 如果成功协助返回true,否则返回false
         *
         * 检查条件并协助玩家攻击敌对单位
         */
        bool AssistPlayerInCombatAgainst(Unit* who);

        /**
         * @brief 检查玩家或队伍是否在范围内
         * @return 如果玩家或队伍成员在范围内返回true,否则返回false
         */
        bool IsPlayerOrGroupInRange();

        /**
         * @brief 从脚本系统加载路径点
         *
         * 从ScriptSystemMgr加载生物的预设路径
         */
        void FillPointMovementListForCreature();

        /**
         * @brief 添加护送状态
         * @param escortState 要添加的状态(位掩码)
         */
        void AddEscortState(uint32 escortState) { _escortState |= escortState; }

        /**
         * @brief 移除护送状态
         * @param escortState 要移除的状态(位掩码)
         */
        void RemoveEscortState(uint32 escortState) { _escortState &= ~escortState; }

        ObjectGuid _playerGUID;                ///< 触发护送的玩家GUID
        Milliseconds _pauseTimer;              ///< 暂停计时器,用于路径点间的等待
        uint32 _playerCheckTimer;              ///< 玩家距离检测计时器
        uint32 _escortState;                   ///< 当前护送状态(位掩码组合)
        float _maxPlayerDistance;              ///< 最大玩家距离限制

        Quest const* _escortQuest;             ///< 关联的护送任务,通常在Start()中传入

        WaypointPath _path;                    ///< 路径点列表

        bool _activeAttacker;                  ///< 是否主动攻击者(已过时,由阵营决定)
        bool _running;                         ///< 是否跑步(默认步行)
        bool _instantRespawn;                  ///< 护送结束后是否立即重生
        bool _returnToStart;                   ///< 是否循环路径而不消失(不适用于常规护送任务)
        bool _despawnAtEnd;                    ///< 护送结束时是否消失
        bool _despawnAtFar;                    ///< 玩家过远时是否消失
        bool _manualPath;                      ///< 是否使用手动添加的路径
        bool _hasImmuneToNPCFlags;             ///< 是否具有NPC免疫标志
        bool _started;                         ///< 护送是否已开始
        bool _ended;                           ///< 护送是否已结束
        bool _resume;                          ///< 是否需要恢复移动
};

#endif
