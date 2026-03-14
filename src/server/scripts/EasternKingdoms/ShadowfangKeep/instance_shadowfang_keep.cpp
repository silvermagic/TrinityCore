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
 * @file instance_shadowfang_keep.cpp
 * @brief 影牙城堡副本实例脚本
 *
 * 该模块实现影牙城堡副本的实例管理逻辑，主要功能包括：
 * - 管理副本内Boss战斗状态和进度
 * - 控制副本内门禁系统（庭院门、巫师门、阿鲁高门）
 * - 处理NPC Ash和Ada的剧情对话
 * - 管理阿鲁高的召唤事件序列
 * - 存档和加载副本进度数据
 *
 * @note 影牙城堡是位于银松森林的低级副本，最终Boss为大法师阿鲁高
 */

/* ScriptData
SDName: Instance_Shadowfang_Keep
SD%Complete: 90
SDComment:
SDCategory: Shadowfang Keep
EndScriptData */

#include "ScriptMgr.h"
#include "shadowfang_keep.h"
#include "GameObject.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "ScriptedCreature.h"
#include "TemporarySummon.h"

/**
 * @brief 副本内战斗事件的最大数量
 * 影牙城堡包含4个主要战斗事件
 */
#define MAX_ENCOUNTER              4

/**
 * @brief NPC对话文本枚举
 *
 * 定义副本内NPC的各种喊话文本ID
 */
enum Yells
{
    SAY_BOSS_DIE_AD         = 4,    ///< Ada在Boss死亡时的对话
    SAY_BOSS_DIE_AS         = 3,    ///< Ash在Boss死亡时的对话
    SAY_ARCHMAGE            = 0     ///< 大法师阿鲁高的对话
};

/**
 * @brief 法术ID枚举
 *
 * 定义副本内使用的各种法术ID
 */
enum Spells
{
    SPELL_ASHCROMBE_TELEPORT    = 15742,    ///< Ashcrombe传送法术 - 用于阿鲁高召唤剧情
    SPELL_SUMMON_VALENTINE_ADD  = 68610     ///< 召唤情人节小怪 - 情人节事件相关
};

/**
 * @brief 虚空行者召唤位置数组
 *
 * 定义阿鲁高在Fenrus战斗后召唤虚空行者的位置坐标
 * 位置0-3：四个虚空行者的生成点
 * 位置4：大法师阿鲁高的投影出现位置
 */
const Position SpawnLocation[] =
{
    {-148.199f, 2165.647f, 128.448f, 1.026f},   ///< 虚空行者生成点1
    {-153.110f, 2168.620f, 128.448f, 1.026f},   ///< 虚空行者生成点2
    {-145.905f, 2180.520f, 128.448f, 4.183f},   ///< 虚空行者生成点3
    {-140.794f, 2178.037f, 128.448f, 4.090f},   ///< 虚空行者生成点4
    {-138.640f, 2170.159f, 136.577f, 2.737f}    ///< 大法师阿鲁高投影位置
};

/**
 * @class instance_shadowfang_keep
 * @brief 影牙城堡副本脚本主类
 *
 * 继承自InstanceMapScript，负责创建和管理副本实例脚本
 * 地图ID：33（影牙城堡）
 */
class instance_shadowfang_keep : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 初始化副本脚本，注册脚本名称和地图ID
     */
    instance_shadowfang_keep() : InstanceMapScript(SFKScriptName, 33) { }

    /**
     * @brief 创建实例脚本对象
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     *
     * 当副本被创建时调用，返回实际的实例管理脚本
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_shadowfang_keep_InstanceMapScript(map);
    }

    /**
     * @struct instance_shadowfang_keep_InstanceMapScript
     * @brief 影牙城堡实例管理脚本
     *
     * 继承自InstanceScript，实现副本的核心管理逻辑：
     * - 追踪Boss战斗状态
     * - 管理门禁系统
     * - 处理剧情事件
     * - 保存/加载副本进度
     */
    struct instance_shadowfang_keep_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数
         * @param map 副本地图指针
         *
         * 初始化实例脚本，设置数据头，清零战斗状态数组
         */
        instance_shadowfang_keep_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));

            uiPhase = 0;
            uiTimer = 0;
        }

        uint32 m_auiEncounter[MAX_ENCOUNTER];   ///< 战斗状态数组，存储4个Boss的战斗进度
        std::string str_data;                   ///< 副本存档数据字符串

        ObjectGuid uiAshGUID;                   ///< NPC Ash的GUID（囚犯NPC）
        ObjectGuid uiAdaGUID;                   ///< NPC Ada的GUID（囚犯NPC）
        ObjectGuid uiArchmageArugalGUID;        ///< 大法师阿鲁高的GUID（最终Boss）

        ObjectGuid DoorCourtyardGUID;           ///< 庭院门的GUID
        ObjectGuid DoorSorcererGUID;            ///< 巫师门的GUID
        ObjectGuid DoorArugalGUID;              ///< 阿鲁高门的GUID

        uint8 uiPhase;                          ///< 剧情事件阶段（用于Fenrus死后的召唤序列）
        uint16 uiTimer;                         ///< 剧情事件计时器（毫秒）

        /**
         * @brief 生物创建时的回调函数
         * @param creature 新创建的生物指针
         *
         * 当副本内的生物被创建时调用，用于记录关键NPC的GUID
         * 以便后续在脚本中引用这些NPC
         *
         * @note 该函数会在生物刷新时自动调用
         */
        void OnCreatureCreate(Creature* creature) override
        {
            switch (creature->GetEntry())
            {
                case NPC_ASH:
                    uiAshGUID = creature->GetGUID();    // 记录囚犯Ash的GUID
                    break;
                case NPC_ADA:
                    uiAdaGUID = creature->GetGUID();    // 记录囚犯Ada的GUID
                    break;
                case NPC_ARCHMAGE_ARUGAL:
                    uiArchmageArugalGUID = creature->GetGUID(); // 记录最终Boss阿鲁高的GUID
                    break;
                case NPC_DND_CRAZED_APOTHECARY_GENERATOR:
                    // 情人节事件：记录疯狂药剂师生成器的GUID
                    _crazedApothecaryGeneratorGUIDs.push_back(creature->GetGUID());
                    break;
                default:
                    break;
            }
        }

        /**
         * @brief 游戏对象创建时的回调函数
         * @param go 新创建的游戏对象指针
         *
         * 当副本内的游戏对象被创建时调用，主要用于：
         * - 记录门禁系统的GUID
         * - 根据副本进度自动打开已通关的门
         *
         * @note 门的状态基于对应的Boss战斗状态决定
         */
        void OnGameObjectCreate(GameObject* go) override
        {
            switch (go->GetEntry())
            {
                case GO_COURTYARD_DOOR:
                    DoorCourtyardGUID = go->GetGUID();  // 记录庭院门GUID
                    // 如果囚犯已释放，则打开庭院门
                    if (m_auiEncounter[0] == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                case GO_SORCERER_DOOR:
                    DoorSorcererGUID = go->GetGUID();   // 记录巫师门GUID
                    // 如果Fenrus已击杀，则打开巫师门
                    if (m_auiEncounter[2] == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
                case GO_ARUGAL_DOOR:
                    DoorArugalGUID = go->GetGUID();     // 记录阿鲁高门GUID
                    // 如果Nandos已击杀，则打开通往阿鲁高的门
                    if (m_auiEncounter[3] == DONE)
                        HandleGameObject(ObjectGuid::Empty, true, go);
                    break;
            }
        }

        /**
         * @brief 执行NPC对话
         *
         * 当Rethilgore被击杀后，让Ada和Ash两个NPC发表对话
         * 这是副本剧情的一部分，表示囚犯获救后的感谢
         *
         * @note 只有当两个NPC都存活时才会触发对话
         */
        void DoSpeech()
        {
            Creature* pAda = instance->GetCreature(uiAdaGUID);
            Creature* pAsh = instance->GetCreature(uiAshGUID);

            // 检查两个NPC是否存在且存活
            if (pAda && pAda->IsAlive() && pAsh && pAsh->IsAlive())
            {
                pAda->AI()->Talk(SAY_BOSS_DIE_AD);  // Ada发言
                pAsh->AI()->Talk(SAY_BOSS_DIE_AS);  // Ash发言
            }
        }

        /**
         * @brief 设置副本数据
         * @param type 数据类型（战斗事件类型）
         * @param data 数据值（战斗状态：NOT_STARTED、IN_PROGRESS、DONE等）
         *
         * 当Boss战斗状态改变时调用，用于：
         * - 更新副本进度状态
         * - 触发相关门禁开关
         * - 触发剧情事件
         * - 保存副本进度到数据库
         *
         * @note 当状态设置为DONE时会自动保存副本进度
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case TYPE_FREE_NPC:
                    // 囚犯释放事件：完成后打开庭院门
                    if (data == DONE)
                        DoUseDoorOrButton(DoorCourtyardGUID);
                    m_auiEncounter[0] = data;
                    break;
                case TYPE_RETHILGORE:
                    // Rethilgore（第一个Boss）击杀：触发囚犯对话
                    if (data == DONE)
                        DoSpeech();
                    m_auiEncounter[1] = data;
                    break;
                case TYPE_FENRUS:
                    // Fenrus（第二个Boss）击杀：启动召唤虚空行者的剧情序列
                    switch (data)
                    {
                        case DONE:
                            uiTimer = 1000;  // 设置1秒后开始剧情
                            uiPhase = 1;     // 进入剧情第1阶段
                            break;
                        case 7:
                            // 特殊值7：所有虚空行者被击杀后打开巫师门
                            DoUseDoorOrButton(DoorSorcererGUID);
                            break;
                    }
                    m_auiEncounter[2] = data;
                    break;
                case TYPE_NANDOS:
                    // Nandos（第三个Boss）击杀：打开通往阿鲁高的门
                    if (data == DONE)
                        DoUseDoorOrButton(DoorArugalGUID);
                    m_auiEncounter[3] = data;
                    break;
                case DATA_SPAWN_VALENTINE_ADDS:
                    // 情人节事件：召唤疯狂药剂师
                    for (ObjectGuid guid : _crazedApothecaryGeneratorGUIDs)
                    {
                        if (Creature* generator = instance->GetCreature(guid))
                            generator->CastSpell(nullptr, SPELL_SUMMON_VALENTINE_ADD);
                    }
                    break;
                default:
                    break;
            }

            // 当任何Boss被击杀时，保存副本进度到数据库
            if (data == DONE)
            {
                OUT_SAVE_INST_DATA;

                // 将4个Boss的状态序列化为字符串
                std::ostringstream saveStream;
                saveStream << m_auiEncounter[0] << ' ' << m_auiEncounter[1] << ' ' << m_auiEncounter[2] << ' ' << m_auiEncounter[3];

                str_data = saveStream.str();

                SaveToDB();
                OUT_SAVE_INST_DATA_COMPLETE;
            }
        }

        /**
         * @brief 获取副本数据
         * @param type 数据类型（战斗事件类型）
         * @return 对应的战斗状态值
         *
         * 查询指定Boss或事件的当前状态
         * 用于其他脚本判断副本进度
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case TYPE_FREE_NPC:
                    return m_auiEncounter[0];   // 返回囚犯释放状态
                case TYPE_RETHILGORE:
                    return m_auiEncounter[1];   // 返回Rethilgore状态
                case TYPE_FENRUS:
                    return m_auiEncounter[2];   // 返回Fenrus状态
                case TYPE_NANDOS:
                    return m_auiEncounter[3];   // 返回Nandos状态
            }
            return 0;
        }

        /**
         * @brief 获取存档数据
         * @return 序列化的副本进度字符串
         *
         * 返回用于保存到数据库的副本进度数据
         * 格式："状态0 状态1 状态2 状态3"
         */
        std::string GetSaveData() override
        {
            return str_data;
        }

        /**
         * @brief 加载存档数据
         * @param in 序列化的副本进度字符串
         *
         * 从数据库加载副本进度，在副本创建时调用
         * 会自动将进行中的战斗重置为未开始状态
         *
         * @note 如果输入数据为空，会记录错误日志
         */
        void Load(char const* in) override
        {
            if (!in)
            {
                OUT_LOAD_INST_DATA_FAIL;
                return;
            }

            OUT_LOAD_INST_DATA(in);

            // 解析字符串，恢复4个Boss的状态
            std::istringstream loadStream(in);
            loadStream >> m_auiEncounter[0] >> m_auiEncounter[1] >> m_auiEncounter[2] >> m_auiEncounter[3];

            // 将进行中的战斗重置为未开始（避免副本重置后Boss卡住）
            for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
            {
                if (m_auiEncounter[i] == IN_PROGRESS)
                    m_auiEncounter[i] = NOT_STARTED;
            }

            OUT_LOAD_INST_DATA_COMPLETE;
        }

        /**
         * @brief 副本更新函数
         * @param uiDiff 距离上次更新的时间间隔（毫秒）
         *
         * 每个游戏循环tick调用一次，用于处理：
         * - Fenrus死后的阿鲁高召唤剧情
         * - 管理剧情事件的计时器
         *
         * @note 只有当Fenrus被击杀后才会执行剧情逻辑
         * @性能注意事项 该函数每个tick都会调用，应避免复杂计算
         */
        void Update(uint32 uiDiff) override
        {
            // 只有当Fenrus被击杀后才执行后续剧情
            if (GetData(TYPE_FENRUS) != DONE)
                return;

            Creature* pArchmage = instance->GetCreature(uiArchmageArugalGUID);

            // 检查阿鲁高是否存在且存活
            if (!pArchmage || !pArchmage->IsAlive())
                return;

            // 处理剧情事件序列
            if (uiPhase)
            {
                if (uiTimer <= uiDiff)
                {
                    switch (uiPhase)
                    {
                        case 1:
                        {
                            // 第1阶段：阿鲁高投影出现并发表讲话
                            // 在指定位置召唤阿鲁高的投影（临时存在10秒）
                            Creature* summon = pArchmage->SummonCreature(pArchmage->GetEntry(), SpawnLocation[4], TEMPSUMMON_TIMED_DESPAWN, 10s);
                            summon->SetImmuneToPC(true);              // 设置对玩家免疫（投影不可攻击）
                            summon->SetReactState(REACT_DEFENSIVE);    // 设置防御反应状态
                            summon->CastSpell(summon, SPELL_ASHCROMBE_TELEPORT, true);  // 施放传送视觉效果
                            summon->AI()->Talk(SAY_ARCHMAGE);         // 阿鲁高发表讲话
                            uiTimer = 2000;   // 2秒后进入下一阶段
                            uiPhase = 2;
                            break;
                        }
                        case 2:
                        {
                            // 第2阶段：召唤4个虚空行者
                            // 这些虚空行者是Fenrus房间后的小怪，需要击杀才能开门
                            pArchmage->SummonCreature(NPC_ARUGAL_VOIDWALKER, SpawnLocation[0], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1min);
                            pArchmage->SummonCreature(NPC_ARUGAL_VOIDWALKER, SpawnLocation[1], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1min);
                            pArchmage->SummonCreature(NPC_ARUGAL_VOIDWALKER, SpawnLocation[2], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1min);
                            pArchmage->SummonCreature(NPC_ARUGAL_VOIDWALKER, SpawnLocation[3], TEMPSUMMON_CORPSE_TIMED_DESPAWN, 1min);
                            uiPhase = 0;  // 剧情结束
                            break;
                        }

                    }
                } else uiTimer -= uiDiff;  // 计时器递减
            }
        }

    private:
        GuidVector _crazedApothecaryGeneratorGUIDs;  ///< 情人节事件药剂师生成器GUID列表
    };

};

/**
 * @brief 注册影牙城堡副本脚本
 *
 * 该函数在服务器启动时被调用，用于注册副本脚本到脚本系统
 * 每个副本脚本都需要通过此方式注册才能生效
 */
void AddSC_instance_shadowfang_keep()
{
    new instance_shadowfang_keep();
}
