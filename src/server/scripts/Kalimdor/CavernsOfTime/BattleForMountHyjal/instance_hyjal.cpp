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
 * @file    instance_hyjal.cpp
 * @brief   海加尔山副本实例脚本
 * @details 该模块实现了海加尔山副本的实例数据管理，包括：
 *          - 副本进度和BOSS状态管理
 *          - 游戏对象（传送门、古代宝石）的管理
 *          - 小怪计数和世界状态更新
 *          - 撤退进度追踪
 *          - 阿克蒙德出场控制
 *
 *          副本包含5个BOSS战斗：
 *          1. 雷基·冬寒（Rage Winterchill）
 *          2. 阿纳塞隆（Anetheron）
 *          3. 卡兹洛加（Kaz'rogal）
 *          4. 阿兹加洛（Azgalor）
 *          5. 阿克蒙德（Archimonde）
 *
 * ScriptData
 * SDName: Instance_Mount_Hyjal
 * SD%Complete: 100
 * SDComment: Instance Data Scripts and functions to acquire mobs and set encounter status for use in various Hyjal Scripts
 * SDCategory: Caverns of Time, Mount Hyjal
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "hyjal.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"

/**
 * @brief 海加尔山副本战斗遭遇列表
 * @details 定义副本中的5个BOSS遭遇
 *          0 - 雷基·冬寒事件
 *          1 - 阿纳塞隆事件
 *          2 - 卡兹洛加事件
 *          3 - 阿兹加洛事件
 *          4 - 阿克蒙德事件
 */

/**
 * @brief 喊话枚举
 * @details 定义阿克蒙德的喊话ID
 */
enum Yells
{
    YELL_ARCHIMONDE_INTRO = 8  ///< 阿克蒙德出场喊话
};

/**
 * @brief 生物数据映射表
 * @details 将生物入口ID映射到数据ID，用于副本脚本获取生物对象
 */
ObjectData const creatureData[] =
{
    { RAGE_WINTERCHILL,   DATA_RAGEWINTERCHILL    },  ///< 雷基·冬寒
    { ANETHERON,          DATA_ANETHERON          },  ///< 阿纳塞隆
    { KAZROGAL,           DATA_KAZROGAL           },  ///< 卡兹洛加
    { AZGALOR,            DATA_AZGALOR            },  ///< 阿兹加洛
    { ARCHIMONDE,         DATA_ARCHIMONDE         },  ///< 阿克蒙德
    { JAINA,              DATA_JAINAPROUDMOORE    },  ///< 吉安娜·普罗德摩尔
    { THRALL,             DATA_THRALL             },  ///< 萨尔
    { TYRANDE,            DATA_TYRANDEWHISPERWIND },  ///< 泰兰德·语风
    { NPC_CHANNEL_TARGET, DATA_CHANNEL_TARGET     },  ///< 引导目标（世界之树）
    { 0,                  0                       }   ///< 结束标记
};

/**
 * @class   instance_hyjal
 * @brief   海加尔山副本实例脚本
 * @details 管理海加尔山副本的整体状态，包括BOSS进度、小怪计数、撤退状态等
 */
class instance_hyjal : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     * @details 注册海加尔山副本脚本，地图ID为534
     */
    instance_hyjal() : InstanceMapScript(HyjalScriptName, 534) { }

    /**
     * @brief 获取实例脚本
     * @param map 副本地图指针
     * @return 返回海加尔山实例脚本实例
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_mount_hyjal_InstanceMapScript(map);
    }

    /**
     * @struct  instance_mount_hyjal_InstanceMapScript
     * @brief   海加尔山实例脚本实现
     * @details 继承自InstanceScript，实现海加尔山副本的所有实例管理功能
     */
    struct instance_mount_hyjal_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         * @details 初始化副本脚本，设置数据头和BOSS数量
         */
        instance_mount_hyjal_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(EncounterCount);
            LoadObjectData(creatureData, nullptr);

            RaidDamage = 0;      ///< 团队累计伤害（用于特殊机制）
            Trash = 0;           ///< 剩余小怪数量
            hordeRetreat = 0;    ///< 部落撤退状态
            allianceRetreat = 0; ///< 联盟撤退状态

            ArchiYell = false;   ///< 阿克蒙德是否已喊话
        }

        /**
         * @brief 游戏对象创建回调
         * @param go 游戏对象指针
         * @details 当游戏对象创建时调用，处理传送门和古代宝石等对象
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_HORDE_ENCAMPMENT_PORTAL:
                    // 部落营地传送门
                    HordeGate = go->GetGUID();
                    if (allianceRetreat)
                        HandleGameObject(ObjectGuid::Empty, true, go);   // 如果联盟已撤退，开启传送门
                    else
                        HandleGameObject(ObjectGuid::Empty, false, go);  // 否则关闭传送门
                    break;

                case GO_NIGHT_ELF_VILLAGE_PORTAL:
                    // 暗夜精灵村庄传送门
                    ElfGate = go->GetGUID();
                    if (hordeRetreat)
                        HandleGameObject(ObjectGuid::Empty, true, go);   // 如果部落已撤退，开启传送门
                    else
                        HandleGameObject(ObjectGuid::Empty, false, go);  // 否则关闭传送门
                    break;

                case GO_ANCIENT_GEM:
                    // 古代宝石，用于撤退事件
                    m_uiAncientGemGUID.push_back(go->GetGUID());
                    break;
            }

            InstanceScript::OnGameObjectCreate(go);
        }

        /**
         * @brief 生物创建回调
         * @param creature 生物指针
         * @details 当生物创建时调用，处理阿克蒙德的初始状态
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case ARCHIMONDE:
                    // 阿克蒙德：如果阿兹加洛未击杀，则隐藏并设为被动状态
                    if (GetBossState(DATA_AZGALOR) != DONE)
                    {
                        creature->SetVisible(false);
                        creature->SetReactState(REACT_PASSIVE);
                    }
                    break;
            }

            InstanceScript::OnCreatureCreate(creature);
        }

        /**
         * @brief 设置数据
         * @param type 数据类型
         * @param data 数据值
         * @details 设置副本中的各种数据，包括小怪计数、撤退状态、团队伤害等
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case DATA_RESET_TRASH_COUNT:
                    // 重置小怪计数
                    Trash = 0;
                    break;

                case DATA_TRASH:
                    // 小怪计数管理
                    if (data)
                        Trash = data;       // 设置小怪数量
                    else
                        Trash--;            // 小怪数量减一
                    DoUpdateWorldState(WORLD_STATE_ENEMYCOUNT, Trash);  // 更新世界状态
                    break;

                case TYPE_RETREAT:
                    // 撤退事件处理
                    if (data == SPECIAL)
                    {
                        // 重新激活所有古代宝石
                        if (!m_uiAncientGemGUID.empty())
                        {
                            for (GuidList::const_iterator itr = m_uiAncientGemGUID.begin(); itr != m_uiAncientGemGUID.end(); ++itr)
                            {
                                // 重生古代宝石（持续24小时）
                                DoRespawnGameObject(*itr, 24h);
                            }
                        }
                    }
                    break;

                case DATA_ALLIANCE_RETREAT:
                    // 联盟撤退状态
                    allianceRetreat = data;
                    HandleGameObject(HordeGate, true);  // 开启部落传送门
                    SaveToDB();
                    break;

                case DATA_HORDE_RETREAT:
                    // 部落撤退状态
                    hordeRetreat = data;
                    HandleGameObject(ElfGate, true);    // 开启暗夜精灵传送门
                    SaveToDB();
                    break;

                case DATA_RAIDDAMAGE:
                    // 团队累计伤害（用于特殊机制）
                    RaidDamage += data;
                    if (RaidDamage >= MINRAIDDAMAGE)
                        RaidDamage = MINRAIDDAMAGE;     // 限制最大值
                    break;

                case DATA_RESET_RAIDDAMAGE:
                    // 重置团队累计伤害
                    RaidDamage = 0;
                    break;
            }

            TC_LOG_DEBUG("scripts", "Instance Hyjal: Instance data updated for event {} (Data={})", type, data);
        }

        /**
         * @brief 设置BOSS状态
         * @param id BOSS ID
         * @param state 遭遇状态
         * @return 是否成功设置状态
         * @details 当BOSS状态改变时调用，处理阿兹加洛击杀后阿克蒙德的出场
         */
        bool SetBossState(uint32 id, EncounterState state) override
        {
            if (!InstanceScript::SetBossState(id, state))
                return false;

            switch (id)
            {
                case DATA_AZGALOR:
                    // 阿兹加洛被击杀后
                    if (state == DONE)
                    {
                        // 加载阿克蒙德所在区域的网格
                        instance->LoadGrid(5581.49f, -3445.63f);

                        // 获取并显示阿克蒙德
                        if (Creature* archimonde = GetCreature(DATA_ARCHIMONDE))
                        {
                            archimonde->SetVisible(true);
                            archimonde->SetReactState(REACT_AGGRESSIVE);

                            // 如果尚未喊话，则让阿克蒙德喊出场白
                            if (!ArchiYell)
                            {
                                ArchiYell = true;
                                archimonde->AI()->Talk(YELL_ARCHIMONDE_INTRO);
                            }
                        }
                    }
                    break;
            }

            return true;
        }

        /**
         * @brief 读取保存数据
         * @param loadStream 输入字符串流
         * @details 从存档数据中读取撤退状态和团队伤害
         */
        void ReadSaveDataMore(std::istringstream& loadStream) override
        {
            loadStream >> allianceRetreat >> hordeRetreat >> RaidDamage;
        }

        /**
         * @brief 写入保存数据
         * @param saveStream 输出字符串流
         * @details 将撤退状态和团队伤害写入存档数据
         */
        void WriteSaveDataMore(std::ostringstream& saveStream) override
        {
            saveStream << allianceRetreat << ' ' << hordeRetreat << ' ' << RaidDamage;
        }

        /**
         * @brief 获取数据
         * @param type 数据类型
         * @return 对应的数据值
         * @details 获取小怪计数、撤退状态、团队伤害等数据
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case DATA_TRASH:            return Trash;
                case DATA_ALLIANCE_RETREAT: return allianceRetreat;
                case DATA_HORDE_RETREAT:    return hordeRetreat;
                case DATA_RAIDDAMAGE:       return RaidDamage;
            }
            return 0;
        }

        protected:
            GuidList m_uiAncientGemGUID;   ///< 古代宝石GUID列表
            ObjectGuid HordeGate;           ///< 部落传送门GUID
            ObjectGuid ElfGate;             ///< 暗夜精灵传送门GUID
            uint32 Trash;                   ///< 剩余小怪数量
            uint32 hordeRetreat;            ///< 部落撤退状态
            uint32 allianceRetreat;         ///< 联盟撤退状态
            uint32 RaidDamage;              ///< 团队累计伤害
            bool ArchiYell;                 ///< 阿克蒙德是否已喊话
    };
};

/**
 * @brief 添加海加尔山实例脚本到系统
 */
void AddSC_instance_mount_hyjal()
{
    new instance_hyjal();
}
