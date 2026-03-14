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
 * @file instance_shattered_halls.cpp
 * @brief 破碎大厅副本实例脚本模块
 *
 * 本模块实现破碎大厅副本的实例管理逻辑,包括:
 * 1. Boss状态管理和持久化
 * 2. 门和机关的控制
 * 3. 英雄模式下的行刑者事件
 * 4. 阵营相关的NPC变化
 *
 * 副本结构:
 * - 大术士尼瑟雷库尔: 第一个Boss
 * - 战争使者奥姆罗格: 第二个Boss
 * - 战争酋长卡加斯·刃拳: 最终Boss
 * - 血卫士波鲁格(英雄模式): 可选Boss
 *
 * 特殊机制:
 * - 英雄模式下行刑者会定期处决囚犯,玩家需在限定时间内击杀
 * - 处决进度通过光环计时器显示,具有紧迫感
 * - 联盟和部落对应不同的NPC和囚犯类型
 */

/* ScriptData
SDName: Instance_Shattered_Halls
SD%Complete: 50
SDComment: instance not complete
SDCategory: Hellfire Citadel, Shattered Halls
EndScriptData */

#include "ScriptMgr.h"
#include "InstanceScript.h"
#include "Map.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "shattered_halls.h"
#include "SpellAuraEffects.h"
#include "SpellAuras.h"
#include "SpellScript.h"
#include "TemporarySummon.h"

/**
 * @brief 门数据配置表
 *
 * 定义副本中各个门与Boss状态的关联关系
 * 当Boss状态改变时,自动控制门的开闭
 */
DoorData const doorData[] =
{
    { GO_GRAND_WARLOCK_CHAMBER_DOOR_1, DATA_NETHEKURSE, DOOR_TYPE_PASSAGE },  // 大术士房间门1
    { GO_GRAND_WARLOCK_CHAMBER_DOOR_2, DATA_NETHEKURSE, DOOR_TYPE_PASSAGE },  // 大术士房间门2
    { 0,                               0,               DOOR_TYPE_ROOM }       // 结束标记
};

/**
 * @brief 破碎大厅副本实例脚本类
 *
 * 管理副本状态、Boss进度和英雄模式行刑者事件
 */
class instance_shattered_halls : public InstanceMapScript
{
    public:
        /**
         * @brief 构造函数
         *
         * 注册破碎大厅副本实例脚本,地图ID为540
         */
        instance_shattered_halls() : InstanceMapScript(SHScriptName, 540) { }

        /**
         * @brief 获取实例脚本
         * @param map 副本地图指针
         * @return 实例脚本指针
         *
         * 工厂方法,创建并返回破碎大厅实例脚本
         *
         * @调用时机 服务器创建副本实例时
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_shattered_halls_InstanceMapScript(map);
        }

        /**
         * @brief 破碎大厅实例脚本实现类
         *
         * 继承自InstanceScript,实现副本的完整管理逻辑
         */
        struct instance_shattered_halls_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 副本地图指针
             *
             * 初始化副本脚本:
             * 1. 设置数据头和Boss数量
             * 2. 加载门数据
             * 3. 初始化行刑者事件相关变量
             */
            instance_shattered_halls_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                SetBossNumber(EncounterCount);
                LoadDoorData(doorData);  // 加载门与Boss的关联数据
                executionTimer = 0;  // 行刑计时器
                executed = 0;  // 已处决囚犯数量
                _team = 0;  // 阵营ID
            }

            /**
             * @brief 玩家进入副本时调用
             * @param player 进入的玩家
             *
             * 处理玩家进入副本时的逻辑:
             * 1. 记录阵营ID
             * 2. 移除旧的行刑者光环
             * 3. 如果行刑者事件正在进行,根据进度给玩家施加对应的光环
             *
             * @调用时机 玩家进入副本时
             */
            void OnPlayerEnter(Player* player) override
            {
                Aura* ex = nullptr;

                // 记录阵营(第一次进入时)
                if (!_team)
                    _team = player->GetTeam();

                // 移除旧的行刑者光环
                player->CastSpell(player, SPELL_REMOVE_KARGATH_EXECUTIONER, true);

                // 如果没有进行中的行刑者事件,返回
                if (!executionTimer || executionerGUID.IsEmpty())
                    return;

                // 根据已处决数量施加对应的光环
                switch (executed)
                {
                    case 0:
                        ex = player->AddAura(SPELL_KARGATH_EXECUTIONER_1, player);  // 第一个囚犯处决中
                        break;
                    case 1:
                        ex = player->AddAura(SPELL_KARGATH_EXECUTIONER_2, player);  // 第二个囚犯处决中
                        break;
                    case 2:
                        ex = player->AddAura(SPELL_KARGATH_EXECUTIONER_3, player);  // 第三个囚犯处决中
                        break;
                    default:
                        break;
                }

                // 设置光环剩余时间
                if (ex)
                    ex->SetDuration(executionTimer);
            }

            /**
             * @brief 生物创建时调用
             * @param creature 新创建的生物
             *
             * 当副本中有新生物刷新时:
             * 1. 记录阵营ID(如果还未记录)
             * 2. 根据生物类型保存GUID或执行特殊处理:
             *    - Boss的GUID
             *    - 根据阵营更新NPC(联盟/部落NPC替换)
             *    - 行刑者刷新时启动行刑者事件
             *    - 记录囚犯和受害者的GUID
             *
             * @调用时机 副本中任何生物刷新时
             */
            void OnCreatureCreate(Creature* creature) override
            {
                // 如果阵营未设置,从第一个玩家获取
                if (!_team)
                {
                    Map::PlayerList const& players = instance->GetPlayers();
                    if (!players.isEmpty())
                        if (Player* player = players.begin()->GetSource())
                            _team = player->GetTeam();
                }

                switch (creature->GetEntry())
                {
                    case NPC_GRAND_WARLOCK_NETHEKURSE:
                        nethekurseGUID = creature->GetGUID();  // 保存大术士GUID
                        break;
                    case NPC_KARGATH_BLADEFIST:
                        kargathGUID = creature->GetGUID();  // 保存卡加斯GUID
                        break;
                    case NPC_RANDY_WHIZZLESPROCKET:
                        // 联盟NPC,如果是部落则替换为部落NPC
                        if (_team == HORDE)
                            creature->UpdateEntry(NPC_DRISELLA);
                        break;
                    case NPC_SHATTERED_EXECUTIONER:
                        // 行刑者刷新,启动行刑者事件
                        executionTimer = 55 * MINUTE * IN_MILLISECONDS;  // 55分钟计时
                        DoCastSpellOnPlayers(SPELL_KARGATH_EXECUTIONER_1);  // 给所有玩家施加第一个光环
                        executionerGUID = creature->GetGUID();  // 保存行刑者GUID
                        SaveToDB();  // 保存进度
                        break;
                    case NPC_CAPTAIN_ALINA:
                    case NPC_CAPTAIN_BONESHATTER:
                        victimsGUID[0] = creature->GetGUID();  // 第一个受害者(军官)
                        break;
                    case NPC_ALLIANCE_VICTIM_1:
                    case NPC_HORDE_VICTIM_1:
                        victimsGUID[1] = creature->GetGUID();  // 第二个受害者
                        break;
                    case NPC_ALLIANCE_VICTIM_2:
                    case NPC_HORDE_VICTIM_2:
                        victimsGUID[2] = creature->GetGUID();  // 第三个受害者
                        break;
                }
            }

            /**
             * @brief 单位死亡时调用
             * @param unit 死亡的单位
             *
             * 监控血卫士波鲁格死亡事件(英雄模式可选Boss)
             *
             * @调用时机 副本中任何单位死亡时
             */
            void OnUnitDeath(Unit* unit) override
            {
                // 血卫士波鲁格死亡时,设置Boss状态为完成
                if (unit->GetEntry() == NPC_BLOOD_GUARD_PORUNG)
                    SetBossState(DATA_PORUNG, DONE);
            }

            /**
             * @brief 设置Boss状态
             * @param type Boss类型标识
             * @param state 新的战斗状态
             * @return 成功返回true,失败返回false
             *
             * 当Boss状态改变时触发相应逻辑:
             * - 行刑者击杀:停止行刑者事件,移除玩家光环
             * - 卡加斯击杀:重置行刑者状态
             *
             * @调用时机 Boss战斗开始、结束、重置时
             */
            bool SetBossState(uint32 type, EncounterState state) override
            {
                if (!InstanceScript::SetBossState(type, state))
                    return false;

                switch (type)
                {
                    case DATA_SHATTERED_EXECUTIONER:
                        if (state == DONE)
                        {
                            // 行刑者被击杀,停止行刑者事件
                            DoCastSpellOnPlayers(SPELL_REMOVE_KARGATH_EXECUTIONER);  // 移除光环
                            executionTimer = 0;  // 停止计时
                            SaveToDB();  // 保存进度
                        }
                        break;
                    case DATA_KARGATH:
                        // 卡加斯击杀后,重置行刑者状态
                        if (Creature* executioner = instance->GetCreature(executionerGUID))
                            executioner->AI()->Reset(); // trigger removal of IMMUNE_TO_PC flag // 移除免疫标志
                        break;
                    case DATA_OMROGG:
                        break;
                }
                return true;
            }

            /**
             * @brief 获取对象GUID
             * @param data 数据类型标识
             * @return 对应类型的对象GUID,不存在则返回空GUID
             *
             * 根据数据类型返回对应的Boss或重要对象GUID
             *
             * @调用时机 Boss AI或脚本需要获取其他对象引用时
             */
            ObjectGuid GetGuidData(uint32 data) const override
            {
                switch (data)
                {
                    case NPC_GRAND_WARLOCK_NETHEKURSE:
                        return nethekurseGUID;  // 返回大术士GUID
                    case NPC_KARGATH_BLADEFIST:
                        return kargathGUID;  // 返回卡加斯GUID
                    case NPC_SHATTERED_EXECUTIONER:
                        return executionerGUID;  // 返回行刑者GUID
                    case DATA_FIRST_PRISONER:
                    case DATA_SECOND_PRISONER:
                    case DATA_THIRD_PRISONER:
                        return victimsGUID[data - DATA_FIRST_PRISONER];  // 返回囚犯GUID
                    default:
                        return ObjectGuid::Empty;
                }
            }

            /**
             * @brief 写入额外的保存数据
             * @param data 输出字符串流
             *
             * 保存英雄模式的行刑者事件进度:
             * - 已处决囚犯数量
             * - 剩余处决时间
             *
             * @调用时机 副本进度保存时
             */
            void WriteSaveDataMore(std::ostringstream& data) override
            {
                // 只在英雄模式保存
                if (!instance->IsHeroic())
                    return;

                data << uint32(executed) << ' '  // 已处决数量
                    << executionTimer << ' ';  // 剩余时间
            }

            /**
             * @brief 读取额外的保存数据
             * @param data 输入字符串流
             *
             * 加载英雄模式的行刑者事件进度:
             * 1. 读取已处决囚犯数量
             * 2. 读取剩余处决时间
             * 3. 如果事件未完成,重新召唤行刑者和剩余囚犯
             *
             * @调用时机 副本进度加载时
             */
            void ReadSaveDataMore(std::istringstream& data) override
            {
                // 只在英雄模式加载
                if (!instance->IsHeroic())
                    return;

                uint32 readbuff;
                data >> readbuff;
                executed = uint8(readbuff);  // 读取已处决数量
                data >> readbuff;  // 读取剩余时间

                // 验证数据有效性
                if (executed > VictimCount)
                {
                    executed = VictimCount;
                    executionTimer = 0;
                    return;
                }

                // 如果没有剩余时间,返回
                if (!readbuff)
                    return;

                Creature* executioner = nullptr;

                // 加载行刑者所在区域的网格
                instance->LoadGrid(Executioner.GetPositionX(), Executioner.GetPositionY());
                // 如果行刑者未刷新,召唤行刑者
                if (Creature* kargath = instance->GetCreature(kargathGUID))
                    if (executionerGUID.IsEmpty())
                        executioner = kargath->SummonCreature(NPC_SHATTERED_EXECUTIONER, Executioner);

                // 召唤剩余的囚犯
                if (executioner)
                    for (uint8 i = executed; i < VictimCount; ++i)
                        executioner->SummonCreature(executionerVictims[i](GetData(DATA_TEAM_IN_INSTANCE)), executionerVictims[i].GetPos());

                executionTimer = readbuff;  // 恢复剩余时间
            }

            /**
             * @brief 获取数据
             * @param type 数据类型标识
             * @return 对应的数据值
             *
             * 返回实例相关的数据:
             * - 已处决囚犯数量
             * - 副本中的阵营
             *
             * @调用时机 其他脚本查询实例数据时
             */
            uint32 GetData(uint32 type) const override
            {
                switch (type)
                {
                    case DATA_PRISONERS_EXECUTED:
                        return executed;  // 返回已处决囚犯数量
                    case DATA_TEAM_IN_INSTANCE:
                        return _team;  // 返回阵营ID
                    default:
                        return 0;
                }
            }

            /**
             * @brief 更新实例状态(每帧调用)
             * @param diff 距离上次调用的时间(毫秒)
             *
             * 处理英雄模式下的行刑者事件计时:
             * - 55分钟后处决第一个囚犯
             * - 10分钟后处决第二个囚犯
             * - 15分钟后处决第三个囚犯
             *
             * 每次处决:
             * 1. 移除旧光环
             * 2. 施加新光环
             * 3. 通知行刑者AI
             * 4. 保存进度
             *
             * @调用时机 游戏主循环每帧调用
             * @性能注意事项 高频调用函数,避免复杂计算
             */
            void Update(uint32 diff) override
            {
                // 如果没有行刑计时器,返回
                if (!executionTimer)
                    return;

                // 计时器到期
                if (executionTimer <= diff)
                {
                    // 移除旧光环
                    DoCastSpellOnPlayers(SPELL_REMOVE_KARGATH_EXECUTIONER);
                    // 增加处决计数并处理
                    switch (++executed)
                    {
                        case 1:
                            // 第一个囚犯处决完成,开始第二个囚犯计时
                            DoCastSpellOnPlayers(SPELL_KARGATH_EXECUTIONER_2);
                            executionTimer = 10 * MINUTE * IN_MILLISECONDS;  // 10分钟
                            break;
                        case 2:
                            // 第二个囚犯处决完成,开始第三个囚犯计时
                            DoCastSpellOnPlayers(SPELL_KARGATH_EXECUTIONER_3);
                            executionTimer = 15 * MINUTE * IN_MILLISECONDS;  // 15分钟
                            break;
                        default:
                            // 所有囚犯处决完成,停止计时
                            executionTimer = 0;
                            break;
                    }

                    // 通知行刑者AI更新状态
                    if (Creature* executioner = instance->GetCreature(executionerGUID))
                        executioner->AI()->SetData(DATA_PRISONERS_EXECUTED, executed);

                    SaveToDB();  // 保存进度
                }
                else
                    executionTimer -= diff;  // 减少剩余时间
            }

        private:
            // Boss GUID
            ObjectGuid nethekurseGUID;      ///< 大术士尼瑟雷库尔GUID
            ObjectGuid kargathGUID;         ///< 战争酋长卡加斯·刃拳GUID
            ObjectGuid executionerGUID;     ///< 破碎行刑者GUID

            // 行刑者事件相关
            ObjectGuid victimsGUID[3];      ///< 受害者/囚犯GUID数组(3个)

            // 行刑者事件状态
            uint8 executed;                 ///< 已处决囚犯数量(0-3)
            uint32 executionTimer;          ///< 行刑计时器(毫秒)
            uint32 _team;                   ///< 副本中的阵营ID(联盟/部落)
        };
};

/**
 * @brief 注册脚本函数
 *
 * 将破碎大厅实例脚本注册到脚本系统
 *
 * @调用时机 服务器启动时,脚本系统初始化阶段
 */
void AddSC_instance_shattered_halls()
{
    new instance_shattered_halls();
}
