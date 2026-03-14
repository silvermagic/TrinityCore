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
 * @file instance_uldaman.cpp
 * @brief Uldaman 副本实例脚本实现
 *
 * 本模块实现了 Uldaman 副本的实例管理逻辑，包括：
 * - 管理副本进度和首领状态
 * - 控制各种门和机关的开关
 * - 协调阿扎达斯和艾隆纳亚的激活流程
 * - 管理石像守卫、宝库行者、土灵守护者等小怪的生成和状态
 * - 保存和加载副本进度数据
 *
 * 副本结构：
 * 1. 守护者祭坛区域 - 石像守卫逐一激活，全部击杀后开启通往阿扎达斯的门
 * 2. 阿扎达斯区域 - 激活祭坛后唤醒阿扎达斯，战斗中分阶段召唤小怪
 * 3. 艾隆纳亚区域 - 插入基石后开启密封门，激活艾隆纳亚
 */

/* ScriptData
SDName: instance_uldaman
SD%Complete: 80%
SDComment: Need some cosmetics updates when archeadas door are closing (Guardians Waypoints).
SDCategory: Uldaman
EndScriptData */

#include "ScriptMgr.h"
#include "Creature.h"
#include "CreatureAI.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "uldaman.h"

/**
 * @brief 本文件使用的法术枚举
 */
enum Spells
{
    SPELL_ARCHAEDAS_AWAKEN      = 10347,  ///< 阿扎达斯觉醒 - 激活视觉效果
    SPELL_AWAKEN_VAULT_WALKER   = 10258,  ///< 觉醒宝库行者 - 激活宝库行者的法术
    SPELL_FREEZE_ANIM           = 16245,  ///< 冻结动画 - 阿扎达斯的冻结效果
    SPELL_MINION_FREEZE_ANIM    = 10255   ///< 小怪冻结动画 - 小怪的冻结效果
};

/**
 * @brief 事件ID枚举
 */
enum Events
{
    EVENT_SUB_BOSS_AGGRO        = 2228    ///< 副首领仇恨事件 - 触发石像守卫激活
};

/**
 * @brief 艾隆纳亚对话枚举
 */
enum IronayaTalk
{
    SAY_AGGRO = 0   ///< 激活时的喊话
};

/**
 * @brief 艾隆纳亚移动目标位置
 *
 * 艾隆纳亚被基石激活后移动到此位置开始战斗。
 */
const Position IronayaPoint = { -231.228f, 246.6135f, -49.01617f, 0.0f };

/**
 * @brief Uldaman 实例脚本类
 *
 * 继承自 InstanceMapScript，管理整个副本的状态和逻辑。
 */
class instance_uldaman : public InstanceMapScript
{
    public:
        instance_uldaman() : InstanceMapScript(UldamanScriptName, 70) { }

        /**
         * @brief Uldaman 实例脚本实现类
         *
         * 负责管理副本内的所有状态、生物和游戏对象。
         */
        struct instance_uldaman_InstanceMapScript : public InstanceScript
        {
            /**
             * @brief 构造函数
             * @param map 实例地图指针
             */
            instance_uldaman_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
            {
                SetHeaders(DataHeader);
                memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));

                ironayaSealDoorTimer = 27000;  ///< 基石动画时间（27秒）
                keystoneCheck = false;
            }

            /**
             * @brief 检查是否有战斗正在进行
             * @return 如果有任何首领战斗正在进行则返回 true
             */
            bool IsEncounterInProgress() const override
            {
                for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
                    if (m_auiEncounter[i] == IN_PROGRESS)
                        return true;

                return false;
            }

            // ============ 生物 GUID 存储 ============
            ObjectGuid archaedasGUID;              ///< 阿扎达斯 GUID
            ObjectGuid ironayaGUID;                ///< 艾隆纳亚 GUID
            ObjectGuid whoWokeuiArchaedasGUID;     ///< 激活阿扎达斯的玩家 GUID

            // ============ 游戏对象 GUID 存储 ============
            ObjectGuid altarOfTheKeeperTempleDoor; ///< 守护者祭坛神殿门
            ObjectGuid archaedasTempleDoor;        ///< 阿扎达斯神殿门
            ObjectGuid ancientVaultDoor;           ///< 古代宝库门
            ObjectGuid ironayaSealDoor;            ///< 艾隆纳亚密封门

            ObjectGuid keystoneGUID;               ///< 基石 GUID

            // ============ 计时器和状态标志 ============
            uint32 ironayaSealDoorTimer;           ///< 艾隆纳亚密封门开启计时器
            bool keystoneCheck;                    ///< 基石激活检查标志

            // ============ 小怪 GUID 容器 ============
            GuidVector stoneKeepers;               ///< 石像守卫列表
            GuidVector altarOfTheKeeperCounts;     ///< 守护者祭坛计数器（未使用）
            GuidVector vaultWalkers;               ///< 宝库行者列表
            GuidVector earthenGuardians;           ///< 土灵守护者列表
            GuidVector archaedasWallMinions;       ///< 阿扎达斯墙边小怪列表

            uint32 m_auiEncounter[MAX_ENCOUNTER];  ///< 首领战斗状态数组
            std::string str_data;                  ///< 保存数据字符串

            /**
             * @brief 游戏对象创建时的处理
             * @param go 创建的游戏对象
             *
             * 根据游戏对象的类型存储其 GUID，并根据已完成的战斗状态
             * 设置门的开启状态。
             */
            void OnGameObjectCreate(GameObject* go) override
            {
                switch (go->GetEntry())
                {
                    case GO_ALTAR_OF_THE_KEEPER_TEMPLE_DOOR:         // 锁住门
                        altarOfTheKeeperTempleDoor = go->GetGUID();

                        if (m_auiEncounter[0] == DONE)
                           HandleGameObject(ObjectGuid::Empty, true, go);
                        break;

                    case GO_ARCHAEDAS_TEMPLE_DOOR:
                        archaedasTempleDoor = go->GetGUID();

                        if (m_auiEncounter[0] == DONE)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;

                    case GO_ANCIENT_VAULT_DOOR:
                        go->SetGoState(GO_STATE_READY);
                        go->ReplaceAllFlags(GO_FLAG_IN_USE | GO_FLAG_NODESPAWN);
                        ancientVaultDoor = go->GetGUID();

                        if (m_auiEncounter[1] == DONE)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;

                    case GO_IRONAYA_SEAL_DOOR:
                        ironayaSealDoor = go->GetGUID();

                        if (m_auiEncounter[2] == DONE)
                            HandleGameObject(ObjectGuid::Empty, true, go);
                        break;

                    case GO_KEYSTONE:
                        keystoneGUID = go->GetGUID();

                        if (m_auiEncounter[2] == DONE)
                        {
                            HandleGameObject(ObjectGuid::Empty, true, go);
                            go->SetFlag(GO_FLAG_INTERACT_COND);
                        }
                        break;
                }
            }

            /**
             * @brief 设置生物为冻结状态
             * @param creature 目标生物
             *
             * 将生物设置为初始冻结状态：
             * - 友好阵营
             * - 移除所有光环
             * - 不可交互标志
             * - 定身
             * - 添加冻结动画效果
             */
            void SetFrozenState(Creature* creature)
            {
                creature->SetFaction(FACTION_FRIENDLY);
                creature->RemoveAllAuras();
                creature->SetUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                creature->SetControlled(true, UNIT_STATE_ROOT);
                creature->AddAura(SPELL_MINION_FREEZE_ANIM, creature);
            }

            /**
             * @brief 设置门的状态
             * @param guid 门的 GUID
             * @param open true 为开启，false 为关闭
             */
            void SetDoor(ObjectGuid guid, bool open)
            {
                GameObject* go = instance->GetGameObject(guid);
                if (!go)
                    return;

                HandleGameObject(guid, open);
            }

            /**
             * @brief 阻止游戏对象被交互
             * @param guid 游戏对象的 GUID
             *
             * 添加交互条件标志，防止玩家重复使用。
             */
            void BlockGO(ObjectGuid guid)
            {
                GameObject* go = instance->GetGameObject(guid);
                if (!go)
                    return;

                go->SetFlag(GO_FLAG_INTERACT_COND);
            }

            /**
             * @brief 激活石像守卫
             *
             * 如果石像守卫事件尚未完成，逐一唤醒石像守卫。
             * 每次只唤醒第一个存活的守卫，守卫死亡后再唤醒下一个。
             * 当所有守卫都被击杀后，开启通往阿扎达斯的门。
             */
            void ActivateStoneKeepers()
            {
                if (GetData(DATA_ALTAR_DOORS) != DONE)
                {
                    for (GuidVector::const_iterator i = stoneKeepers.begin(); i != stoneKeepers.end(); ++i)
                    {
                        Creature* target = instance->GetCreature(*i);
                        if (!target || !target->IsAlive())
                            continue;
                        // 解除冻结状态
                        target->SetControlled(false, UNIT_STATE_ROOT);
                        target->SetFaction(FACTION_MONSTER);
                        target->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                        target->RemoveAura(SPELL_MINION_FREEZE_ANIM);

                        return;        // 只激活找到的第一个
                    }
                    // 如果执行到这里，说明所有四个守卫都已死亡，开启门
                    SetData(DATA_ALTAR_DOORS, DONE);
                    SetDoor(archaedasTempleDoor, true); // 同时开启下一扇门
                }
            }

            /**
             * @brief 激活墙边小怪
             *
             * 唤醒阿扎达斯房间周围的墙边小怪，每次只唤醒一个。
             * 这些小怪包括土灵守护者 (Earthen Custodian) 和土灵塑形者 (Earthen Hallshaper)。
             */
            void ActivateWallMinions()
            {
                Creature* archaedas = instance->GetCreature(archaedasGUID);
                if (!archaedas)
                    return;

                for (GuidVector::const_iterator i = archaedasWallMinions.begin(); i != archaedasWallMinions.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (!target || !target->IsAlive() || target->GetFaction() == FACTION_MONSTER)
                        continue;
                    // 解除冻结状态
                    target->SetControlled(false, UNIT_STATE_ROOT);
                    target->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    target->SetFaction(FACTION_MONSTER);
                    target->RemoveAura(SPELL_MINION_FREEZE_ANIM);
                    // 施放觉醒视觉效果
                    archaedas->CastSpell(target, SPELL_AWAKEN_VAULT_WALKER, true);
                    target->CastSpell(target, SPELL_ARCHAEDAS_AWAKEN, true);

                    return;        // 只激活找到的第一个
                }
            }

            /**
             * @brief 停用所有小怪
             *
             * 当阿扎达斯死亡时调用，消除所有已激活的小怪。
             * 包括墙边小怪、宝库行者和土灵守护者。
             */
            void DeActivateMinions()
            {
                // 首先消除已激活的墙边小怪
                for (GuidVector::const_iterator i = archaedasWallMinions.begin(); i != archaedasWallMinions.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (!target || target->isDead() || target->GetFaction() != FACTION_MONSTER)
                        continue;

                    target->DespawnOrUnsummon();
                }

                // 宝库行者
                for (GuidVector::const_iterator i = vaultWalkers.begin(); i != vaultWalkers.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (!target || target->isDead() || target->GetFaction() != FACTION_MONSTER)
                        continue;

                    target->DespawnOrUnsummon();
                }

                // 土灵守护者
                for (GuidVector::const_iterator i = earthenGuardians.begin(); i != earthenGuardians.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (!target || target->isDead() || target->GetFaction() != FACTION_MONSTER)
                        continue;

                    target->DespawnOrUnsummon();
                }
            }

            /**
             * @brief 激活阿扎达斯
             * @param target 激活祭坛的玩家 GUID
             *
             * 当玩家点击祭坛时调用，开始阿扎达斯的激活序列：
             * 1. 移除冻结效果
             * 2. 施放觉醒法术
             * 3. 设置阵营为泰坦阵营
             * 4. 记录激活玩家的 GUID
             */
            void ActivateArchaedas(ObjectGuid target)
            {
                Creature* archaedas = instance->GetCreature(archaedasGUID);
                if (!archaedas)
                    return;

                if (ObjectAccessor::GetUnit(*archaedas, target))
                {
                    archaedas->RemoveAura(SPELL_FREEZE_ANIM);
                    archaedas->CastSpell(archaedas, SPELL_ARCHAEDAS_AWAKEN, false);
                    archaedas->SetFaction(FACTION_TITAN);
                    archaedas->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);
                    whoWokeuiArchaedasGUID = target;
                }
            }

            /**
             * @brief 激活艾隆纳亚
             *
             * 当基石动画完成后调用，激活艾隆纳亚：
             * 1. 设置阵营为泰坦阵营
             * 2. 解除定身
             * 3. 移除不可交互标志
             * 4. 移动到战斗位置
             * 5. 喊话
             */
            void ActivateIronaya()
            {
                Creature* ironaya = instance->GetCreature(ironayaGUID);
                if (!ironaya)
                    return;

                ironaya->SetFaction(FACTION_TITAN);
                ironaya->SetControlled(false, UNIT_STATE_ROOT);
                ironaya->RemoveUnitFlag(UNIT_FLAG_UNINTERACTIBLE);

                // 清除当前移动并移动到战斗位置
                ironaya->GetMotionMaster()->Clear();
                ironaya->GetMotionMaster()->MovePoint(0, IronayaPoint);
                ironaya->SetHomePosition(IronayaPoint);

                ironaya->AI()->Talk(SAY_AGGRO);
            }

            /**
             * @brief 重生所有小怪
             *
             * 当阿扎达斯重置时调用，重生所有死亡的小怪并设置为冻结状态。
             */
            void RespawnMinions()
            {
                // 首先重生墙边小怪
                for (GuidVector::const_iterator i = archaedasWallMinions.begin(); i != archaedasWallMinions.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (target && target->isDead())
                    {
                        target->Respawn();
                        target->GetMotionMaster()->MoveTargetedHome();
                        SetFrozenState(target);
                    }
                }

                // 宝库行者
                for (GuidVector::const_iterator i = vaultWalkers.begin(); i != vaultWalkers.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (target && target->isDead())
                    {
                        target->Respawn();
                        target->GetMotionMaster()->MoveTargetedHome();
                        SetFrozenState(target);
                    }
                }

                // 土灵守护者
                for (GuidVector::const_iterator i = earthenGuardians.begin(); i != earthenGuardians.end(); ++i)
                {
                    Creature* target = instance->GetCreature(*i);
                    if (target && target->isDead())
                    {
                        target->Respawn();
                        target->GetMotionMaster()->MoveTargetedHome();
                        SetFrozenState(target);
                    }
                }
            }

            /**
             * @brief 更新实例状态
             * @param diff 自上次更新以来经过的时间（毫秒）
             *
             * 处理基石激活后的计时器逻辑：
             * - 等待27秒动画完成后激活艾隆纳亚
             * - 开启密封门
             * - 阻止基石再次被使用
             */
            void Update(uint32 diff) override
            {
                if (!keystoneCheck)
                    return;

                if (ironayaSealDoorTimer <= diff)
                {
                    ActivateIronaya();

                    SetDoor(ironayaSealDoor, true);
                    BlockGO(keystoneGUID);

                    SetData(DATA_IRONAYA_DOOR, DONE); // 保存状态
                    keystoneCheck = false;
                }
                else
                    ironayaSealDoorTimer -= diff;
            }

            /**
             * @brief 设置数据值
             * @param type 数据类型
             * @param data 数据值
             *
             * 根据数据类型执行相应的操作：
             * - DATA_ALTAR_DOORS: 控制守护者祭坛门的状态
             * - DATA_ANCIENT_DOOR: 控制古代宝库门（阿扎达斯死亡时开启）
             * - DATA_IRONAYA_DOOR: 控制艾隆纳亚密封门的状态
             * - DATA_STONE_KEEPERS: 激活下一个石像守卫
             * - DATA_MINIONS: 管理阿扎达斯的小怪状态
             * - DATA_IRONAYA_SEAL: 开始基石激活计时
             *
             * 当状态为 DONE 时，保存实例进度到数据库。
             */
            void SetData(uint32 type, uint32 data) override
            {
                switch (type)
                {
                    case DATA_ALTAR_DOORS:
                        m_auiEncounter[0] = data;
                        if (data == DONE)
                            SetDoor(altarOfTheKeeperTempleDoor, true);
                        break;

                    case DATA_ANCIENT_DOOR:
                        m_auiEncounter[1] = data;
                        if (data == DONE) // 阿扎达斯被击败
                        {
                            SetDoor(archaedasTempleDoor, true); // 重新开启入口门
                            SetDoor(ancientVaultDoor, true);
                        }
                        break;

                    case DATA_IRONAYA_DOOR:
                        m_auiEncounter[2] = data;
                        break;

                    case DATA_STONE_KEEPERS:
                        ActivateStoneKeepers();
                        break;

                    case DATA_MINIONS:
                        switch (data)
                        {
                            case NOT_STARTED:
                                if (m_auiEncounter[0] == DONE) // 如果玩家已开启门
                                    SetDoor(archaedasTempleDoor, true);

                                RespawnMinions();
                                break;

                            case IN_PROGRESS:
                                ActivateWallMinions();
                                break;

                            case SPECIAL:
                                DeActivateMinions();
                                break;
                        }
                        break;

                    case DATA_IRONAYA_SEAL:
                        keystoneCheck = true;
                        break;
                }

                if (data == DONE)
                {
                    OUT_SAVE_INST_DATA;

                    std::ostringstream saveStream;
                    saveStream << m_auiEncounter[0] << ' ' << m_auiEncounter[1] << ' ' << m_auiEncounter[2];

                    str_data = saveStream.str();

                    SaveToDB();
                    OUT_SAVE_INST_DATA_COMPLETE;
                }
            }

            /**
             * @brief 设置 GUID 数据
             * @param type 数据类型（0 = 阿扎达斯激活）
             * @param data 玩家的 GUID
             *
             * 当玩家激活阿扎达斯祭坛时调用：
             * - 激活阿扎达斯
             * - 关闭神殿门（阻止逃跑）
             */
            void SetGuidData(uint32 type, ObjectGuid data) override
            {
                // 阿扎达斯
                if (type == 0)
                {
                    ActivateArchaedas (data);
                    SetDoor(archaedasTempleDoor, false); // 事件开始时关闭门
                }
            }

            /**
             * @brief 获取保存数据
             * @return 序列化的实例进度数据
             */
            std::string GetSaveData() override
            {
                return str_data;
            }

            /**
             * @brief 加载保存的数据
             * @param in 序列化的实例进度数据
             *
             * 从数据库加载实例进度，恢复首领战斗状态。
             * 正在进行中的战斗会被重置为未开始。
             */
            void Load(char const* in) override
            {
                if (!in)
                {
                    OUT_LOAD_INST_DATA_FAIL;
                    return;
                }

                OUT_LOAD_INST_DATA(in);

                std::istringstream loadStream(in);
                loadStream >> m_auiEncounter[0] >> m_auiEncounter[1] >> m_auiEncounter[2];

                for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
                {
                    if (m_auiEncounter[i] == IN_PROGRESS)
                        m_auiEncounter[i] = NOT_STARTED;
                }

                OUT_LOAD_INST_DATA_COMPLETE;
            }

            /**
             * @brief 生物创建时的处理
             * @param creature 创建的生物
             *
             * 根据生物的 Entry 将其 GUID 存储到相应的容器中，
             * 并为需要冻结的生物设置冻结状态。
             */
            void OnCreatureCreate(Creature* creature) override
            {
                switch (creature->GetEntry())
                {
                    case 4857:    // 石像守卫 (Stone Keeper)
                        SetFrozenState (creature);
                        stoneKeepers.push_back(creature->GetGUID());
                        break;

                    case 7309:    // 土灵守护者 (Earthen Custodian)
                        archaedasWallMinions.push_back(creature->GetGUID());
                        break;

                    case 7077:    // 土灵塑形者 (Earthen Hallshaper)
                        archaedasWallMinions.push_back(creature->GetGUID());
                        break;

                    case 7076:    // 土灵守护者 (Earthen Guardian)
                        earthenGuardians.push_back(creature->GetGUID());
                        break;

                    case 7228:    // 艾隆纳亚 (Ironaya)
                        ironayaGUID = creature->GetGUID();

                        if (m_auiEncounter[2] != DONE)
                            SetFrozenState (creature);
                        break;

                    case 10120:    // 宝库行者 (Vault Walker)
                        vaultWalkers.push_back(creature->GetGUID());
                        break;

                    case 2748:    // 阿扎达斯 (Archaedas)
                        archaedasGUID = creature->GetGUID();
                        break;

                }
            }

            /**
             * @brief 获取 GUID 数据
             * @param identifier 标识符
             * @return 对应的 GUID
             *
             * 标识符映射：
             * - 0: 激活阿扎达斯的玩家 GUID
             * - 1-4: 宝库行者 1-4
             * - 5-10: 土灵守护者 1-6
             */
            ObjectGuid GetGuidData(uint32 identifier) const override
            {
                switch (identifier)
                {
                    case 0:
                        return whoWokeuiArchaedasGUID;
                    case 1:
                    case 2:
                    case 3:
                    case 4:
                        return vaultWalkers.at(identifier - 1);
                    case 5:
                    case 6:
                    case 7:
                    case 8:
                    case 9:
                    case 10:
                        return earthenGuardians.at(identifier - 5);
                    default:
                        break;
                }

                return ObjectGuid::Empty;
            } // end GetGuidData

            /**
             * @brief 处理游戏对象事件
             * @param gameObject 触发事件的游戏对象（未使用）
             * @param eventId 事件ID
             *
             * 处理守护者祭坛的激活事件，唤醒石像守卫。
             */
            void ProcessEvent(WorldObject* /*gameObject*/, uint32 eventId) override
            {
                switch (eventId)
                {
                    case EVENT_SUB_BOSS_AGGRO:
                        SetData(DATA_STONE_KEEPERS, IN_PROGRESS); // 激活石像守卫
                        break;
                    default:
                        break;
                }
            }
        };

        /**
         * @brief 获取实例脚本
         * @param map 实例地图指针
         * @return 新创建的实例脚本对象
         */
        InstanceScript* GetInstanceScript(InstanceMap* map) const override
        {
            return new instance_uldaman_InstanceMapScript(map);
        }
};

/**
 * @brief 脚本注册函数
 *
 * 此函数在脚本初始化时被调用一次。
 * 注册 Uldaman 实例脚本。
 */
void AddSC_instance_uldaman()
{
    new instance_uldaman();
}
