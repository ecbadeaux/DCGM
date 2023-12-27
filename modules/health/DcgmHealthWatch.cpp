/*
 * Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "DcgmHealthWatch.h"
#include "DcgmLogging.h"
#include "dcgm_errors.h"
#include "dcgm_test_apis.h"
#include "timelib.h"
#include <sstream>
#include <stdexcept>

const char *EntityToString(dcgm_field_entity_group_t entityGroupId)
{
    switch (entityGroupId)
    {
        case DCGM_FE_GPU:
            return "GPU";
        case DCGM_FE_VGPU:
            return "VGPU";
        case DCGM_FE_SWITCH:
            return "NvSwitch";
        case DCGM_FE_GPU_I:
            return "GPU Instance";
        case DCGM_FE_GPU_CI:
            return "Compute Instance";
        case DCGM_FE_LINK:
            return "Link";

        default:
            return "Unknown";
    }

    return "";
}

// Adds a watch for the specified field that will poll every 10 seconds for the last hour's events
#define ADD_WATCH(fieldId)                                                                                \
    do                                                                                                    \
    {                                                                                                     \
        bool updateOnFirstWatch = false; /* All of the callers of this call UpdateFields() right after */ \
        bool wereFirstWatcher   = false;                                                                  \
        ret                     = mpCoreProxy.AddFieldWatch(entityGroupId,                                \
                                        entityId,                                     \
                                        fieldId,                                      \
                                        updateInterval,                               \
                                        maxKeepAge,                                   \
                                        0,                                            \
                                        watcher,                                      \
                                        false,                                        \
                                        updateOnFirstWatch,                           \
                                        wereFirstWatcher);                            \
        if (DCGM_ST_OK != ret)                                                                            \
        {                                                                                                 \
            log_error("Failed to set watch for field {} on {} {} group {}",                               \
                      fieldId,                                                                            \
                      EntityToString(entityGroupId),                                                      \
                      entityId,                                                                           \
                      entityGroupId);                                                                     \
            return ret;                                                                                   \
        }                                                                                                 \
    } while (0)

/*****************************************************************************/
DcgmHealthWatch::DcgmHealthWatch(dcgmCoreCallbacks_t &dcc)
    : mpCoreProxy(dcc)
{
    m_mutex = new DcgmMutex(0);

    mGroupWatchState.clear();

    BuildFieldLists();
}

/*****************************************************************************/
DcgmHealthWatch::~DcgmHealthWatch()
{
    if (m_mutex)
    {
        delete (m_mutex);
        m_mutex = 0;
    }
}

/*****************************************************************************/
void DcgmHealthWatch::BuildFieldLists(void)
{
    // all the non-fatal error field ids.
    m_nvSwitchNonFatalFieldIds.push_back(DCGM_FI_DEV_NVSWITCH_NON_FATAL_ERRORS);

    // all the fatal error field ids.
    m_nvSwitchFatalFieldIds.push_back(DCGM_FI_DEV_NVSWITCH_FATAL_ERRORS);
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetNvSwitchWatches(std::vector<unsigned int> &groupSwitchIds,
                                                 dcgmHealthSystems_t systems,
                                                 DcgmWatcher watcher,
                                                 long long updateInterval,
                                                 double maxKeepAge)
{
    dcgmReturn_t dcgmReturn;
    std::vector<unsigned int>::iterator switchIter;
    unsigned int i;

    bool updateOnFirstWatch = false; /* All of the callers of this call UpdateFields() right after */
    bool wereFirstWatcher   = false;

    for (switchIter = groupSwitchIds.begin(); switchIter != groupSwitchIds.end(); ++switchIter)
    {
        if (systems & DCGM_HEALTH_WATCH_NVSWITCH_NONFATAL)
        {
            for (i = 0; i < m_nvSwitchNonFatalFieldIds.size(); i++)
            {
                dcgmReturn = mpCoreProxy.AddFieldWatch(DCGM_FE_SWITCH,
                                                       *switchIter,
                                                       m_nvSwitchNonFatalFieldIds[i],
                                                       updateInterval,
                                                       maxKeepAge,
                                                       0,
                                                       watcher,
                                                       false,
                                                       updateOnFirstWatch,
                                                       wereFirstWatcher);
                if (dcgmReturn != DCGM_ST_OK)
                {
                    log_error("Error {} from AddEntityFieldWatch() for NvSwitch fields", (int)dcgmReturn);
                    return dcgmReturn;
                }
            }
        }

        if (systems & DCGM_HEALTH_WATCH_NVSWITCH_FATAL)
        {
            for (i = 0; i < m_nvSwitchFatalFieldIds.size(); i++)
            {
                dcgmReturn = mpCoreProxy.AddFieldWatch(DCGM_FE_SWITCH,
                                                       *switchIter,
                                                       m_nvSwitchFatalFieldIds[i],
                                                       updateInterval,
                                                       maxKeepAge,
                                                       0,
                                                       watcher,
                                                       false,
                                                       updateOnFirstWatch,
                                                       wereFirstWatcher);
                if (dcgmReturn != DCGM_ST_OK)
                {
                    log_error("Error {} from AddEntityFieldWatch() for NvSwitch fields", (int)dcgmReturn);
                    return dcgmReturn;
                }
            }
        }
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetWatches(unsigned int groupId,
                                         dcgmHealthSystems_t systems,
                                         dcgm_connection_id_t connectionId,
                                         long long updateInterval,
                                         double maxKeepAge)
{
    dcgmReturn_t ret    = DCGM_ST_OK;
    dcgmReturn_t tmpRet = DCGM_ST_OK;
    std::vector<unsigned int> groupGpuIds;
    std::vector<unsigned int> groupSwitchIds;
    DcgmWatcher watcher(DcgmWatcherTypeHealthWatch, connectionId);
    std::vector<dcgmGroupEntityPair_t> entities;

    ret = mpCoreProxy.GetGroupEntities(groupId, entities);
    if (ret != DCGM_ST_OK)
    {
        log_error("Got st {} from GetGroupEntities()", ret);
        return ret;
    }

    dcgm_mutex_lock(m_mutex);
    mGroupWatchState[groupId] = systems;
    dcgm_mutex_unlock(m_mutex);

    /* Capture the entities that are GPUs as a separate list */
    for (size_t i = 0; i < entities.size(); i++)
    {
        switch (entities[i].entityGroupId)
        {
            // Handle GPUs, GPU instances, and compute instances the same for setting watches
            case DCGM_FE_GPU:
            case DCGM_FE_GPU_I:
            case DCGM_FE_GPU_CI:
                for (unsigned int bitIndex = 0; bitIndex < DCGM_HEALTH_WATCH_COUNT_V2; bitIndex++)
                {
                    unsigned int bit = 1 << bitIndex;
                    switch (bit)
                    {
                        case DCGM_HEALTH_WATCH_PCIE:
                            tmpRet = SetPcie(entities[i].entityGroupId,
                                             entities[i].entityId,
                                             (systems & bit) ? true : false,
                                             watcher,
                                             updateInterval,
                                             maxKeepAge);
                            break;
                        case DCGM_HEALTH_WATCH_MEM:
                            tmpRet = SetMem(entities[i].entityGroupId,
                                            entities[i].entityId,
                                            (systems & bit) ? true : false,
                                            watcher,
                                            updateInterval,
                                            maxKeepAge);
                            break;
                        case DCGM_HEALTH_WATCH_INFOROM:
                            tmpRet = SetInforom(entities[i].entityGroupId,
                                                entities[i].entityId,
                                                (systems & bit) ? true : false,
                                                watcher,
                                                updateInterval,
                                                maxKeepAge);
                            break;
                        case DCGM_HEALTH_WATCH_THERMAL:
                            tmpRet = SetThermal(entities[i].entityGroupId,
                                                entities[i].entityId,
                                                (systems & bit) ? true : false,
                                                watcher,
                                                updateInterval,
                                                maxKeepAge);
                            break;
                        case DCGM_HEALTH_WATCH_POWER:
                            tmpRet = SetPower(entities[i].entityGroupId,
                                              entities[i].entityId,
                                              (systems & bit) ? true : false,
                                              watcher,
                                              updateInterval,
                                              maxKeepAge);
                            break;
                        case DCGM_HEALTH_WATCH_NVLINK:
                            tmpRet = SetNVLink(entities[i].entityGroupId,
                                               entities[i].entityId,
                                               (systems & bit) ? true : false,
                                               watcher,
                                               updateInterval,
                                               maxKeepAge);
                            break;
                        default: // ignore everything else for now
                            break;
                    }
                    if (DCGM_ST_OK != tmpRet)
                    {
                        log_error("Error {} from bit {}, entity group {} entityId {}",
                                  (int)tmpRet,
                                  bit,
                                  entities[i].entityGroupId,
                                  entities[i].entityId);
                        break; // exit on error
                    }
                }
                break;

            case DCGM_FE_SWITCH:
                groupSwitchIds.push_back(entities[i].entityId);
                break;

            case DCGM_FE_LINK:
                /**
                 * DCGM-2836. Examine what we should do in this case. Watch
                 * for the health status of the associated switch or GPU,
                 * perhaps?
                 */
                break;

            case DCGM_FE_CPU:
                for (unsigned int bitIndex = 0; bitIndex < DCGM_HEALTH_WATCH_COUNT_V2; bitIndex++)
                {
                    unsigned int bit = 1 << bitIndex;
                    switch (bit)
                    {
                        case DCGM_HEALTH_WATCH_THERMAL:
                            tmpRet = SetCpuThermal(entities[i].entityGroupId,
                                                   entities[i].entityId,
                                                   (systems & bit) ? true : false,
                                                   watcher,
                                                   updateInterval,
                                                   maxKeepAge);
                            break;
                        case DCGM_HEALTH_WATCH_POWER:
                            tmpRet = SetCpuPower(entities[i].entityGroupId,
                                                 entities[i].entityId,
                                                 (systems & bit) ? true : false,
                                                 watcher,
                                                 updateInterval,
                                                 maxKeepAge);
                            break;
                        default: // ignore everything else for now
                            break;
                    }
                    if (DCGM_ST_OK != tmpRet)
                    {
                        log_error("Error {} from bit {}, entity group {} entityId {}",
                                  (int)tmpRet,
                                  bit,
                                  entities[i].entityGroupId,
                                  entities[i].entityId);
                        break; // exit on error
                    }
                }
                break;

            default:
                // NO-OP
                break;
        }
    }

    if (groupSwitchIds.size() > 0)
    {
        ret = SetNvSwitchWatches(groupSwitchIds, systems, watcher, updateInterval, maxKeepAge);
    }

    /* Make sure every field has updated */
    tmpRet = mpCoreProxy.UpdateAllFields(1);
    if (tmpRet != DCGM_ST_OK)
    {
        DCGM_LOG_ERROR << "UpdateAllFields() returned " << tmpRet;
        ret = tmpRet;
    }

    return ret;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::GetWatches(unsigned int groupId, dcgmHealthSystems_t *systems)
{
    dcgmReturn_t ret = DCGM_ST_OK;
    groupWatchTable_t::iterator groupWatchIter;
    std::vector<dcgmGroupEntityPair_t> entities;

    ret = mpCoreProxy.GetGroupEntities(groupId, entities);
    if (ret != DCGM_ST_OK)
    {
        log_error("Got st {} from GetGroupEntities()", ret);
        return ret;
    }

    dcgm_mutex_lock(m_mutex);
    groupWatchIter = mGroupWatchState.find(groupId);

    *systems = (groupWatchIter == mGroupWatchState.end()) ? (dcgmHealthSystems_enum)0 : groupWatchIter->second;

    dcgm_mutex_unlock(m_mutex);
    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorWatchesForGpu(unsigned int gpuId,
                                                   long long startTime,
                                                   long long endTime,
                                                   dcgmHealthSystems_t healthSystemsMask,
                                                   DcgmHealthResponse &response)

{
    dcgmReturn_t ret = DCGM_ST_OK;

    if (gpuId >= DCGM_MAX_NUM_DEVICES)
    {
        DCGM_LOG_ERROR << "Bad gpuId: " << gpuId;
        return DCGM_ST_BADPARAM;
    }

    for (unsigned int index = 0; index < DCGM_HEALTH_WATCH_COUNT_V1; index++)
    {
        unsigned int bit    = 1 << index;
        dcgmReturn_t tmpRet = DCGM_ST_OK;

        switch (bit)
        {
            case DCGM_HEALTH_WATCH_PCIE:
                if (bit & healthSystemsMask)
                    tmpRet = MonitorPcie(DCGM_FE_GPU, gpuId, startTime, endTime, response);
                break;
            case DCGM_HEALTH_WATCH_MEM:
                if (bit & healthSystemsMask)
                    tmpRet = MonitorMem(DCGM_FE_GPU, gpuId, startTime, endTime, response);
                break;
            case DCGM_HEALTH_WATCH_INFOROM:
                if (bit & healthSystemsMask)
                    tmpRet = MonitorInforom(DCGM_FE_GPU, gpuId, startTime, endTime, response);
                break;
            case DCGM_HEALTH_WATCH_THERMAL:
                if (bit & healthSystemsMask)
                    tmpRet = MonitorThermal(DCGM_FE_GPU, gpuId, startTime, endTime, response);
                break;
            case DCGM_HEALTH_WATCH_POWER:
                if (bit & healthSystemsMask)
                    tmpRet = MonitorPower(DCGM_FE_GPU, gpuId, startTime, endTime, response);
                break;
            case DCGM_HEALTH_WATCH_NVLINK:
                if (bit & healthSystemsMask)
                    tmpRet = MonitorNVLink(DCGM_FE_GPU, gpuId, startTime, endTime, response);
                break;
            default: // ignore everything else for now, other bugs
                break;
        }

        if ((ret == DCGM_ST_OK) && (tmpRet != DCGM_ST_OK))
            ret = tmpRet;
    }

    return ret;
}

bool DcgmHealthWatch::FitsGpuHardwareCheck(dcgm_field_entity_group_t entityGroupId)
{
    return (entityGroupId == DCGM_FE_GPU || entityGroupId == DCGM_FE_GPU_I || entityGroupId == DCGM_FE_GPU_CI);
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorWatches(unsigned int groupId,
                                             long long startTime,
                                             long long endTime,
                                             DcgmHealthResponse &response)
{
    unsigned int index;
    dcgmReturn_t ret = DCGM_ST_OK;
    std::vector<dcgmGroupEntityPair_t> entities;
    dcgmHealthSystems_t healthSystemsMask = (dcgmHealthSystems_t)0; /* Cached version of this group's watch mask */
    std::vector<unsigned int> gpuIds;

    /* Handle BLANK start-time and end-time */
    if (DCGM_INT64_IS_BLANK(startTime))
        startTime = 0;
    if (DCGM_INT64_IS_BLANK(endTime))
        endTime = 0;

    ret = mpCoreProxy.GetGroupEntities(groupId, entities);
    if (ret != DCGM_ST_OK)
    {
        log_error("Got st {} from GetGroupEntities()", ret);
        return ret;
    }

    dcgm_mutex_lock(m_mutex);
    groupWatchTable_t::iterator groupWatchIter = mGroupWatchState.find(groupId);
    if (groupWatchIter != mGroupWatchState.end())
    {
        healthSystemsMask = groupWatchIter->second;
        log_debug("Found health systems mask {:X} for groupId {}", (unsigned int)healthSystemsMask, groupId);
    }
    else
    {
        log_debug("Found NO health systems mask for groupId {}", groupId);
    }
    dcgm_mutex_unlock(m_mutex);

    if (healthSystemsMask == 0)
        return DCGM_ST_OK; /* This is the same as walking over the loops below and doing nothing */

    for (size_t entityIndex = 0; entityIndex < entities.size(); entityIndex++)
    {
        dcgm_field_entity_group_t entityGroupId = entities[entityIndex].entityGroupId;
        dcgm_field_eid_t entityId               = entities[entityIndex].entityId;

        for (index = 0; index < DCGM_HEALTH_WATCH_COUNT_V2; index++)
        {
            unsigned int bit = 1 << index;

            switch (bit)
            {
                case DCGM_HEALTH_WATCH_PCIE:
                    if (bit & healthSystemsMask && FitsGpuHardwareCheck(entityGroupId))
                        ret = MonitorPcie(entityGroupId, entityId, startTime, endTime, response);
                    break;
                case DCGM_HEALTH_WATCH_MEM:
                    if (bit & healthSystemsMask && FitsGpuHardwareCheck(entityGroupId))
                        ret = MonitorMem(entityGroupId, entityId, startTime, endTime, response);
                    break;
                case DCGM_HEALTH_WATCH_INFOROM:
                    if (bit & healthSystemsMask && FitsGpuHardwareCheck(entityGroupId))
                        ret = MonitorInforom(entityGroupId, entityId, startTime, endTime, response);
                    break;
                case DCGM_HEALTH_WATCH_THERMAL:
                    if (bit & healthSystemsMask && FitsGpuHardwareCheck(entityGroupId))
                    {
                        ret = MonitorThermal(entityGroupId, entityId, startTime, endTime, response);
                        if (ret != DCGM_ST_OK)
                        {
                            break;
                        }
                    }
                    if (bit & healthSystemsMask && entityGroupId == DCGM_FE_CPU)
                    {
                        ret = MonitorCpuThermal(entityGroupId, entityId, startTime, endTime, response);
                    }
                    break;
                case DCGM_HEALTH_WATCH_POWER:
                    if (bit & healthSystemsMask && FitsGpuHardwareCheck(entityGroupId))
                    {
                        ret = MonitorPower(entityGroupId, entityId, startTime, endTime, response);
                        if (ret != DCGM_ST_OK)
                        {
                            break;
                        }
                    }
                    if (bit & healthSystemsMask && entityGroupId == DCGM_FE_CPU)
                    {
                        ret = MonitorCpuPower(entityGroupId, entityId, startTime, endTime, response);
                    }
                    break;
                case DCGM_HEALTH_WATCH_NVLINK:
                    if (bit & healthSystemsMask && FitsGpuHardwareCheck(entityGroupId))
                        ret = MonitorNVLink(entityGroupId, entityId, startTime, endTime, response);
                    break;
                case DCGM_HEALTH_WATCH_NVSWITCH_NONFATAL:
                    if (bit & healthSystemsMask && entityGroupId == DCGM_FE_SWITCH)
                        ret = MonitorNvSwitchErrorCounts(false, entityGroupId, entityId, startTime, endTime, response);
                    break;
                case DCGM_HEALTH_WATCH_NVSWITCH_FATAL:
                    if (bit & healthSystemsMask && entityGroupId == DCGM_FE_SWITCH)
                        ret = MonitorNvSwitchErrorCounts(true, entityGroupId, entityId, startTime, endTime, response);
                    break;
                default:
                    // reduce the logging level as this may pollute the log file if unsupported fields are watched
                    // continuously.
                    log_debug("Unhandled health bit {}", bit);
                    break;
            }
        }
    }

    return ret;
}

std::string DcgmHealthWatch::GetHealthSystemAsString(dcgmHealthSystems_t system)
{
    switch (system)
    {
        case DCGM_HEALTH_WATCH_PCIE:
            return "PCIe";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_NVLINK:
            return "NVLink";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_PMU:
            return "PMU";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_MCU:
            return "MCU";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_MEM:
            return "Memory";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_SM:
            return "SM";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_INFOROM:
            return "Inforom";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_THERMAL:
            return "Thermal";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_POWER:
            return "Power";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_DRIVER:
            return "Driver";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_NVSWITCH_NONFATAL:
            return "NVSwitch non-fatal errors";
            break; // NOT REACHED
        case DCGM_HEALTH_WATCH_NVSWITCH_FATAL:
            return "NVSwitch fatal errors";
            break; // NOT REACHED
        default:
            return "Unknown";
            break; // NOT REACHED
    }
}

std::string DcgmHealthWatch::GetHealthResultAsString(dcgmHealthWatchResults_t result)
{
    switch (result)
    {
        case DCGM_HEALTH_RESULT_PASS:
            return "PASS";
            break; // NOT REACHED
        case DCGM_HEALTH_RESULT_WARN:
            return "WARNING";
            break; // NOT REACHED
        case DCGM_HEALTH_RESULT_FAIL:
            return "FAILURE";
            break; // NOT REACHED
        default:
            return "UNKNOWN";
            break; // NOT REACHED
    }
}

/*****************************************************************************/
void DcgmHealthWatch::SetResponse(dcgm_field_entity_group_t entityGroupId,
                                  dcgm_field_eid_t entityId,
                                  dcgmHealthWatchResults_t status,
                                  dcgmHealthSystems_t system,
                                  DcgmError &d,
                                  DcgmHealthResponse &response)
{
    dcgmDiagErrorDetail_t error;
    snprintf(error.msg, sizeof(error.msg), "%s", d.GetMessage().c_str());
    error.code = d.GetCode();
    response.AddIncident(system, status, error, entityGroupId, entityId);
    log_error("Detected a {} in health system {}: '{}'",
              GetHealthResultAsString(status),
              GetHealthSystemAsString(system),
              d.GetMessage());
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetPcie(dcgm_field_entity_group_t entityGroupId,
                                      unsigned int entityId,
                                      bool enable,
                                      DcgmWatcher watcher,
                                      long long updateInterval,
                                      double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    ADD_WATCH(DCGM_FI_DEV_PCIE_REPLAY_COUNTER);
    return ret;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetMem(dcgm_field_entity_group_t entityGroupId,
                                     unsigned int entityId,
                                     bool enable,
                                     DcgmWatcher watcher,
                                     long long updateInterval,
                                     double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    ADD_WATCH(DCGM_FI_DEV_ECC_DBE_VOL_TOTAL);

    // the sampling of 1 second is fine for the above, these however should have a longer sampling rate
    updateInterval = std::max(30000000ll, updateInterval);

    bool updateOnFirstWatch = false; /* The caller calls UpdateFields() after this */
    bool wereFirstWatcher   = false;

    // single and double bit retired pages

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_RETIRED_SBE,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_RETIRED_SBE,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_RETIRED_DBE,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_RETIRED_DBE,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_RETIRED_PENDING,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_RETIRED_PENDING,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    /* Note that we're subscribing for XID updates so that OnFieldValuesUpdate and eventually ProcessXidFv
       get called */
    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_XID_ERRORS,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    true,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_XID_ERRORS,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_ROW_REMAP_FAILURE,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_ROW_REMAP_FAILURE,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    return ret;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetInforom(dcgm_field_entity_group_t entityGroupId,
                                         unsigned int entityId,
                                         bool enable,
                                         DcgmWatcher watcher,
                                         long long updateInterval,
                                         double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    updateInterval = std::max(3600000000ll, updateInterval);
    maxKeepAge     = std::max(7200.0, maxKeepAge); /* Keep at least 2 hours of data so we can get a sample */

    bool updateOnFirstWatch = false; /* The caller calls UpdateFields() after this */
    bool wereFirstWatcher   = false;

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_INFOROM_CONFIG_VALID,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_INFOROM_CONFIG_VALID,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    return DCGM_ST_OK;
}


/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetThermal(dcgm_field_entity_group_t entityGroupId,
                                         unsigned int entityId,
                                         bool enable,
                                         DcgmWatcher watcher,
                                         long long updateInterval,
                                         double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    /* Enforce a minimum sample rate of every 30 seconds */
    updateInterval = std::max(30000000ll, updateInterval);

    bool updateOnFirstWatch = false; /* The caller calls UpdateFields() after this */
    bool wereFirstWatcher   = false;

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_THERMAL_VIOLATION,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_THERMAL_VIOLATION,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetPower(dcgm_field_entity_group_t entityGroupId,
                                       unsigned int entityId,
                                       bool enable,
                                       DcgmWatcher watcher,
                                       long long updateInterval,
                                       double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    /* Enforce a minimum sample rate of every 30 seconds */
    updateInterval = std::max(30000000ll, updateInterval);

    bool updateOnFirstWatch = false; /* The caller calls UpdateFields() after this */
    bool wereFirstWatcher   = false;

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_POWER_VIOLATION,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_POWER_VIOLATION,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    ret = mpCoreProxy.AddFieldWatch(entityGroupId,
                                    entityId,
                                    DCGM_FI_DEV_POWER_USAGE,
                                    updateInterval,
                                    maxKeepAge,
                                    0,
                                    watcher,
                                    false,
                                    updateOnFirstWatch,
                                    wereFirstWatcher);
    if (DCGM_ST_OK != ret)
    {
        log_error("Failed to set watch for field {} on {} {}",
                  DCGM_FI_DEV_POWER_VIOLATION,
                  EntityToString(entityGroupId),
                  entityId);
        return ret;
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetCpuThermal(dcgm_field_entity_group_t entityGroupId,
                                            unsigned int entityId,
                                            bool enable,
                                            DcgmWatcher watcher,
                                            long long updateInterval,
                                            double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    /* Enforce a minimum sample rate of every 30 seconds */
    updateInterval = std::max(30000000ll, updateInterval);

    ADD_WATCH(DCGM_FI_DEV_CPU_TEMP_CURRENT);
    ADD_WATCH(DCGM_FI_DEV_CPU_TEMP_WARNING);
    ADD_WATCH(DCGM_FI_DEV_CPU_TEMP_CRITICAL);

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetCpuPower(dcgm_field_entity_group_t entityGroupId,
                                          unsigned int entityId,
                                          bool enable,
                                          DcgmWatcher watcher,
                                          long long updateInterval,
                                          double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    /* Enforce a minimum sample rate of every 30 seconds */
    updateInterval = std::max(30000000ll, updateInterval);

    ADD_WATCH(DCGM_FI_DEV_CPU_POWER_UTIL_CURRENT);
    ADD_WATCH(DCGM_FI_DEV_CPU_POWER_LIMIT);

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::SetNVLink(dcgm_field_entity_group_t entityGroupId,
                                        unsigned int entityId,
                                        bool enable,
                                        DcgmWatcher watcher,
                                        long long updateInterval,
                                        double maxKeepAge)
{
    // currently if a watch is removed it removes for the entire system (i.e. no reference counter)
    // thus ignore the "enable" flag for now
    dcgmReturn_t ret = DCGM_ST_OK;

    if (!enable) // ignore
        return ret;

    ADD_WATCH(DCGM_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_TOTAL);
    ADD_WATCH(DCGM_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_TOTAL);
    ADD_WATCH(DCGM_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_TOTAL);
    ADD_WATCH(DCGM_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_TOTAL);

    return ret;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorPcie(dcgm_field_entity_group_t entityGroupId,
                                          dcgm_field_eid_t entityId,
                                          long long startTime,
                                          long long endTime,
                                          DcgmHealthResponse &response)
{
    dcgmReturn_t ret           = DCGM_ST_OK;
    unsigned short fieldId     = DCGM_FI_DEV_PCIE_REPLAY_COUNTER;
    dcgmcm_sample_t startValue = {};
    dcgmcm_sample_t endValue   = {};

    int count                    = 0;
    unsigned int oneMinuteInUsec = 60000000;
    timelib64_t now              = timelib_usecSince1970();


    /* Update the start time if it is blank. Allow endTime to be in the future */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Get the value of the field at the StartTime*/
    count = 1;
    ret   = mpCoreProxy.GetSamples(
        entityGroupId, entityId, fieldId, &startValue, &count, startTime, endTime, DCGM_ORDER_ASCENDING);

    if (DCGM_ST_NO_DATA == ret)
    {
        log_debug("No data for PCIe for gpuId {}", entityId);
        return DCGM_ST_OK;
    }
    else if (DCGM_ST_NOT_WATCHED == ret)
    {
        log_warning("PCIe not watched for gpuId {}", entityId);
        return DCGM_ST_OK;
    }
    else if (DCGM_ST_OK != ret)
    {
        log_error("mpCoreProxy.GetSamples returned {} for gpuId {}", (int)ret, entityId);
        return ret;
    }

    if (DCGM_INT64_IS_BLANK(startValue.val.i64))
        return DCGM_ST_OK;

    /* Get the value of the field at the endTime*/
    count = 1;
    ret   = mpCoreProxy.GetSamples(
        entityGroupId, entityId, fieldId, &endValue, &count, startTime, endTime, DCGM_ORDER_DESCENDING);
    if (DCGM_ST_NO_DATA == ret)
    {
        log_debug("No data for PCIe for gpuId {}", entityId);
        return DCGM_ST_OK;
    }
    else if (DCGM_ST_NOT_WATCHED == ret)
    {
        log_warning("PCIe not watched for gpuId {}", entityId);
        return DCGM_ST_OK;
    }
    else if (DCGM_ST_OK != ret)
    {
        log_error("mpCoreProxy.GetSamples returned {} for gpuId {}", (int)ret, entityId);
        return ret;
    }

    if (DCGM_INT64_IS_BLANK(endValue.val.i64))
        return DCGM_ST_OK;


    // NO DATA is handled automatically so here we can assume we have the values from the last minute
    // both values have been checked for BLANK values so can be used here
    int pciReplayRate = (startValue.val.i64 >= endValue.val.i64) ? (startValue.val.i64 - endValue.val.i64)
                                                                 : (endValue.val.i64 - startValue.val.i64);


    if (pciReplayRate > DCGM_LIMIT_MAX_PCIREPLAY_RATE)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_PCI_REPLAY_RATE, d, DCGM_LIMIT_MAX_PCIREPLAY_RATE, entityId, pciReplayRate);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_PCIE, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
std::string DcgmHealthWatch::MemFieldToString(unsigned short fieldId)
{
    switch (fieldId)
    {
        case DCGM_FI_DEV_ECC_SBE_VOL_TOTAL:
            return "Volatile SBEs";
        case DCGM_FI_DEV_ECC_DBE_VOL_TOTAL:
            return "Volatile DBEs";
        default:
            return "Error";
    }
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorMemVolatileDbes(dcgm_field_entity_group_t entityGroupId,
                                                     dcgm_field_eid_t entityId,
                                                     long long startTime,
                                                     long long endTime,
                                                     DcgmHealthResponse &response)
{
    // first handle the actual error counts
    // if our stored value is greater than the returned value then someone likely
    // reset the volatile counter.  Just reset ours
    dcgmReturn_t ret;
    int count              = 1;
    dcgmcm_sample_t sample = {};

    ret = mpCoreProxy.GetSamples(entityGroupId,
                                 entityId,
                                 DCGM_FI_DEV_ECC_DBE_VOL_TOTAL,
                                 &sample,
                                 &count,
                                 startTime,
                                 endTime,
                                 DCGM_ORDER_DESCENDING);

    if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA && ret != DCGM_ST_NOT_WATCHED)
    {
        DCGM_LOG_ERROR << "GetSamples got ret " << errorString(ret);
        return ret;
    }

    if (DCGM_INT64_IS_BLANK(sample.val.i64))
    {
        DCGM_LOG_DEBUG << "DCGM_FI_DEV_ECC_DBE_VOL_TOTAL was blank for eg " << entityGroupId << ", eid " << entityId;
        return DCGM_ST_OK;
    }

    // Fail for any volatile DBEs
    if (sample.val.i64 > 0)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(
            DCGM_FR_VOLATILE_DBE_DETECTED, d, static_cast<unsigned int>(sample.val.i64), entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_MEM, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorMemRetiredPending(dcgm_field_entity_group_t entityGroupId,
                                                       dcgm_field_eid_t entityId,
                                                       long long startTime,
                                                       long long endTime,
                                                       DcgmHealthResponse &response)
{
    dcgmcm_sample_t retiredPending = {};
    int count                      = 1;
    dcgmReturn_t ret               = mpCoreProxy.GetSamples(entityGroupId,
                                              entityId,
                                              DCGM_FI_DEV_RETIRED_PENDING,
                                              &retiredPending,
                                              &count,
                                              startTime,
                                              endTime,
                                              DCGM_ORDER_DESCENDING);

    if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA && ret != DCGM_ST_NOT_WATCHED)
    {
        DCGM_LOG_ERROR << "GetSamples got ret " << errorString(ret);
        return ret;
    }

    if (!DCGM_INT64_IS_BLANK(retiredPending.val.i64) && retiredPending.val.i64 != 0)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_PENDING_PAGE_RETIREMENTS, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_MEM, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorMemSbeDbeRetiredPages(dcgm_field_entity_group_t entityGroupId,
                                                           dcgm_field_eid_t entityId,
                                                           long long startTime,
                                                           long long endTime,
                                                           DcgmHealthResponse &response)
{
    dcgmcm_sample_t sbeRetiredPage = {};
    dcgmcm_sample_t dbeRetiredPage = {};
    int count                      = 1;
    dcgmReturn_t ret               = mpCoreProxy.GetSamples(entityGroupId,
                                              entityId,
                                              DCGM_FI_DEV_RETIRED_DBE,
                                              &dbeRetiredPage,
                                              &count,
                                              startTime,
                                              endTime,
                                              DCGM_ORDER_DESCENDING);

    if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA && ret != DCGM_ST_NOT_WATCHED)
    {
        DCGM_LOG_ERROR << "GetSamples got ret " << errorString(ret);
        return ret;
    }

    count = 1;
    ret   = mpCoreProxy.GetSamples(entityGroupId,
                                 entityId,
                                 DCGM_FI_DEV_RETIRED_SBE,
                                 &sbeRetiredPage,
                                 &count,
                                 startTime,
                                 endTime,
                                 DCGM_ORDER_DESCENDING);

    if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA && ret != DCGM_ST_NOT_WATCHED)
    {
        DCGM_LOG_ERROR << "GetSamples got ret " << errorString(ret);
        return ret;
    }

    long long totalRetiredPages = 0;

    if (!DCGM_INT64_IS_BLANK(sbeRetiredPage.val.i64))
    {
        totalRetiredPages += sbeRetiredPage.val.i64;
    }

    if (!DCGM_INT64_IS_BLANK(dbeRetiredPage.val.i64))
    {
        totalRetiredPages += dbeRetiredPage.val.i64;
    }

    // the combined total of retired pages should not be more than or equal to DCGM_LIMIT_MAX_RETIRED_PAGES
    // which is set via bug 1665722
    if (totalRetiredPages >= DCGM_LIMIT_MAX_RETIRED_PAGES)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_RETIRED_PAGES_LIMIT, d, DCGM_LIMIT_MAX_RETIRED_PAGES, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_MEM, d, response);
        return ret;
    }

    // The dbe retired pages should not be more than DCGM_LIMIT_MAX_RETIRED_PAGES_SOFT_LIMIT
    // *AND* be accumulating more than 1 per week after the limit has been met
    // JIRA DCGM-458
    if (!DCGM_INT64_IS_BLANK(dbeRetiredPage.val.i64)
        && dbeRetiredPage.val.i64 > DCGM_LIMIT_MAX_RETIRED_PAGES_SOFT_LIMIT)
    {
        // Check whether the rate of continuing page retirments (after the SOFT_LIMIT) meets the failure condition.
        dcgmReturn_t localReturn                  = DCGM_ST_OK;
        dcgmcm_sample_t oneWeekAgoDbeRetiredPages = {};
        timelib64_t oneWeekInUsec                 = 604800000000;
        timelib64_t now                           = timelib_usecSince1970();
        count                                     = 1;
        // Get the number of dbe retired pages before current week
        localReturn = mpCoreProxy.GetSamples(entityGroupId,
                                             entityId,
                                             DCGM_FI_DEV_RETIRED_DBE,
                                             &oneWeekAgoDbeRetiredPages,
                                             &count,
                                             0,
                                             now - oneWeekInUsec,
                                             DCGM_ORDER_DESCENDING);

        if (localReturn != DCGM_ST_OK && localReturn != DCGM_ST_NO_DATA)
        {
            DCGM_LOG_ERROR << "GetSamples got ret " << errorString(ret);
            return ret;
        }

        if (DCGM_INT64_IS_BLANK(oneWeekAgoDbeRetiredPages.val.i64))
        {
            DCGM_LOG_DEBUG << "oneWeekAgoDbeRetiredPages was blank";
            return DCGM_ST_OK;
        }

        int64_t dbePagesRetiredThisWeek = dbeRetiredPage.val.i64 - oneWeekAgoDbeRetiredPages.val.i64;
        if (dbePagesRetiredThisWeek > 1)
        {
            // More than one page retired due to DBE in the past week, failure condition met.
            DcgmError d { entityId };
            DCGM_ERROR_FORMAT_MESSAGE(
                DCGM_FR_RETIRED_PAGES_DBE_LIMIT, d, DCGM_LIMIT_MAX_RETIRED_PAGES_SOFT_LIMIT, entityId);
            SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_MEM, d, response);
        }
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorMemRowRemapFailures(dcgm_field_entity_group_t entityGroupId,
                                                         dcgm_field_eid_t entityId,
                                                         long long startTime,
                                                         long long endTime,
                                                         DcgmHealthResponse &response)
{
    // first handle the actual error counts
    // if our stored value is greater than the returned value then someone likely
    // reset the volatile counter.  Just reset ours
    dcgmReturn_t ret;
    int count              = 1;
    dcgmcm_sample_t sample = {};

    ret = mpCoreProxy.GetSamples(entityGroupId,
                                 entityId,
                                 DCGM_FI_DEV_ROW_REMAP_FAILURE,
                                 &sample,
                                 &count,
                                 startTime,
                                 endTime,
                                 DCGM_ORDER_DESCENDING);

    if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA && ret != DCGM_ST_NOT_WATCHED)
    {
        DCGM_LOG_ERROR << "GetSamples got ret " << errorString(ret);
        return ret;
    }

    if (DCGM_INT64_IS_BLANK(sample.val.i64))
    {
        DCGM_LOG_DEBUG << "DCGM_FI_DEV_ROW_REMAP_FAILURE was blank for eg " << entityGroupId << ", eid " << entityId;
        return DCGM_ST_OK;
    }

    // Fail for any volatile DBEs
    if (sample.val.i64 > 0)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_ROW_REMAP_FAILURE, d);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_MEM, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorUncontainedErrors(dcgm_field_entity_group_t entityGroupId,
                                                       dcgm_field_eid_t entityId,
                                                       long long startTime,
                                                       long long endTime,
                                                       DcgmHealthResponse &response)
{
    if (entityGroupId != DCGM_FE_GPU)
    {
        return DCGM_ST_OK;
    }

    DcgmLockGuard dlg(m_mutex);
    if (m_gpuHadUncontainedErrorXid.find(entityId) == m_gpuHadUncontainedErrorXid.end())
    {
        DCGM_LOG_DEBUG << "gpuId " << entityId << " hasn't had any uncontained errors";
        return DCGM_ST_OK;
    }

    DCGM_LOG_ERROR << "gpuId " << entityId << " has had an uncontained error";

    DcgmError d { entityId };
    DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_UNCONTAINED_ERROR, d);
    SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_MEM, d, response);
    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorMem(dcgm_field_entity_group_t entityGroupId,
                                         dcgm_field_eid_t entityId,
                                         long long startTime,
                                         long long endTime,
                                         DcgmHealthResponse &response)
{
    dcgmReturn_t ret             = DCGM_ST_OK;
    dcgmReturn_t localReturn     = DCGM_ST_OK;
    unsigned int oneMinuteInUsec = 60000000;
    timelib64_t now              = timelib_usecSince1970();

    /* Update the start time if it is blank */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    localReturn = MonitorMemVolatileDbes(entityGroupId, entityId, startTime, endTime, response);
    if (localReturn != DCGM_ST_OK)
    {
        ret = localReturn;
    }

    localReturn = MonitorMemRetiredPending(entityGroupId, entityId, startTime, endTime, response);
    if (localReturn != DCGM_ST_OK)
    {
        ret = localReturn;
    }

    localReturn = MonitorMemSbeDbeRetiredPages(entityGroupId, entityId, startTime, endTime, response);
    if (localReturn != DCGM_ST_OK)
    {
        ret = localReturn;
    }

    localReturn = MonitorMemRowRemapFailures(entityGroupId, entityId, startTime, endTime, response);
    if (localReturn != DCGM_ST_OK)
    {
        ret = localReturn;
    }

    localReturn = MonitorUncontainedErrors(entityGroupId, entityId, startTime, endTime, response);
    if (localReturn != DCGM_ST_OK)
    {
        ret = localReturn;
    }

    return ret;
}

dcgmReturn_t DcgmHealthWatch::MonitorInforom(dcgm_field_entity_group_t entityGroupId,
                                             dcgm_field_eid_t entityId,
                                             long long startTime,
                                             long long endTime,
                                             DcgmHealthResponse &response)
{
    dcgmReturn_t ret       = DCGM_ST_OK;
    dcgmcm_sample_t sample = {};
    unsigned short fieldId = DCGM_FI_DEV_INFOROM_CONFIG_VALID;

    if (!startTime)
    {
        startTime = 0; /* Check from the start of the cache */
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    /* check for the fieldValue at the endTime*/
    ret = mpCoreProxy.GetLatestSample(entityGroupId, entityId, fieldId, &sample, 0);

    if (DCGM_ST_NO_DATA == ret)
    {
        log_debug("No data for inforom for gpuId {}", entityId);
        return DCGM_ST_OK;
    }
    else if (DCGM_ST_NOT_WATCHED == ret)
    {
        log_warning("Not watched for inforom for gpuId {}", entityId);
        return DCGM_ST_OK;
    }
    else if (DCGM_ST_OK != ret)
    {
        log_error("Unable to retrieve field {} from cache. gpuId {}", fieldId, entityId);
        return ret;
    }

    if (DCGM_INT64_IS_BLANK(sample.val.i64))
        return DCGM_ST_OK;

    if (!(sample.val.i64))
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_CORRUPT_INFOROM, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_INFOROM, d, response);
    }

    return ret;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorThermal(dcgm_field_entity_group_t entityGroupId,
                                             dcgm_field_eid_t entityId,
                                             long long startTime,
                                             long long endTime,
                                             DcgmHealthResponse &response)
{
    dcgmReturn_t ret            = DCGM_ST_OK;
    unsigned short fieldId      = DCGM_FI_DEV_THERMAL_VIOLATION;
    dcgmcm_sample_t startValue  = {};
    dcgmcm_sample_t endValue    = {};
    int count                   = 1;
    long long int violationTime = 0;

    timelib64_t now              = timelib_usecSince1970();
    unsigned int oneMinuteInUsec = 60000000;

    /* Update the start and the end time if they are blank */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    /* Get the value at the startTime */
    ret = mpCoreProxy.GetSamples(
        entityGroupId, entityId, fieldId, &startValue, &count, startTime, endTime, DCGM_ORDER_ASCENDING);

    if (DCGM_ST_NO_DATA == ret)
        return DCGM_ST_OK;
    if (DCGM_ST_OK != ret)
        return ret;
    if (DCGM_INT64_IS_BLANK(startValue.val.i64))
        return DCGM_ST_OK;


    /* Get the value at the endTime*/
    ret = mpCoreProxy.GetLatestSample(entityGroupId, entityId, fieldId, &endValue, 0);

    if (DCGM_ST_NO_DATA == ret)
        return DCGM_ST_OK;
    if (DCGM_ST_OK != ret)
        return ret;
    if (DCGM_INT64_IS_BLANK(endValue.val.i64))
        return DCGM_ST_OK;

    // NO DATA is handled automatically so here we can assume we have the values from the last minute
    // both values have been checked for BLANK values so can be used here
    violationTime = startValue.val.i64 >= endValue.val.i64 ? (startValue.val.i64 - endValue.val.i64)
                                                           : (endValue.val.i64 - startValue.val.i64);

    if (violationTime)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_CLOCK_THROTTLE_THERMAL, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_THERMAL, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorPower(dcgm_field_entity_group_t entityGroupId,
                                           dcgm_field_eid_t entityId,
                                           long long startTime,
                                           long long endTime,
                                           DcgmHealthResponse &response)
{
    dcgmReturn_t ret             = DCGM_ST_OK;
    unsigned short fieldId       = DCGM_FI_DEV_POWER_VIOLATION;
    dcgmcm_sample_t startValue   = {};
    dcgmcm_sample_t endValue     = {};
    unsigned int oneMinuteInUsec = 60000000;
    int count                    = 0;
    long long int violationTime  = 0;
    dcgmcm_sample_t sample       = {};

    timelib64_t now = timelib_usecSince1970();

    // Warn if we cannot read the power on this entity
    if (entityGroupId == DCGM_FE_GPU)
    {
        ret = mpCoreProxy.GetLatestSample(entityGroupId, entityId, DCGM_FI_DEV_POWER_USAGE, &sample, 0);
        if (ret == DCGM_ST_OK && DCGM_FP64_IS_BLANK(sample.val.d) && sample.val.d != DCGM_FP64_NOT_SUPPORTED)
        {
            // We aren't successfully reading the power for this GPU, add a warning
            DcgmError d { entityId };
            DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_POWER_UNREADABLE, d, entityId);
            SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_POWER, d, response);
        }
    }

    /* Update the start and the end time if they are blank */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    /* Update the value at the start time*/
    count = 1;
    ret   = mpCoreProxy.GetSamples(
        entityGroupId, entityId, fieldId, &startValue, &count, startTime, endTime, DCGM_ORDER_ASCENDING);

    if (DCGM_ST_NO_DATA == ret)
        return DCGM_ST_OK;
    if (DCGM_ST_OK != ret)
        return ret;
    if (DCGM_INT64_IS_BLANK(startValue.val.i64))
        return DCGM_ST_OK;


    /* Update the value at the end time */
    count = 1;
    ret   = mpCoreProxy.GetSamples(
        entityGroupId, entityId, fieldId, &endValue, &count, startTime, endTime, DCGM_ORDER_DESCENDING);
    if (DCGM_ST_NO_DATA == ret)
        return DCGM_ST_OK;
    if (DCGM_ST_OK != ret)
        return ret;
    if (DCGM_INT64_IS_BLANK(endValue.val.i64))
        return DCGM_ST_OK;

    // NO DATA is handled automatically so here we can assume we have the values from the last minute
    // both values have been checked for BLANK values so can be used here
    violationTime = startValue.val.i64 >= endValue.val.i64 ? (startValue.val.i64 - endValue.val.i64)
                                                           : (endValue.val.i64 - startValue.val.i64);

    if (violationTime)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_CLOCK_THROTTLE_POWER, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_POWER, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorCpuThermal(dcgm_field_entity_group_t entityGroupId,
                                                dcgm_field_eid_t entityId,
                                                long long startTime,
                                                long long endTime,
                                                DcgmHealthResponse &response)
{
    dcgmReturn_t ret = DCGM_ST_OK;
    std::vector<unsigned short> fieldId { DCGM_FI_DEV_CPU_TEMP_CURRENT,
                                          DCGM_FI_DEV_CPU_TEMP_WARNING,
                                          DCGM_FI_DEV_CPU_TEMP_CRITICAL };
    std::unordered_map<unsigned short, dcgmcm_sample_t> startValue {};
    std::unordered_map<unsigned short, dcgmcm_sample_t> endValue {};
    int count = 1;

    timelib64_t now              = timelib_usecSince1970();
    unsigned int oneMinuteInUsec = 60000000;

    /* Update the start and the end time if they are blank */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    /* Get the value at the startTime */
    for (auto field : fieldId)
    {
        ret = mpCoreProxy.GetSamples(
            entityGroupId, entityId, field, &startValue[field], &count, startTime, endTime, DCGM_ORDER_ASCENDING);

        if (DCGM_ST_NO_DATA == ret)
            return DCGM_ST_OK;
        if (DCGM_ST_OK != ret)
            return ret;
        if (DCGM_INT64_IS_BLANK(startValue[field].val.i64))
            return DCGM_ST_OK;
    }

    /* Get the value at the endTime*/
    for (auto field : fieldId)
    {
        ret = mpCoreProxy.GetLatestSample(entityGroupId, entityId, field, &endValue[field], 0);

        if (DCGM_ST_NO_DATA == ret)
            return DCGM_ST_OK;
        if (DCGM_ST_OK != ret)
            return ret;
        if (DCGM_INT64_IS_BLANK(endValue[field].val.i64))
            return DCGM_ST_OK;
    }

    // First check: start and end samples are over the warning threshold (WARN)
    if (((startValue[DCGM_FI_DEV_CPU_TEMP_CURRENT].val.d + endValue[DCGM_FI_DEV_CPU_TEMP_CURRENT].val.d) / 2)
        >= endValue[DCGM_FI_DEV_CPU_TEMP_WARNING].val.d)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_FIELD_THRESHOLD_DBL, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_WARN, DCGM_HEALTH_WATCH_THERMAL, d, response);
    }
    // If the latest sample is over the critical threshold (FAIL)
    if (endValue[DCGM_FI_DEV_CPU_TEMP_CURRENT].val.d >= endValue[DCGM_FI_DEV_CPU_TEMP_CRITICAL].val.d)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_FIELD_THRESHOLD_DBL, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_THERMAL, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorCpuPower(dcgm_field_entity_group_t entityGroupId,
                                              dcgm_field_eid_t entityId,
                                              long long startTime,
                                              long long endTime,
                                              DcgmHealthResponse &response)
{
    dcgmReturn_t ret = DCGM_ST_OK;
    std::vector<unsigned short> fieldId { DCGM_FI_DEV_CPU_POWER_UTIL_CURRENT, DCGM_FI_DEV_CPU_POWER_LIMIT };
    std::unordered_map<unsigned short, dcgmcm_sample_t> currValue {};

    /* Get the value at the endTime*/
    for (auto field : fieldId)
    {
        ret = mpCoreProxy.GetLatestSample(entityGroupId, entityId, field, &currValue[field], 0);

        if (DCGM_ST_NO_DATA == ret)
            return DCGM_ST_OK;
        if (DCGM_ST_OK != ret)
            return ret;
        if (DCGM_INT64_IS_BLANK(currValue[field].val.i64))
            return DCGM_ST_OK;
    }

    // If the sample is over the power limit (FAIL)
    if (currValue[DCGM_FI_DEV_CPU_POWER_UTIL_CURRENT].val.d >= currValue[DCGM_FI_DEV_CPU_POWER_LIMIT].val.d)
    {
        DcgmError d { entityId };
        DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_FIELD_THRESHOLD_DBL, d, entityId);
        SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_POWER, d, response);
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorNVLink(dcgm_field_entity_group_t entityGroupId,
                                            dcgm_field_eid_t entityId,
                                            long long startTime,
                                            long long endTime,
                                            DcgmHealthResponse &response)
{
    dcgmReturn_t ret                                                   = DCGM_ST_OK;
    unsigned short fieldIds[DCGM_HEALTH_WATCH_NVLINK_ERROR_NUM_FIELDS] = { 0 };
    dcgmcm_sample_t startValue                                         = {};
    dcgmcm_sample_t endValue                                           = {};
    int count                                                          = 0;

    /* Various NVLink error counters to be monitored */
    fieldIds[0] = DCGM_FI_DEV_NVLINK_CRC_FLIT_ERROR_COUNT_TOTAL;
    fieldIds[1] = DCGM_FI_DEV_NVLINK_CRC_DATA_ERROR_COUNT_TOTAL;
    fieldIds[2] = DCGM_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_TOTAL;
    fieldIds[3] = DCGM_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_TOTAL;

    unsigned int oneMinuteInUsec = 60000000;
    timelib64_t now              = timelib_usecSince1970();

    /* Update the start and the end time if they are blank */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    for (unsigned int nvLinkField = 0; nvLinkField < DCGM_HEALTH_WATCH_NVLINK_ERROR_NUM_FIELDS; nvLinkField++)
    {
        count = 1;
        ret   = mpCoreProxy.GetSamples(entityGroupId,
                                     entityId,
                                     fieldIds[nvLinkField],
                                     &startValue,
                                     &count,
                                     startTime,
                                     endTime,
                                     DCGM_ORDER_ASCENDING);

        if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA && ret != DCGM_ST_NOT_WATCHED)
            return ret;

        /* If the field is not supported, continue with others */
        if (ret == DCGM_ST_NO_DATA || startValue.val.i64 == DCGM_INT64_NOT_SUPPORTED
            || DCGM_INT64_IS_BLANK(startValue.val.i64))
            continue;

        count = 1;
        ret   = mpCoreProxy.GetSamples(entityGroupId,
                                     entityId,
                                     fieldIds[nvLinkField],
                                     &endValue,
                                     &count,
                                     startTime,
                                     endTime,
                                     DCGM_ORDER_DESCENDING);

        if (ret != DCGM_ST_OK && ret != DCGM_ST_NO_DATA)
            return ret;

        /* Continue with other fields if this value is BLANK or has no data  */
        if (ret == DCGM_ST_NO_DATA || DCGM_INT64_IS_BLANK(endValue.val.i64))
            continue;

        // NO DATA is handled automatically so here we can assume we have the values from the last minute
        // both values have been checked for BLANK values so can be used here

        int64_t nvLinkError = (startValue.val.i64 >= endValue.val.i64) ? (startValue.val.i64 - endValue.val.i64)
                                                                       : (endValue.val.i64 - startValue.val.i64);

        if (nvLinkError >= DCGM_LIMIT_MAX_NVLINK_ERROR)
        {
            dcgm_field_meta_p fm = DcgmFieldGetById(fieldIds[nvLinkField]);
            char fieldTag[128];
            dcgmHealthWatchResults_t res = DCGM_HEALTH_RESULT_WARN;
            DcgmError d { entityId };

            if (fm != NULL)
            {
                snprintf(fieldTag, sizeof(fieldTag), "%s", fm->tag);
            }
            else
            {
                snprintf(fieldTag, sizeof(fieldTag), "Unknown field %hu", fieldIds[nvLinkField]);
            }


            if ((fieldIds[nvLinkField] == DCGM_FI_DEV_NVLINK_REPLAY_ERROR_COUNT_TOTAL)
                || (fieldIds[nvLinkField] == DCGM_FI_DEV_NVLINK_RECOVERY_ERROR_COUNT_TOTAL))
            {
                // Replay and recovery errors are failures, not warnings.
                res = DCGM_HEALTH_RESULT_FAIL;
                DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVLINK_ERROR_CRITICAL, d, nvLinkError, fieldTag, entityId);
            }
            else
            {
                // CRC errors are only an error if more than 100 are happening per second
                double timeDiffInSec;
                if (endTime == 0)
                {
                    // Use now as the end time
                    timeDiffInSec = (now - startTime) / 1000000.0;
                }
                else
                {
                    timeDiffInSec = (endTime - startTime) / 1000000.0;
                }
                double perSec = static_cast<double>(nvLinkError) / timeDiffInSec;
                if (perSec >= DCGM_LIMIT_MAX_NVLINK_CRC_ERROR)
                {
                    res = DCGM_HEALTH_RESULT_FAIL;
                    DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVLINK_CRC_ERROR_THRESHOLD, d, perSec, fieldTag, entityId);
                }
                else
                {
                    DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVLINK_ERROR_THRESHOLD,
                                              d,
                                              nvLinkError,
                                              fieldTag,
                                              entityId,
                                              DCGM_LIMIT_MAX_NVLINK_ERROR);
                }
            }

            SetResponse(entityGroupId, entityId, res, DCGM_HEALTH_WATCH_NVLINK, d, response);
        }
    }


    /* See if any links are down */
    dcgmNvLinkLinkState_t linkStates[DCGM_NVLINK_MAX_LINKS_PER_GPU];
    ret = mpCoreProxy.GetEntityNvLinkLinkStatus(DCGM_FE_GPU, entityId, linkStates);
    if (ret != DCGM_ST_OK)
    {
        log_error("Got error {} from GetEntityNvLinkLinkStatus gpuId {}", (int)ret, entityId);
        return ret;
    }
    for (int i = 0; i < DCGM_NVLINK_MAX_LINKS_PER_GPU; i++)
    {
        if (linkStates[i] == DcgmNvLinkLinkStateDown)
        {
            DcgmError d { entityId };
            DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVLINK_DOWN, d, entityId, i);
            SetResponse(entityGroupId, entityId, DCGM_HEALTH_RESULT_FAIL, DCGM_HEALTH_WATCH_NVLINK, d, response);
        }
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
dcgmReturn_t DcgmHealthWatch::MonitorNvSwitchErrorCounts(bool fatal,
                                                         dcgm_field_entity_group_t entityGroupId,
                                                         dcgm_field_eid_t entityId,
                                                         long long startTime,
                                                         long long endTime,
                                                         DcgmHealthResponse &response)
{
    std::vector<unsigned int>::iterator fieldIdIter;
    dcgmReturn_t dcgmReturn;
    std::vector<unsigned int> *fieldIds;
    dcgmcm_sample_t sample = {};
    dcgmHealthWatchResults_t healthWatchResult;
    dcgmHealthSystems_t healthWatchSystems;
    std::string errorTypeString;

    unsigned int oneMinuteInUsec = 60000000;
    timelib64_t now              = timelib_usecSince1970();

    /* Update the start and the end time if they are blank */
    if (!startTime)
    {
        startTime = now - oneMinuteInUsec;
    }

    /* Note: Allow endTime to be in the future. 0 = blank = most recent record in time series */

    if (fatal)
    {
        fieldIds           = &m_nvSwitchFatalFieldIds;
        healthWatchResult  = DCGM_HEALTH_RESULT_FAIL;
        healthWatchSystems = DCGM_HEALTH_WATCH_NVSWITCH_FATAL;
        errorTypeString    = "fatal";
    }
    else /* Non-fatal */
    {
        fieldIds           = &m_nvSwitchNonFatalFieldIds;
        healthWatchResult  = DCGM_HEALTH_RESULT_WARN;
        healthWatchSystems = DCGM_HEALTH_WATCH_NVSWITCH_NONFATAL;
        errorTypeString    = "nonfatal";
    }

    memset(&sample, 0, sizeof(sample));

    for (fieldIdIter = fieldIds->begin(); fieldIdIter != fieldIds->end(); ++fieldIdIter)
    {
        int count  = 1;
        dcgmReturn = mpCoreProxy.GetSamples(
            entityGroupId, entityId, *fieldIdIter, &sample, &count, startTime, endTime, DCGM_ORDER_DESCENDING);
        if (dcgmReturn != DCGM_ST_OK)
        {
            log_debug("return {} for GetSamples eg {}, eid {}, fieldId {}, start {}, end {}",
                      (int)dcgmReturn,
                      entityGroupId,
                      entityId,
                      *fieldIdIter,
                      startTime,
                      endTime);
            continue;
        }

        if (!DCGM_INT64_IS_BLANK(sample.val.i64) && sample.val.i64 > 0)
        {
            unsigned int linkId = (*fieldIdIter) - fieldIds->at(0);
            DcgmError d { entityId };
            if (fatal)
            {
                DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVSWITCH_FATAL_ERROR, d, entityId, linkId);
            }
            else
            {
                DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVSWITCH_NON_FATAL_ERROR, d, entityId, linkId);
            }

            SetResponse(entityGroupId, entityId, healthWatchResult, healthWatchSystems, d, response);
        }
    }

    /* See if any links are down. Only do this for the fatal case so we don't get duplicate errors for both fatal and
     * non-fatal */
    if (fatal)
    {
        dcgmNvLinkLinkState_t linkStates[DCGM_NVLINK_MAX_LINKS_PER_NVSWITCH];
        dcgmReturn = mpCoreProxy.GetEntityNvLinkLinkStatus(DCGM_FE_SWITCH, entityId, linkStates);
        if (dcgmReturn != DCGM_ST_OK)
        {
            log_error("Got error {} from GetEntityNvLinkLinkStatus eid {}", (int)dcgmReturn, entityId);
            return dcgmReturn;
        }
        for (int i = 0; i < DCGM_NVLINK_MAX_LINKS_PER_NVSWITCH; i++)
        {
            if (linkStates[i] == DcgmNvLinkLinkStateDown)
            {
                DcgmError d { entityId };
                DCGM_ERROR_FORMAT_MESSAGE(DCGM_FR_NVLINK_DOWN, d, entityId, i);
                SetResponse(entityGroupId, entityId, healthWatchResult, healthWatchSystems, d, response);
            }
        }
    }

    return DCGM_ST_OK;
}

/*****************************************************************************/
void DcgmHealthWatch::OnGroupRemove(unsigned int groupId)
{
    groupWatchTable_t::iterator groupWatchIter;

    dcgm_mutex_lock(m_mutex);

    groupWatchIter = mGroupWatchState.find(groupId);
    if (groupWatchIter == mGroupWatchState.end())
    {
        log_debug("OnGroupRemove didn't find groupId {}", groupId);
    }
    else
    {
        mGroupWatchState.erase(groupWatchIter);
        log_debug("OnGroupRemove found and removed groupId {}", groupId);
    }

    dcgm_mutex_unlock(m_mutex);
}

/*****************************************************************************/
void DcgmHealthWatch::ProcessXidFv(dcgmBufferedFv_t *fv)
{
    switch (fv->value.i64)
    {
        case 95: /* Uncontained error */
        {
            DCGM_LOG_ERROR << "gpuId " << fv->entityId << " hit fatal XID " << fv->value.i64;
            DcgmLockGuard dlg(m_mutex);
            m_gpuHadUncontainedErrorXid.insert(fv->entityId);
            break;
        }

        default:
            DCGM_LOG_DEBUG << "Ignored XID " << fv->value.i64 << " for gpuId " << fv->entityId;
            break;
    }
}

/*****************************************************************************/
void DcgmHealthWatch::OnFieldValuesUpdate(DcgmFvBuffer *fvBuffer)
{
    dcgmBufferedFv_t *fv;
    dcgmBufferedFvCursor_t cursor = 0;

    /* This is a bit coarse-grained for now, but it's clean */
    dcgmMutexReturn_t mutexSt = dcgm_mutex_lock(m_mutex);

    for (fv = fvBuffer->GetNextFv(&cursor); fv; fv = fvBuffer->GetNextFv(&cursor))
    {
        /* Policy only pertains to GPUs for now */
        if (fv->entityGroupId != DCGM_FE_GPU)
        {
            log_debug("Ignored non-GPU eg {}", fv->entityGroupId);
            continue;
        }

        switch (fv->fieldId)
        {
            case DCGM_FI_DEV_XID_ERRORS:
                ProcessXidFv(fv);
                break;

            default:
                /* This is partially expected since the cache manager will broadcast
                   any FVs that updated during the same loop as FVs we care about */
                DCGM_LOG_DEBUG << "Ignoring unhandled field " << fv->fieldId;
                break;
        }
    }

    if (mutexSt != DCGM_MUTEX_ST_LOCKEDBYME)
        dcgm_mutex_unlock(m_mutex);
}

/*****************************************************************************/
