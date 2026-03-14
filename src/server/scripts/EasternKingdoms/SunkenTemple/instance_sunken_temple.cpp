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
 * @file    instance_sunken_temple.cpp
 * @brief   沉没的神庙副本实例脚本
 * @details 实现沉没的神庙副本的核心机制，包括：
 *          - Boss 击杀状态管理
 *          - 雕像谜题机制（需要按顺序激活 6 个雕像）
 *          - 精英巨魔事件（击杀 6 个精英巨魔解锁先知贾玛兰）
 *          - 力场屏障门控制
 *          - Boss 免疫状态管理
 *
 *          沉没的神庙副本地图 ID: 109
 *          主要 Boss：
 *          - 伊兰尼库斯之影（最终 Boss）
 *          - 先知贾玛兰
 *          - 哈卡的化身
 *          - 4 只绿龙（梦镰、织者、莫弗拉斯、哈扎斯）
 *          - 阿塔拉里恩（雕像谜题召唤）
 *
 * ScriptData
 * SDName: Instance_Sunken_Temple
 * SD%Complete: 100
 * SDComment: Place Holder
 * SDCategory: Sunken Temple
 * EndScriptData
 */

#include "ScriptMgr.h"
#include "Creature.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Map.h"
#include "sunken_temple.h"

/**
 * @brief 门数据配置表
 * @details 定义副本中门/屏障的状态与 Boss 事件的关联。
 *          当精英巨魔事件完成后，力场屏障将自动打开。
 */
static constexpr DoorData doorData[] =
{
    { GO_FORCEFIELD, BOSS_EVENT_ELITE_TROLLS, DOOR_TYPE_PASSAGE },  ///< 力场屏障，精英巨魔事件完成后开启
    { 0,             0,                       DOOR_TYPE_ROOM }      ///< 结束标记
};

/**
 * @brief 游戏对象数据映射表
 * @details 将游戏对象 ID 映射到实例脚本内部使用的标识符。
 *          用于快速查找和访问副本中的关键游戏对象。
 */
static constexpr ObjectData gameObjects[] =
{
    { GO_ATALAI_STATUE1, GO_ATALAI_STATUE1 },  ///< 雕像 1
    { GO_ATALAI_STATUE2, GO_ATALAI_STATUE2 },  ///< 雕像 2
    { GO_ATALAI_STATUE3, GO_ATALAI_STATUE3 },  ///< 雕像 3
    { GO_ATALAI_STATUE4, GO_ATALAI_STATUE4 },  ///< 雕像 4
    { GO_ATALAI_STATUE5, GO_ATALAI_STATUE5 },  ///< 雕像 5
    { GO_ATALAI_STATUE6, GO_ATALAI_STATUE6 },  ///< 雕像 6
    { 0,                 0 }                   ///< 结束标记
};

/// 阿塔拉里恩的生成位置（完成雕像谜题后召唤）
static Position const atalalarianPos = { -466.5134f, 95.19822f, -189.6463f, 0.03490658f };

/// 雕像总数
static uint8 const nStatues = 6;

/// 6 个雕像的位置坐标，用于生成光效
static Position const statuePositions[nStatues]
{
    { -515.553f,  95.25821f, -173.707f,  0.0f },  ///< 雕像 1 位置
    { -419.8487f, 94.48368f, -173.707f,  0.0f },  ///< 雕像 2 位置
    { -491.4003f, 135.9698f, -173.707f,  0.0f },  ///< 雕像 3 位置
    { -491.4909f, 53.48179f, -173.707f,  0.0f },  ///< 雕像 4 位置
    { -443.8549f, 136.1007f, -173.707f,  0.0f },  ///< 雕像 5 位置
    { -443.4171f, 53.83124f, -173.707f,  0.0f }   ///< 雕像 6 位置
};

/**
 * @class instance_sunken_temple
 * @brief 沉没的神庙副本地图脚本
 * @details 注册副本地图脚本，地图 ID 为 109。
 *          负责创建实例脚本实例。
 */
class instance_sunken_temple : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     * @details 注册脚本名称和地图 ID。
     *          STScriptName = "instance_sunken_temple"
     *          地图 ID 109 = 沉没的神庙
     */
    instance_sunken_temple() : InstanceMapScript(STScriptName, 109) { }

    /**
     * @brief 创建实例脚本
     * @param map 副本地图指针
     * @return 返回新创建的实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_sunken_temple_InstanceMapScript(map);
    }

    /**
     * @struct instance_sunken_temple_InstanceMapScript
     * @brief 沉没的神庙实例脚本实现
     * @details 管理副本的核心逻辑：
     *          - Boss 击杀状态追踪
     *          - 雕像谜题机制
     *          - 精英巨魔事件进度
     *          - 门/屏障控制
     *          - Boss 免疫状态管理
     */
    struct instance_sunken_temple_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         * @details 初始化副本状态：
         *          - 设置数据头标识
         *          - 设置 Boss 数量
         *          - 加载门数据
         *          - 加载游戏对象数据
         *          - 初始化雕像状态为全部未激活
         *          - 初始化精英巨魔击杀数为 0
         */
        instance_sunken_temple_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            SetBossNumber(MAX_ENCOUNTER);
            LoadDoorData(doorData);
            LoadObjectData(nullptr, gameObjects);
            State = 0;

            s1 = false;
            s2 = false;
            s3 = false;
            s4 = false;
            s5 = false;
            s6 = false;
            EliteTrollsKilled = 0;
        }

        ObjectGuid JammalAnTheProphetGUID;  ///< 先知贾玛兰的 GUID
        ObjectGuid ShadeOfEranikusGUID;     ///< 伊兰尼库斯之影的 GUID
        uint32 EliteTrollsKilled;           ///< 已击杀的精英巨魔数量（0-6）

        uint32 State;  ///< 当前雕像状态（用于谜题进度）

        bool s1;  ///< 雕像 1 是否已激活
        bool s2;  ///< 雕像 2 是否已激活
        bool s3;  ///< 雕像 3 是否已激活
        bool s4;  ///< 雕像 4 是否已激活
        bool s5;  ///< 雕像 5 是否已激活
        bool s6;  ///< 雕像 6 是否已激活

        /**
         * @brief 单位死亡事件处理
         * @param unit 死亡的单位指针
         * @details 当副本中有单位死亡时调用。
         *          处理以下逻辑：
         *          - Boss 死亡：更新对应的 Boss 状态为 DONE
         *          - 精英巨魔死亡：递增击杀计数，达到 6 个时解锁先知贾玛兰
         * @note 此函数由核心在单位死亡时自动调用
         */
        void OnUnitDeath(Unit* unit) override
        {
            switch (unit->GetEntry())
            {
                case NPC_AVATAR_OF_HAKKAR:      SetBossState(BOSS_AVATAR_OF_HAKKAR, DONE); break;      // 哈卡的化身
                case NPC_JAMMALAN_THE_PROPHET:  SetBossState(BOSS_JAMMALAN_THE_PROPHET, DONE); break;  // 先知贾玛兰
                case NPC_DREAMSCYTHE:           SetBossState(BOSS_DREAMSCYTHE, DONE); break;           // 梦镰
                case NPC_WEAVER:                SetBossState(BOSS_WEAVER, DONE); break;                // 织者
                case NPC_MORPHAZ:               SetBossState(BOSS_MORPHAZ, DONE); break;               // 莫弗拉斯
                case NPC_HAZZAS:                SetBossState(BOSS_HAZZAS, DONE); break;                // 哈扎斯
                case NPC_SHADE_OF_ERANIKUS:     SetBossState(BOSS_SHADE_OF_ERANIKUS, DONE); break;     // 伊兰尼库斯之影
                case NPC_ATALALARION:           SetBossState(BOSS_ATALALARION, DONE); break;           // 阿塔拉里恩
                case NPC_ZOLO:                  // 精英巨魔佐洛
                case NPC_GASHER:                // 精英巨魔加什尔
                case NPC_LORO:                  // 精英巨魔洛罗
                case NPC_HUKKU:                 // 精英巨魔胡库
                case NPC_ZUL_LOR:               // 精英巨魔祖尔洛
                case NPC_MIJAN:                 // 精英巨魔米詹
                    SetData(BOSS_EVENT_ELITE_TROLLS, EliteTrollsKilled + 1);  // 增加精英巨魔击杀数
                    break;
                default:                        break;  // 其他单位不处理
            }
        }

        /**
         * @brief 生物创建事件处理
         * @param creature 新创建的生物指针
         * @details 当副本中创建生物时调用。
         *          处理以下逻辑：
         *          - 先知贾玛兰：保存 GUID，如果精英巨魔事件未完成则设置为免疫并施放绿色引导法术
         *          - 伊兰尼库斯之影：保存 GUID，如果先知贾玛兰未击杀则设置为免疫
         * @note 此函数由核心在生物创建时自动调用
         */
        void OnCreatureCreate(Creature* creature) override
        {
            InstanceScript::OnCreatureCreate(creature);

            switch (creature->GetEntry())
            {
                case NPC_JAMMALAN_THE_PROPHET:
                    // 保存先知贾玛兰的 GUID
                    JammalAnTheProphetGUID = creature->GetGUID();
                    // 如果精英巨魔事件未完成，贾玛兰处于免疫状态并施放引导法术
                    if (GetBossState(BOSS_EVENT_ELITE_TROLLS) != DONE)
                    {
                        creature->SetImmuneToPC(true);                    // 对玩家免疫
                        creature->CastSpell(creature, SPELL_GREEN_CHANNELING);  // 施放绿色引导视觉效果
                    }
                    break;
                case NPC_SHADE_OF_ERANIKUS:
                    // 保存伊兰尼库斯之影的 GUID
                    ShadeOfEranikusGUID = creature->GetGUID();
                    // 如果先知贾玛兰未击杀，伊兰尼库斯之影处于完全免疫状态
                    if (GetBossState(BOSS_JAMMALAN_THE_PROPHET) != DONE)
                        creature->SetImmuneToAll(true);  // 对所有伤害和效果免疫
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 更新雕像谜题状态
         * @param diff 距离上次更新的时间间隔（毫秒），本脚本中未使用
         * @details 每帧调用，处理雕像谜题的激活逻辑。
         *          雕像必须按照特定顺序激活（1->2->3->4->5->6）。
         *          每次只能激活一个雕像，且必须在前一个雕像激活后才能激活下一个。
         *          State 变量由 go_atalai_statue 脚本设置，表示玩家点击了哪个雕像。
         *
         * 激活顺序验证：
         * - 雕像 1：所有雕像都未激活时可激活
         * - 雕像 2：雕像 1 已激活，其他未激活时可激活
         * - 雕像 3：雕像 1、2 已激活，其他未激活时可激活
         * - 雕像 4：雕像 1、2、3 已激活，其他未激活时可激活
         * - 雕像 5：雕像 1、2、3、4 已激活，其他未激活时可激活
         * - 雕像 6：雕像 1、2、3、4、5 已激活，雕像 6 未激活时可激活
         *          激活雕像 6 后，将召唤 Boss 阿塔拉里恩
         * @note 此函数由核心每帧自动调用
         */
         virtual void Update(uint32 /*diff*/) override // correct order goes form 1-6
         {
             switch (State)
             {
             case GO_ATALAI_STATUE1:
                // 雕像 1：要求所有雕像都未激活
                if (!s1 && !s2 && !s3 && !s4 && !s5 && !s6)
                {
                    if (GameObject* pAtalaiStatue1 = GetGameObject(GO_ATALAI_STATUE1))
                        UseStatue(pAtalaiStatue1);
                    s1 = true;        // 标记雕像 1 已激活
                    State = 0;        // 重置状态，等待下一个雕像
                }
                break;
             case GO_ATALAI_STATUE2:
                // 雕像 2：要求雕像 1 已激活，其他未激活
                if (s1 && !s2 && !s3 && !s4 && !s5 && !s6)
                {
                    if (GameObject* pAtalaiStatue2 = GetGameObject(GO_ATALAI_STATUE2))
                        UseStatue(pAtalaiStatue2);
                    s2 = true;
                    State = 0;
                }
                break;
             case GO_ATALAI_STATUE3:
                // 雕像 3：要求雕像 1、2 已激活，其他未激活
                if (s1 && s2 && !s3 && !s4 && !s5 && !s6)
                {
                    if (GameObject* pAtalaiStatue3 = GetGameObject(GO_ATALAI_STATUE3))
                        UseStatue(pAtalaiStatue3);
                    s3 = true;
                    State = 0;
                }
                break;
             case GO_ATALAI_STATUE4:
                // 雕像 4：要求雕像 1、2、3 已激活，其他未激活
                if (s1 && s2 && s3 && !s4 && !s5 && !s6)
                {
                    if (GameObject* pAtalaiStatue4 = GetGameObject(GO_ATALAI_STATUE4))
                        UseStatue(pAtalaiStatue4);
                    s4 = true;
                    State = 0;
                }
                break;
             case GO_ATALAI_STATUE5:
                // 雕像 5：要求雕像 1、2、3、4 已激活，其他未激活
                if (s1 && s2 && s3 && s4 && !s5 && !s6)
                {
                    if (GameObject* pAtalaiStatue5 = GetGameObject(GO_ATALAI_STATUE5))
                        UseStatue(pAtalaiStatue5);
                    s5 = true;
                    State = 0;
                }
                break;
             case GO_ATALAI_STATUE6:
                // 雕像 6：要求雕像 1、2、3、4、5 已激活，雕像 6 未激活
                if (s1 && s2 && s3 && s4 && s5 && !s6)
                {
                    if (GameObject* pAtalaiStatue6 = GetGameObject(GO_ATALAI_STATUE6))
                    {
                        UseStatue(pAtalaiStatue6);        // 激活雕像
                        UseLastStatue(pAtalaiStatue6);    // 召唤 Boss
                    }
                    s6 = true;
                    State = 0;
                }
                break;
             }
         }

        /**
         * @brief 激活雕像
         * @param go 要激活的雕像游戏对象指针
         * @details 当雕像按正确顺序激活时调用。
         *          执行以下操作：
         *          1. 在雕像位置生成光效（GO_ATALAI_LIGHT1）
         *          2. 设置雕像为已交互状态（不可再次点击）
         */
        void UseStatue(GameObject* go)
        {
            go->SummonGameObject(GO_ATALAI_LIGHT1, *go, QuaternionData(), 0s);  // 生成激活光效
            go->SetFlag(GO_FLAG_INTERACT_COND);  // 设置为已交互，防止重复激活
        }

        /**
         * @brief 激活最后一个雕像并召唤 Boss
         * @param go 最后一个雕像（雕像 6）的游戏对象指针
         * @details 当雕像 6 激活时调用，完成雕像谜题。
         *          执行以下操作：
         *          1. 在所有 6 个雕像位置生成光效（GO_ATALAI_LIGHT2）
         *          2. 召唤 Boss 阿塔拉里恩
         * @note 阿塔拉里恩是雕像谜题的奖励 Boss
         */
        void UseLastStatue(GameObject* go)
        {
            // 在所有雕像位置生成最终光效
            for (uint8 i = 0; i < nStatues; ++i)
                go->SummonGameObject(GO_ATALAI_LIGHT2, statuePositions[i], QuaternionData(), 0s);

            // 召唤阿塔拉里恩，尸体在 10 分钟后消失
            go->SummonCreature(NPC_ATALALARION, atalalarianPos, TEMPSUMMON_CORPSE_TIMED_DESPAWN, 10min);
        }

        /**
         * @brief 设置 Boss 状态
         * @param type  Boss 遭遇战类型（ID）
         * @param state Boss 状态（NOT_STARTED、IN_PROGRESS、DONE 等）
         * @return true 如果设置成功，false 如果设置失败
         * @details 重写父类方法，在 Boss 状态改变时执行额外逻辑。
         *          特殊处理：
         *          - 先知贾玛兰被击杀：解除伊兰尼库斯之影的免疫状态
         * @note 此函数由核心在 Boss 状态改变时自动调用
         */
        bool SetBossState(uint32 type, EncounterState state) override
        {
            // 调用父类方法，如果失败则直接返回
            if (!InstanceScript::SetBossState(type, state))
                return false;

            switch (type)
            {
                case BOSS_JAMMALAN_THE_PROPHET:
                    // 先知贾玛兰被击杀后，解除伊兰尼库斯之影的免疫
                    if (state == DONE)
                        if (Creature* creature = instance->GetCreature(ShadeOfEranikusGUID))
                            creature->SetImmuneToAll(false);  // 解除所有免疫，使 Boss 可被攻击
                    break;
                default:
                    break;
            }
            return true;
        }

        /**
         * @brief 设置自定义数据
         * @param type 数据类型标识
         * @param data 数据值
         * @details 处理副本中的自定义数据设置请求。
         *          支持的数据类型：
         *          - EVENT_STATE：雕像状态，用于触发雕像谜题进度
         *          - BOSS_EVENT_ELITE_TROLLS：精英巨魔击杀数更新
         *
         * 精英巨魔事件逻辑：
         * - 当击杀数达到 6 时，解除先知贾玛兰的免疫状态
         * - 标记精英巨魔事件为完成
         * - 保存副本进度到数据库
         */
         void SetData(uint32 type, uint32 data) override
         {
            switch (type)
            {
                case EVENT_STATE:
                    // 设置雕像状态，触发 Update 函数中的雕像激活逻辑
                    State = data;
                    break;
                case BOSS_EVENT_ELITE_TROLLS:
                    // 更新精英巨魔击杀数
                    EliteTrollsKilled = data;
                    // 当击杀数达到 6 时，解锁先知贾玛兰
                    if (EliteTrollsKilled == 6)
                    {
                        if (Creature* jammal = instance->GetCreature(JammalAnTheProphetGUID))
                            jammal->SetImmuneToPC(false);  // 解除对玩家的免疫，贾玛兰变为可攻击
                        SetBossState(BOSS_EVENT_ELITE_TROLLS, DONE);  // 标记事件完成
                    }
                    SaveToDB();  // 保存进度，防止副本重置后丢失
                    break;
                default:
                    break;
            }
         }

        /**
         * @brief 获取自定义数据
         * @param type 数据类型标识
         * @return 对应的数据值，如果类型不存在则返回 0
         * @details 查询副本中的自定义数据。
         *          支持的数据类型：
         *          - EVENT_STATE：当前雕像状态
         *          - BOSS_EVENT_ELITE_TROLLS：精英巨魔击杀数
         */
         uint32 GetData(uint32 type) const override
         {
            switch (type)
            {
                case EVENT_STATE:
                    return State;               // 返回当前雕像状态
                case BOSS_EVENT_ELITE_TROLLS:
                    return EliteTrollsKilled;   // 返回精英巨魔击杀数
                default:
                    break;
            }
            return 0;  // 未知类型返回 0
         }

        /**
         * @brief 从存档数据中读取额外数据
         * @param data 输入字符串流，包含从数据库加载的副本进度数据
         * @details 重写父类方法，读取副本的额外保存数据。
         *          本副本保存精英巨魔击杀数，确保副本重载后进度不丢失。
         * @note 此函数由核心在加载副本存档时自动调用
         */
        void ReadSaveDataMore(std::istringstream& data) override
        {
            data >> EliteTrollsKilled;  // 读取精英巨魔击杀数
        }

        /**
         * @brief 写入额外数据到存档
         * @param data 输出字符串流，用于写入副本进度数据
         * @details 重写父类方法，写入副本的额外保存数据。
         *          本副本保存精英巨魔击杀数，确保副本重载后进度不丢失。
         * @note 此函数由核心在保存副本进度时自动调用
         */
        void WriteSaveDataMore(std::ostringstream& data) override
        {
            data << EliteTrollsKilled;  // 写入精英巨魔击杀数
        }
    };
};

/**
 * @brief 注册沉没的神庙副本实例脚本
 * @details 此函数由脚本加载器在服务器启动时调用。
 *          创建并注册 instance_sunken_temple 实例。
 */
void AddSC_instance_sunken_temple()
{
    new instance_sunken_temple();
}
