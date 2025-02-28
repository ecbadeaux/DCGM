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
#ifndef TESTDCGMCONNECTIONS_H
#define TESTDCGMCONNECTIONS_H

#include "TestDcgmModule.h"
#include "dcgm_fields.h"
#include "timelib.h"

/*****************************************************************************/
class TestDcgmConnections : public TestDcgmModule
{
public:
    TestDcgmConnections();
    ~TestDcgmConnections();

    /*************************************************************************/
    /* Inherited methods from TestDcgmModule */
    int Init(const TestDcgmModuleInitParams &initParams) override;
    int Run();
    int Cleanup();
    std::string GetTag();

    /*************************************************************************/
private:
    /*************************************************************************/
    /*
     * Actual test cases. These should return a status like below
     *
     * Returns 0 on success
     *        <0 on fatal error. Will abort entire framework
     *        >0 on non-fatal error
     *
     **/
    int TestDeadlockSingle(void);
    int TestDeadlockMulti(void);
    int TestThrash(void);
    int TestIpcSocketPair(void);

    /*************************************************************************/
    /*
     * Helper for running a test.  Provided the test name, the return from running the
     * test, and a reference to the number of failed tests it will take care of printing
     * the appropriate pass/fail messages and increment the failed test count.
     *
     * An std::runtime_error is raised if the given testReturn was < 0, signifying a fatal error.
     */
    void CompleteTest(std::string testName, int testReturn, int &Nfailed);

    /*************************************************************************/
    /* Virtual method inherited from TestDcgmModule */
    bool IncludeInDefaultList(void)
    {
        return false;
    }

    /*************************************************************************/
};

/*****************************************************************************/

#endif // TESTDCGMMUTEX_H
