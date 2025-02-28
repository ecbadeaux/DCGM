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
/*
 * Diag.h
 *
 *  Created on: Oct 13, 2015
 *      Author: chris
 */

#ifndef DIAG_H_
#define DIAG_H_

#include "Command.h"
#include "CommandOutputController.h"
#include "yaml-cpp/yaml.h"
#include "json/json.h"
#include <dcgm_structs_internal.h>

#include <DcgmThread.h>

#define STOP_DIAG_ENV_VARIABLE_NAME "DCGMI_STOP_DIAG_HOSTNAME"

class Diag
{
public:
    Diag(unsigned int iterations, const std::string &hostname);
    virtual ~Diag();
    dcgmReturn_t RunStartDiag(dcgmHandle_t mNvcmHandle);
    dcgmReturn_t RunViewDiag();

    void setDcgmRunDiag(dcgmRunDiag_t *drd);
    void setJsonOutput(bool jsonOutput);
    std::string Sanitize(const std::string &toOutput);
    void DisplayVerboseInfo(CommandOutputController &cmdView, const std::string &name, const std::string &info);

    // Made public for unit tests
    std::string HelperGetPluginName(unsigned int index);
    void HelperJsonBuildOutput(Json::Value &output,
                               dcgmDiagResponse_t &diagResult,
                               const std::vector<unsigned int> &gpuIndices);
    bool HelperJsonAddResult(dcgmDiagResponsePerGpu_v5 &gpuResult,
                             Json::Value &testEntry,
                             unsigned int gpuIndex,
                             unsigned int testIndex,
                             size_t i);
    void HelperJsonAddBasicTests(Json::Value &output, int &headerIndex, dcgmDiagResponse_t &diagResult);
    dcgmReturn_t GetFailureResult(dcgmDiagResponse_t &diagResult);
    void PopulateGpuList(const dcgmDiagResponse_t &diagResult, std::vector<unsigned int> &gpuVec);
    void InitializeDiagResponse(dcgmDiagResponse_t &diagResult);

private:
    void HelperDisplayDeployment(dcgmDiagResponse_t &diagResult);
    void HelperDisplayHardware(dcgmDiagResponsePerGpu_v5 *diagResults, const std::vector<unsigned int> &gpuIndices);
    void HelperDisplayIntegration(dcgmDiagResponsePerGpu_v5 *diagResults, const std::vector<unsigned int> &gpuIndices);
    void HelperDisplayPerformance(dcgmDiagResponsePerGpu_v5 *diagResults, const std::vector<unsigned int> &gpuIndices);
    void HelperDisplayDeploymentResult(CommandOutputController &cmdView,
                                       const std::string &nameTag,
                                       dcgmDiagTestResult_v3 &result);
    std::string HelperDisplayDiagResult(dcgmDiagResult_t val);

    void HelperDisplayGpuResults(std::string dataName,
                                 unsigned int testIndex,
                                 dcgmDiagResponsePerGpu_v5 *diagResults,
                                 const std::vector<unsigned int> &gpuIndices);

    dcgmReturn_t HelperDisplayAsJson(dcgmDiagResponse_t &diagResult, const std::vector<unsigned int> &gpuIndices);
    void HelperJsonAddPlugin(Json::Value &category, int &pluginCount, Json::Value &testEntry);
    void HelperJsonAddCategory(Json::Value &output, int &categoryIndex, Json::Value &category, int categoryCount);
    void HelperDisplayDetails(bool forceVerbose,
                              const std::vector<unsigned int> &gpuIndices,
                              unsigned int testIndex,
                              CommandOutputController &cmdView,
                              dcgmDiagResponsePerGpu_v5 *diagResults);

    void HelperDisplayVersionAndDevIds(dcgmDiagResponse_t &diagResult);

    /*****************************************************************************/
    /*
     * Runs the diag one time and returns the result
     *
     * @return DCGM_ST_OK if the diagnostic found no problems
     *         DCGM_ST_* indicating what error was found
     */
    dcgmReturn_t RunDiagOnce(dcgmHandle_t handle);

    /*****************************************************************************/
    dcgmReturn_t ExecuteDiagOnServer(dcgmHandle_t handle, dcgmDiagResponse_t &diagResult);

    /*
     * Displays a complete failure message for the diag accounting for JSON or normal output
     *
     */
    void HelperDisplayFailureMessage(const std::string &errMsg, dcgmReturn_t ret);

    bool isWhitespace(char c);

    dcgmRunDiag_t m_drd;
    bool m_jsonOutput;
    unsigned int m_iterations;
    std::string m_hostname;
    // This is only used if we're running iteratively
    Json::Value m_jsonTmpValue;
};

/*****************************************************************************
 * Make a simple class for launching the diagnostic in a thread so we can
 * monitor it and interrupt it as needed.
 *****************************************************************************/
class RemoteDiagExecutor : public DcgmThread
{
public:
    /*****************************************************************************/
    RemoteDiagExecutor(dcgmHandle_t handle, dcgmRunDiag_t &drd);

    /*****************************************************************************/
    void run(void);

    /*****************************************************************************/
    dcgmReturn_t GetResult() const;

    /*****************************************************************************/
    dcgmDiagResponse_t GetResponse() const;

private:
    dcgmHandle_t m_handle;
    dcgmRunDiag_t m_drd;
    dcgmDiagResponse_t m_diagResult;
    dcgmReturn_t m_result;
};

/**
 * Start Diagnostics Invoker
 */
class StartDiag : public Command
{
public:
    StartDiag(const std::string &hostname,
              const bool hostAddressWasOverridden,
              const std::string &parms,
              const std::string &configPath,
              bool jsonOutput,
              dcgmRunDiag_t &drd,
              unsigned int iterations,
              const std::string &pathToDcgmExecutable);

protected:
    dcgmReturn_t DoExecuteConnected() override;
    dcgmReturn_t DoExecuteConnectionFailure(dcgmReturn_t connectionStatus) override;

private:
    Diag m_diagObj;

    bool validGpuListFormat(const std::string &gpuList);

    /*
     * Makes the embedded host engine listen on a port so that DCGM Diag can talk to it successfully
     */
    dcgmReturn_t StartListenerServer();
};

/**
 * Abort Diagnostics Invoker
 */
class AbortDiag : public Command
{
public:
    AbortDiag(std::string hostname);

protected:
    dcgmReturn_t DoExecuteConnected() override;
};

#endif /* DIAG_H_ */
