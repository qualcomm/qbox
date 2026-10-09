/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 */

#ifndef _CONTAINER_BUILDER_TEST_H_
#define _CONTAINER_BUILDER_TEST_H_

#include <systemc>
#include <tlm>
#include <cciutils.h>
#include <argparser.h>
#include <tests/initiator-tester.h>
#include <tests/test-bench.h>
#include <container_builder.h>
#include <router.h>
#include <memory>

class ContainerBuilderTestBench : public TestBench
{
public:
    SCP_LOGGER();

    ContainerBuilderTestBench(const sc_core::sc_module_name& n);
    virtual ~ContainerBuilderTestBench() {}

public:
    std::unique_ptr<Container> m_platform;
    std::unique_ptr<ContainerDeferModulesConstruct> m_reuse_platform;
};

#endif
