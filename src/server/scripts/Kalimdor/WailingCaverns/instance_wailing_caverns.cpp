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
 * @file instance_wailing_caverns.cpp
 * @brief 哀嚎洞穴副本实例脚本
 *
 * 该模块实现哀嚎洞穴副本的实例管理功能,包括:
 * - Boss击杀状态跟踪(4个芬里斯领主)
 * - 纳雷克斯唤醒事件的多阶段管理
 * - 穆塔努斯Boss战状态管理
 * - 副本进度的保存和加载
 *
 * 副本流程:
 * 1. 玩家需要击杀4个芬里斯领主(Lord Cobrahn, Lord Pythas, Lady Anacondra, Lord Serpentis)
 * 2. 击杀所有领主后,可以与纳雷克斯的弟子对话启动唤醒仪式
 * 3. 唤醒仪式分为3个阶段,每阶段召唤不同的怪物波次
 * 4. 最后召唤Boss穆塔努斯
 */

/* ScriptData
SDName: Instance_Wailing_Caverns
SD%Complete: 99
SDComment: Everything seems to work, still need some checking
SDCategory: Wailing Caverns
EndScriptData */

#include "ScriptMgr.h"
#include "Creature.h"
#include "InstanceScript.h"
#include "Log.h"
#include "Map.h"
#include "wailing_caverns.h"

/// 最大遭遇战数量(9个Boss/事件)
#define MAX_ENCOUNTER   9

/**
 * @class instance_wailing_caverns
 * @brief 哀嚎洞穴副本实例脚本主类
 *
 * 继承自 InstanceMapScript,为副本地图提供实例级别的脚本支持
 */
class instance_wailing_caverns : public InstanceMapScript
{
public:
    /**
     * @brief 构造函数
     *
     * 注册脚本名称和地图ID(43为哀嚎洞穴)
     */
    instance_wailing_caverns() : InstanceMapScript(WCScriptName, 43) { }

    /**
     * @brief 创建实例脚本对象
     * @param map 副本地图指针
     * @return 新创建的实例脚本对象
     */
    InstanceScript* GetInstanceScript(InstanceMap* map) const override
    {
        return new instance_wailing_caverns_InstanceMapScript(map);
    }

    /**
     * @class instance_wailing_caverns_InstanceMapScript
     * @brief 哀嚎洞穴实例脚本实现类
     *
     * 管理副本内的所有Boss状态和事件进度
     */
    struct instance_wailing_caverns_InstanceMapScript : public InstanceScript
    {
        /**
         * @brief 构造函数,初始化所有状态
         * @param map 副本地图指针
         */
        instance_wailing_caverns_InstanceMapScript(InstanceMap* map) : InstanceScript(map)
        {
            SetHeaders(DataHeader);
            memset(&m_auiEncounter, 0, sizeof(m_auiEncounter));

            yelled = false;
        }

        /// 遭遇战状态数组,存储所有Boss和事件的进度
        uint32 m_auiEncounter[MAX_ENCOUNTER];

        /// 纳雷克斯是否已经喊话(避免重复触发)
        bool yelled;

        /// 纳雷克斯NPC的GUID
        ObjectGuid NaralexGUID;

        /**
         * @brief 生物创建时的回调
         * @param creature 新创建的生物指针
         *
         * 当副本创建生物时调用,用于记录重要NPC的GUID
         * 只记录纳雷克斯的GUID,供后续事件使用
         */
        void OnCreatureCreate(Creature* creature) override
        {
            if (creature->GetEntry() == DATA_NARALEX)
                NaralexGUID = creature->GetGUID();
        }

        /**
         * @brief 设置遭遇战状态
         * @param type 遭遇战类型(Boss/事件ID)
         * @param data 状态数据(NOT_STARTED, IN_PROGRESS, DONE, FAIL)
         *
         * 当Boss战状态改变时调用,更新内部状态数组
         * 当状态为DONE时自动保存到数据库
         *
         * 遭遇战类型:
         * - TYPE_LORD_COBRAHN: 考布拉领主
         * - TYPE_LORD_PYTHAS: 皮萨斯领主
         * - TYPE_LADY_ANACONDRA: 安娜科德拉女士
         * - TYPE_LORD_SERPENTIS: 瑟芬提斯领主
         * - TYPE_NARALEX_EVENT: 纳雷克斯事件总状态
         * - TYPE_NARALEX_PART1/2/3: 纳雷克斯事件三个阶段
         * - TYPE_MUTANUS_THE_DEVOURER: 吞噬者穆塔努斯
         * - TYPE_NARALEX_YELLED: 纳雷克斯喊话标记
         */
        void SetData(uint32 type, uint32 data) override
        {
            switch (type)
            {
                case TYPE_LORD_COBRAHN:         m_auiEncounter[0] = data;break;
                case TYPE_LORD_PYTHAS:          m_auiEncounter[1] = data;break;
                case TYPE_LADY_ANACONDRA:       m_auiEncounter[2] = data;break;
                case TYPE_LORD_SERPENTIS:       m_auiEncounter[3] = data;break;
                case TYPE_NARALEX_EVENT:        m_auiEncounter[4] = data;break;
                case TYPE_NARALEX_PART1:        m_auiEncounter[5] = data;break;
                case TYPE_NARALEX_PART2:        m_auiEncounter[6] = data;break;
                case TYPE_NARALEX_PART3:        m_auiEncounter[7] = data;break;
                case TYPE_MUTANUS_THE_DEVOURER: m_auiEncounter[8] = data;break;
                case TYPE_NARALEX_YELLED:       yelled = true;      break;
            }
            if (data == DONE)SaveToDB();
        }

        /**
         * @brief 获取遭遇战状态
         * @param type 遭遇战类型
         * @return 当前状态值
         *
         * 用于其他脚本查询Boss击杀进度
         * 例如:纳雷克斯的弟子需要确认4个领主都被击杀
         */
        uint32 GetData(uint32 type) const override
        {
            switch (type)
            {
                case TYPE_LORD_COBRAHN:         return m_auiEncounter[0];
                case TYPE_LORD_PYTHAS:          return m_auiEncounter[1];
                case TYPE_LADY_ANACONDRA:       return m_auiEncounter[2];
                case TYPE_LORD_SERPENTIS:       m_auiEncounter[3];
                case TYPE_NARALEX_EVENT:        return m_auiEncounter[4];
                case TYPE_NARALEX_PART1:        return m_auiEncounter[5];
                case TYPE_NARALEX_PART2:        return m_auiEncounter[6];
                case TYPE_NARALEX_PART3:        return m_auiEncounter[7];
                case TYPE_MUTANUS_THE_DEVOURER: return m_auiEncounter[8];
                case TYPE_NARALEX_YELLED:       return yelled;
            }
            return 0;
        }

        /**
         * @brief 获取NPC的GUID
         * @param data NPC数据ID
         * @return NPC的全局唯一标识符
         *
         * 目前仅用于获取纳雷克斯的GUID
         * 纳雷克斯的弟子需要通过此GUID与纳雷克斯交互
         */
        ObjectGuid GetGuidData(uint32 data) const override
        {
            if (data == DATA_NARALEX)return NaralexGUID;
            return ObjectGuid::Empty;
        }

        /**
         * @brief 生成副本进度保存数据
         * @return 字符串格式的保存数据
         *
         * 将所有遭遇战状态序列化为字符串
         * 格式: "0 1 2 3 4 5 6 7 8"(空格分隔的状态值)
         *
         * 性能注意:
         * - 仅在Boss被击杀时调用(SaveToDB)
         * - 使用ostringstream提高字符串拼接效率
         */
        std::string GetSaveData() override
        {
            OUT_SAVE_INST_DATA;

            std::ostringstream saveStream;
            saveStream << m_auiEncounter[0] << ' ' << m_auiEncounter[1] << ' ' << m_auiEncounter[2] << ' '
                << m_auiEncounter[3] << ' ' << m_auiEncounter[4] << ' ' << m_auiEncounter[5] << ' '
                << m_auiEncounter[6] << ' ' << m_auiEncounter[7] << ' ' << m_auiEncounter[8];

            OUT_SAVE_INST_DATA_COMPLETE;
            return saveStream.str();
        }

        /**
         * @brief 加载副本进度数据
         * @param in 保存的字符串数据
         *
         * 从数据库加载副本进度,恢复所有遭遇战状态
         * 如果状态不是DONE,则重置为NOT_STARTED(避免卡在IN_PROGRESS状态)
         *
         * 调用时机:
         * - 玩家进入已有 personally的副本时
         * - 服务器重启后加载副本状态时
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
            loadStream >> m_auiEncounter[0] >> m_auiEncounter[1] >> m_auiEncounter[2] >> m_auiEncounter[3]
            >> m_auiEncounter[4] >> m_auiEncounter[5] >> m_auiEncounter[6] >> m_auiEncounter[7] >> m_auiEncounter[8];

            // 重置未完成的遭遇战状态,防止副本卡死
            for (uint8 i = 0; i < MAX_ENCOUNTER; ++i)
                if (m_auiEncounter[i] != DONE)
                    m_auiEncounter[i] = NOT_STARTED;

            OUT_LOAD_INST_DATA_COMPLETE;
        }

    };

};

/**
 * @brief 注册哀嚎洞穴实例脚本
 *
 * 此函数在脚本加载时被调用,创建实例脚本对象
 */
void AddSC_instance_wailing_caverns()
{
    new instance_wailing_caverns();
}
