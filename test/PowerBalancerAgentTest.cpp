/*
 * Copyright (c) 2015, 2016, 2017, 2018, 2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY LOG OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <memory>

#include "gtest/gtest.h"
#include "gmock/gmock.h"

#include "MockPowerGovernor.hpp"
#include "MockPowerBalancer.hpp"
#include "MockPlatformIO.hpp"
#include "MockPlatformTopo.hpp"
#include "PowerBalancerAgent.hpp"
#include "Helper.hpp"
#include "geopm_test.hpp"

#include "config.h"

using geopm::PowerBalancerAgent;
using geopm::PlatformTopo;
using ::testing::_;
using ::testing::SetArgReferee;
using ::testing::ContainerEq;
using ::testing::Return;
using ::testing::Sequence;
using ::testing::InvokeWithoutArgs;
using ::testing::InSequence;
using ::testing::AtLeast;

bool is_format_double(std::function<std::string(double)> func);
bool is_format_float(std::function<std::string(double)> func);
bool is_format_integer(std::function<std::string(double)> func);
bool is_format_hex(std::function<std::string(double)> func);
bool is_format_raw64(std::function<std::string(double)> func);


class PowerBalancerAgentTest : public ::testing::Test
{
    protected:
        void SetUp();

        enum {
            M_SIGNAL_EPOCH_COUNT,
            M_SIGNAL_EPOCH_RUNTIME,
            M_SIGNAL_EPOCH_RUNTIME_NETWORK,
            M_SIGNAL_EPOCH_RUNTIME_IGNORE,
        };

        MockPlatformIO m_platform_io;
        MockPlatformTopo m_platform_topo;
        std::unique_ptr<MockPowerGovernor> m_power_gov;
        std::unique_ptr<MockPowerBalancer> m_power_bal;
        std::unique_ptr<PowerBalancerAgent> m_agent;

        const double M_POWER_PACKAGE_MIN = 50;
        const double M_POWER_PACKAGE_TDP = 300;
        const double M_POWER_PACKAGE_MAX = 325;
        const int M_NUM_PKGS = 2;
        const std::vector<int> M_FAN_IN = {2, 2};
};

void PowerBalancerAgentTest::SetUp()
{
    m_power_gov = geopm::make_unique<MockPowerGovernor>();
    m_power_bal = geopm::make_unique<MockPowerBalancer>();

    ON_CALL(m_platform_io, read_signal("POWER_PACKAGE_TDP", GEOPM_DOMAIN_BOARD, 0))
        .WillByDefault(Return(M_POWER_PACKAGE_TDP));
    ON_CALL(m_platform_io, read_signal("POWER_PACKAGE_MIN", GEOPM_DOMAIN_BOARD, 0))
        .WillByDefault(Return(M_POWER_PACKAGE_MIN));
    ON_CALL(m_platform_io, read_signal("POWER_PACKAGE_MAX", GEOPM_DOMAIN_BOARD, 0))
        .WillByDefault(Return(M_POWER_PACKAGE_MAX));
    ON_CALL(m_platform_io, read_signal("POWER_PACKAGE_MAX", GEOPM_DOMAIN_PACKAGE, _))
        .WillByDefault(Return(M_POWER_PACKAGE_MAX/M_NUM_PKGS));

}

TEST_F(PowerBalancerAgentTest, power_balancer_agent)
{
    const std::vector<std::string> exp_pol_names = {"POWER_PACKAGE_LIMIT_TOTAL",
                                                    "STEP_COUNT",
                                                    "MAX_EPOCH_RUNTIME",
                                                    "POWER_SLACK"};
    const std::vector<std::string> exp_smp_names = {"STEP_COUNT",
                                                    "MAX_EPOCH_RUNTIME",
                                                    "SUM_POWER_SLACK",
                                                    "MIN_POWER_HEADROOM"};
    MockPowerGovernor *power_gov_p = m_power_gov.get();
    m_agent = geopm::make_unique<PowerBalancerAgent>(m_platform_io, m_platform_topo,
                                                     std::move(m_power_gov), std::move(m_power_bal));

    EXPECT_EQ("power_balancer", m_agent->plugin_name());
    EXPECT_EQ(exp_pol_names, m_agent->policy_names());
    EXPECT_EQ(exp_smp_names, m_agent->sample_names());
    m_agent->report_header();
    m_agent->report_host();
    m_agent->report_region();
    m_agent->wait();

    // check that single-node balancer can be initialized
    EXPECT_CALL(m_platform_topo, num_domain(_)).Times(AtLeast(1));
    EXPECT_CALL(*power_gov_p, init_platform_io());
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_RUNTIME", _, _));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_COUNT", _, _));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_RUNTIME_NETWORK", _, _));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_RUNTIME_IGNORE", _, _));
    m_agent->init(0, {}, false);
}

TEST_F(PowerBalancerAgentTest, tree_root_agent)
{
    const bool IS_ROOT = true;
    int level = 2;
    int num_children = M_FAN_IN[level - 1];

    ON_CALL(m_platform_io, read_signal("POWER_PACKAGE_MIN", GEOPM_DOMAIN_BOARD, 0))
        .WillByDefault(Return(50));
    ON_CALL(m_platform_io, read_signal("POWER_PACKAGE_MAX", GEOPM_DOMAIN_BOARD, 0))
        .WillByDefault(Return(200));

    EXPECT_CALL(m_platform_io, control_domain_type("POWER_PACKAGE_LIMIT"))
        .WillOnce(Return(GEOPM_DOMAIN_PACKAGE));
    EXPECT_CALL(m_platform_topo, num_domain(GEOPM_DOMAIN_PACKAGE))
        .WillOnce(Return(2));

    m_agent = geopm::make_unique<PowerBalancerAgent>(m_platform_io, m_platform_topo,
                                                     std::move(m_power_gov), std::move(m_power_bal));
    m_agent->init(level, M_FAN_IN, IS_ROOT);

    std::vector<double> in_policy {NAN, NAN, NAN, NAN};
    std::vector<std::vector<double> > exp_out_policy;

    std::vector<std::vector<double> > in_sample;
    std::vector<double> exp_out_sample;

    std::vector<std::vector<double> > out_policy = std::vector<std::vector<double> >(num_children, {NAN, NAN, NAN, NAN});
    std::vector<double> out_sample = {NAN, NAN, NAN, NAN};

#ifdef GEOPM_DEBUG
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->adjust_platform(in_policy), GEOPM_ERROR_LOGIC, "was called on non-leaf agent");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->sample_platform(out_sample), GEOPM_ERROR_LOGIC, "was called on non-leaf agent");
    std::vector<double> trace_data;
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->trace_values(trace_data), GEOPM_ERROR_LOGIC, "was called on non-leaf agent");
#endif

    int ctl_step = 0;
    double curr_cap = 300;
    double curr_cnt = (double) ctl_step;
    double curr_epc = 0.0;
    double curr_slk = 0.0;
    double curr_hrm = 0.0;
    bool exp_descend_ret = true;
    bool exp_ascend_ret  = true;
    bool desc_ret;
    bool ascend_ret;
    /// M_STEP_SEND_DOWN_LIMIT
    {
    in_policy = {curr_cap, curr_cnt, curr_epc, curr_slk};
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {curr_cap, curr_cnt, curr_epc, curr_slk});
    in_sample = std::vector<std::vector<double> >(num_children, {(double)ctl_step, curr_epc, curr_slk, curr_hrm});
    exp_out_sample = {(double)ctl_step, curr_epc, curr_slk, curr_hrm};

#ifdef GEOPM_DEBUG
    std::vector<std::vector<double> > inv_out_policy = {};
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->split_policy({}, out_policy), GEOPM_ERROR_LOGIC, "policy vectors are not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->split_policy(in_policy, inv_out_policy), GEOPM_ERROR_LOGIC, "policy vectors are not correctly sized.");
#endif
    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));

#ifdef GEOPM_DEBUG
    std::vector<double> inv_out_sample = {};
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->aggregate_sample({}, out_sample), GEOPM_ERROR_LOGIC, "sample vectors not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->aggregate_sample(in_sample, inv_out_sample), GEOPM_ERROR_LOGIC, "sample vectors not correctly sized.");
#endif
    m_agent->aggregate_sample(in_sample, out_sample);
    ascend_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_ascend_ret, ascend_ret);
    EXPECT_THAT(out_sample, ContainerEq(exp_out_sample));
    }

    ctl_step = 1;
    curr_cnt = (double) ctl_step;

    /// M_STEP_MEASURE_RUNTIME
    {
    in_policy = {curr_cap, 0.0, 0.0, 0.0};
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {0.0, curr_cnt, curr_epc, curr_slk});
    curr_epc = 22.0;
    in_sample = std::vector<std::vector<double> >(num_children, {(double)ctl_step, curr_epc, curr_slk, curr_hrm});
    exp_out_sample = {(double)ctl_step, curr_epc, curr_slk, curr_hrm};

    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));

    m_agent->aggregate_sample(in_sample, out_sample);
    ascend_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_ascend_ret, ascend_ret);
    EXPECT_THAT(out_sample, ContainerEq(exp_out_sample));
    }

    ctl_step = 2;
    curr_cnt = (double) ctl_step;

    /// M_STEP_REDUCE_LIMIT
    {
    in_policy = {curr_cap, 0.0, 0.0, 0.0};
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {0.0, curr_cnt, curr_epc, curr_slk});
    curr_slk = 9.0;
    in_sample = std::vector<std::vector<double> >(num_children, {(double)ctl_step, curr_epc, curr_slk, curr_hrm});
    exp_out_sample = {(double)ctl_step, curr_epc, num_children * curr_slk, curr_hrm};///@todo update when/if updated to use unique child sample inputs

    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));

    m_agent->aggregate_sample(in_sample, out_sample);
    ascend_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_ascend_ret, ascend_ret);
    EXPECT_THAT(out_sample, ContainerEq(exp_out_sample));
    }

    ctl_step = 3;
    curr_cnt = (double) ctl_step;
    curr_slk = 0.0;///@todo
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {0.0, curr_cnt, curr_epc, curr_slk});

    /// M_STEP_SEND_DOWN_LIMIT
    {
    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));
    }
}

TEST_F(PowerBalancerAgentTest, tree_agent)
{
    const bool IS_ROOT = false;
    int level = 1;
    int num_children = M_FAN_IN[level - 1];

    m_agent = geopm::make_unique<PowerBalancerAgent>(m_platform_io, m_platform_topo,
                                                     std::move(m_power_gov), std::move(m_power_bal));
    m_agent->init(level, M_FAN_IN, IS_ROOT);

    std::vector<double> in_policy {NAN, NAN, NAN, NAN};
    std::vector<std::vector<double> > exp_out_policy;

    std::vector<std::vector<double> > in_sample;
    std::vector<double> exp_out_sample;

    std::vector<std::vector<double> > out_policy = std::vector<std::vector<double> >(num_children, {NAN, NAN, NAN, NAN});
    std::vector<double> out_sample = {NAN, NAN, NAN, NAN};

#ifdef GEOPM_DEBUG
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->adjust_platform(in_policy), GEOPM_ERROR_LOGIC, "was called on non-leaf agent");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->sample_platform(out_sample), GEOPM_ERROR_LOGIC, "was called on non-leaf agent");
    std::vector<double> trace_data;
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->trace_values(trace_data), GEOPM_ERROR_LOGIC, "was called on non-leaf agent");
#endif

    int ctl_step = 0;
    double curr_cap = 300;
    double curr_cnt = (double) ctl_step;
    double curr_epc = 0.0;
    double curr_slk = 0.0;
    double curr_hrm = 0.0;
    bool exp_descend_ret = true;
    bool exp_ascend_ret  = true;
    bool desc_ret;
    bool ascend_ret;
    /// M_STEP_SEND_DOWN_LIMIT
    {
    in_policy = {curr_cap, curr_cnt, curr_epc, curr_slk};
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {curr_cap, curr_cnt, curr_epc, curr_slk});
    in_sample = std::vector<std::vector<double> >(num_children, {(double)ctl_step, curr_epc, curr_slk, curr_hrm});
    exp_out_sample = {(double)ctl_step, curr_epc, 0.0, 0.0};

#ifdef GEOPM_DEBUG
    std::vector<std::vector<double> > inv_out_policy = {};
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->split_policy({}, out_policy), GEOPM_ERROR_LOGIC, "policy vectors are not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->split_policy(in_policy, inv_out_policy), GEOPM_ERROR_LOGIC, "policy vectors are not correctly sized.");
#endif
    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));

#ifdef GEOPM_DEBUG
    std::vector<double> inv_out_sample = {};
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->aggregate_sample({}, out_sample), GEOPM_ERROR_LOGIC, "sample vectors not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->aggregate_sample(in_sample, inv_out_sample), GEOPM_ERROR_LOGIC, "sample vectors not correctly sized.");
#endif
    m_agent->aggregate_sample(in_sample, out_sample);
    ascend_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_ascend_ret, ascend_ret);
    EXPECT_THAT(out_sample, ContainerEq(exp_out_sample));
    }

    ctl_step = 1;
    curr_cnt = (double) ctl_step;

    /// M_STEP_MEASURE_RUNTIME
    {
    in_policy = {0.0, curr_cnt, 0.0, 0.0};
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {0.0, curr_cnt, curr_epc, curr_slk});
    curr_epc = 22.0;
    in_sample = std::vector<std::vector<double> >(num_children, {(double)ctl_step, curr_epc, curr_slk, curr_hrm});
    exp_out_sample = {(double)ctl_step, curr_epc, curr_slk, curr_hrm};

    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));

    m_agent->aggregate_sample(in_sample, out_sample);
    ascend_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_ascend_ret, ascend_ret);
    EXPECT_THAT(out_sample, ContainerEq(exp_out_sample));
    }

    ctl_step = 2;
    curr_cnt = (double) ctl_step;

    /// M_STEP_REDUCE_LIMIT
    {
    in_policy = {0.0, curr_cnt, curr_epc, 0.0};
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {0.0, curr_cnt, curr_epc, curr_slk});
    curr_slk = 9.0;
    in_sample = std::vector<std::vector<double> >(num_children, {(double)ctl_step, curr_epc, curr_slk, curr_hrm});
    exp_out_sample = {(double)ctl_step, curr_epc, num_children * curr_slk, curr_hrm};///@todo update when/if updated to use unique child sample inputs

    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));

    m_agent->aggregate_sample(in_sample, out_sample);
    ascend_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_ascend_ret, ascend_ret);
    EXPECT_THAT(out_sample, ContainerEq(exp_out_sample));
    }

    ctl_step = 3;
    curr_cnt = (double) ctl_step;
    curr_slk /= num_children;
    exp_out_policy = std::vector<std::vector<double> >(num_children,
                                                       {0.0, curr_cnt, 0.0, curr_slk});

    /// M_STEP_SEND_DOWN_LIMIT
    {
    in_policy = {0.0, curr_cnt, 0.0, curr_slk};
    m_agent->split_policy(in_policy, out_policy);
    desc_ret = m_agent->do_send_policy();
    EXPECT_EQ(exp_descend_ret, desc_ret);
    EXPECT_THAT(out_policy, ContainerEq(exp_out_policy));
    }
}

TEST_F(PowerBalancerAgentTest, leaf_agent)
{
    const bool IS_ROOT = false;
    int level = 0;
    int num_children = 1;
    int counter = 0;
    std::vector<double> trace_vals(7, NAN);
    std::vector<double> exp_trace_vals(7, NAN);
    const std::vector<std::string> trace_cols {
        "POLICY_POWER_PACKAGE_LIMIT_TOTAL",
        "POLICY_STEP_COUNT",
        "POLICY_MAX_EPOCH_RUNTIME",
        "POLICY_POWER_SLACK",
        "EPOCH_RUNTIME",
        "POWER_LIMIT",
        "ENFORCED_POWER_LIMIT"};
    const std::vector<std::function<std::string(double)> > trace_formats {
        geopm::string_format_double,
        PowerBalancerAgent::format_step_count,
        geopm::string_format_double,
        geopm::string_format_double,
        geopm::string_format_double,
        geopm::string_format_double,
        geopm::string_format_double};

    std::vector<double> epoch_rt_mpi = {0.50, 0.75};
    std::vector<double> epoch_rt_ignore = {0.25, 0.27};
    std::vector<double> epoch_rt = {1.0, 1.01};

    EXPECT_CALL(m_platform_topo, num_domain(GEOPM_DOMAIN_PACKAGE))
        .WillOnce(Return(M_NUM_PKGS));
    EXPECT_CALL(m_platform_io, read_signal("POWER_PACKAGE_TDP", _, _));
    EXPECT_CALL(m_platform_io, read_signal("POWER_PACKAGE_MAX", GEOPM_DOMAIN_PACKAGE, _))
        .WillOnce(Return(M_POWER_PACKAGE_MAX));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_COUNT", GEOPM_DOMAIN_BOARD, 0))
        .WillOnce(Return(M_SIGNAL_EPOCH_COUNT));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_RUNTIME", GEOPM_DOMAIN_BOARD, 0))
        .WillOnce(Return(M_SIGNAL_EPOCH_RUNTIME));
    EXPECT_CALL(m_platform_io, sample(M_SIGNAL_EPOCH_COUNT))
        .WillRepeatedly(InvokeWithoutArgs([&counter]()
                {
                    return (double) ++counter;
                }));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_RUNTIME_NETWORK", GEOPM_DOMAIN_BOARD, 0))
        .WillOnce(Return(M_SIGNAL_EPOCH_RUNTIME_NETWORK));
    EXPECT_CALL(m_platform_io, push_signal("EPOCH_RUNTIME_IGNORE", GEOPM_DOMAIN_BOARD, 0))
        .WillOnce(Return(M_SIGNAL_EPOCH_RUNTIME_IGNORE));

    Sequence e_rt_pio_seq;
    for (size_t x = 0; x < epoch_rt.size(); ++x) {
        EXPECT_CALL(m_platform_io, sample(M_SIGNAL_EPOCH_RUNTIME))
            .InSequence(e_rt_pio_seq)
            .WillOnce(Return(epoch_rt[x]));
    }
    Sequence mpi_rt_pio_seq;
    for (size_t x = 0; x < epoch_rt_mpi.size(); ++x) {
        EXPECT_CALL(m_platform_io, sample(M_SIGNAL_EPOCH_RUNTIME_NETWORK))
            .InSequence(mpi_rt_pio_seq)
            .WillOnce(Return(epoch_rt_mpi[x]));
    }
    Sequence i_rt_pio_seq;
    for (size_t x = 0; x < epoch_rt_ignore.size(); ++x) {
        EXPECT_CALL(m_platform_io, sample(M_SIGNAL_EPOCH_RUNTIME_IGNORE))
            .InSequence(i_rt_pio_seq)
            .WillOnce(Return(epoch_rt_ignore[x]));
    }

    m_power_gov = geopm::make_unique<MockPowerGovernor>();
    EXPECT_CALL(*m_power_gov, init_platform_io());
    EXPECT_CALL(*m_power_gov, sample_platform())
        .Times(4);
    double actual_limit = 299.0 / M_NUM_PKGS;
    EXPECT_CALL(*m_power_gov, adjust_platform(300.0, _))
        .Times(4)
        .WillRepeatedly(SetArgReferee<1>(actual_limit));
    EXPECT_CALL(*m_power_gov, do_write_batch())
        .Times(4)
        .WillRepeatedly(Return(true));
    m_power_bal = geopm::make_unique<MockPowerBalancer>();
    EXPECT_CALL(*m_power_bal, power_limit_adjusted(actual_limit))
        .Times(4);

    EXPECT_CALL(*m_power_bal, target_runtime(epoch_rt[0]));
    EXPECT_CALL(*m_power_bal, calculate_runtime_sample()).Times(2);
    EXPECT_CALL(*m_power_bal, runtime_sample())
        .WillRepeatedly(Return(epoch_rt[0]));
    double exp_in = epoch_rt[0] - epoch_rt_mpi[0] - epoch_rt_ignore[0];
    EXPECT_CALL(*m_power_bal, is_runtime_stable(exp_in))
        .WillOnce(Return(true));
    exp_in = epoch_rt[1] - epoch_rt_mpi[1] - epoch_rt_ignore[1];
    EXPECT_CALL(*m_power_bal, is_target_met(exp_in))
        .WillRepeatedly(Return(true));
    EXPECT_CALL(*m_power_bal, power_slack())
        .WillRepeatedly(Return(0.0));
    EXPECT_CALL(*m_power_bal, power_cap(300.0))
        .Times(2);
    EXPECT_CALL(*m_power_bal, power_cap())
        .WillRepeatedly(Return(300.0));
    EXPECT_CALL(*m_power_bal, power_limit())
        .WillRepeatedly(Return(300.0));
    EXPECT_CALL(*m_power_bal, power_slack());
    m_agent = geopm::make_unique<PowerBalancerAgent>(m_platform_io, m_platform_topo,
                                                     std::move(m_power_gov), std::move(m_power_bal));
    m_agent->init(level, M_FAN_IN, IS_ROOT);

    EXPECT_EQ(trace_cols, m_agent->trace_names());

    auto expect_it = trace_formats.begin();
    for (const auto &actual_it : m_agent->trace_formats()) {
        EXPECT_EQ(is_format_double(*expect_it), is_format_double(actual_it));
        EXPECT_EQ(is_format_float(*expect_it), is_format_float(actual_it));
        EXPECT_EQ(is_format_integer(*expect_it), is_format_integer(actual_it));
        EXPECT_EQ(is_format_hex(*expect_it), is_format_hex(actual_it));
        EXPECT_EQ(is_format_raw64(*expect_it), is_format_raw64(actual_it));
        ++expect_it;
    }
    auto fun = m_agent->trace_formats().at(1);

    EXPECT_EQ("0-STEP_SEND_DOWN_LIMIT", fun(0));
    EXPECT_EQ("0-STEP_MEASURE_RUNTIME", fun(1));
    EXPECT_EQ("0-STEP_REDUCE_LIMIT", fun(2));
    EXPECT_EQ("1-STEP_SEND_DOWN_LIMIT", fun(3));
    EXPECT_EQ("1-STEP_MEASURE_RUNTIME", fun(4));
    EXPECT_EQ("1-STEP_REDUCE_LIMIT", fun(5));

    std::vector<double> in_policy {NAN, NAN, NAN, NAN};

    std::vector<double> exp_out_sample;

    std::vector<std::vector<double> > out_policy = std::vector<std::vector<double> >(num_children, {NAN, NAN, NAN, NAN});
    std::vector<double> out_sample = {NAN, NAN, NAN, NAN};

#ifdef GEOPM_DEBUG
    std::vector<double> err_trace_vals, err_out_sample;
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->trace_values(err_trace_vals), GEOPM_ERROR_LOGIC, "values vector not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->adjust_platform({}), GEOPM_ERROR_LOGIC, "policy vectors are not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->sample_platform(err_out_sample), GEOPM_ERROR_LOGIC, "out_sample vector not correctly sized.");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->split_policy(in_policy, out_policy), GEOPM_ERROR_LOGIC, "was called on non-tree agent");
    GEOPM_EXPECT_THROW_MESSAGE(m_agent->aggregate_sample({}, out_sample), GEOPM_ERROR_LOGIC, "was called on non-tree agent");
#endif

    int ctl_step = 0;
    double curr_cap = 300;
    double curr_cnt = (double) ctl_step;
    double curr_epc = 0.0;
    double curr_slk = 0.0;
    bool exp_adj_plat_ret = true;
    bool exp_smp_plat_ret  = true;
    bool adj_ret;
    bool smp_ret;
    /// M_STEP_SEND_DOWN_LIMIT
    {
    in_policy = {curr_cap, curr_cnt, curr_epc, curr_slk};
    exp_out_sample = {(double)ctl_step, curr_epc, 0.0, 0.0};

    m_agent->adjust_platform(in_policy);
    adj_ret = m_agent->do_write_batch();
    EXPECT_EQ(exp_adj_plat_ret, adj_ret);

    m_agent->sample_platform(out_sample);
    smp_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_smp_plat_ret, smp_ret);
    EXPECT_EQ(out_sample, exp_out_sample);
    m_agent->trace_values(trace_vals);
    }

    ctl_step = 1;
    curr_cnt = (double) ctl_step;
    /// M_STEP_MEASURE_RUNTIME
    {
    in_policy = {0.0, curr_cnt, curr_epc, curr_slk};
    curr_epc = epoch_rt[ctl_step - 1];
    exp_out_sample = {(double)ctl_step, curr_epc, 0.0, 0.0};

    m_agent->adjust_platform(in_policy);
    adj_ret = m_agent->do_write_batch();
    EXPECT_EQ(exp_adj_plat_ret, adj_ret);

    m_agent->sample_platform(out_sample);
    smp_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_smp_plat_ret, smp_ret);
    EXPECT_EQ(out_sample, exp_out_sample);
    }

    ctl_step = 2;
    curr_cnt = (double) ctl_step;
    /// M_STEP_REDUCE_LIMIT
    {
    in_policy = {0.0, curr_cnt, curr_epc, curr_slk};
    exp_out_sample = {(double)ctl_step, curr_epc, 0.0, 350.0};

    m_agent->adjust_platform(in_policy);
    adj_ret = m_agent->do_write_batch();
    EXPECT_EQ(exp_adj_plat_ret, adj_ret);

    m_agent->sample_platform(out_sample);
    smp_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_smp_plat_ret, smp_ret);
    EXPECT_EQ(out_sample, exp_out_sample);
    }

    ctl_step = 3;
    curr_cnt = (double) ctl_step;
    /// M_STEP_SEND_DOWN_LIMIT
    {
    in_policy = {0.0, curr_cnt, curr_epc, curr_slk};
    exp_out_sample = {(double)ctl_step, curr_epc, 0.0, 350.0};

    m_agent->adjust_platform(in_policy);
    adj_ret = m_agent->do_write_batch();
    EXPECT_EQ(exp_adj_plat_ret, adj_ret);

    m_agent->sample_platform(out_sample);
    smp_ret = m_agent->do_send_sample();
    EXPECT_EQ(exp_smp_plat_ret, smp_ret);
    EXPECT_EQ(out_sample, exp_out_sample);
    }
}

TEST_F(PowerBalancerAgentTest, enforce_policy)
{
    const double limit = 100;
    const std::vector<double> policy{limit, NAN, NAN, NAN};
    const std::vector<double> bad_policy{100};

    EXPECT_CALL(m_platform_io, control_domain_type("POWER_PACKAGE_LIMIT"))
        .WillOnce(Return(GEOPM_DOMAIN_PACKAGE));
    EXPECT_CALL(m_platform_topo, num_domain(GEOPM_DOMAIN_PACKAGE))
        .WillOnce(Return(M_NUM_PKGS));
    EXPECT_CALL(m_platform_io, write_control("POWER_PACKAGE_LIMIT", GEOPM_DOMAIN_BOARD,
                                             0, limit/M_NUM_PKGS));

    m_agent = geopm::make_unique<PowerBalancerAgent>(m_platform_io, m_platform_topo,
                                                     std::move(m_power_gov), std::move(m_power_bal));
    m_agent->enforce_policy(policy);

    EXPECT_THROW(m_agent->enforce_policy(bad_policy), geopm::Exception);
}

TEST_F(PowerBalancerAgentTest, validate_policy)
{
    m_agent = geopm::make_unique<PowerBalancerAgent>(m_platform_io, m_platform_topo,
                                                     std::move(m_power_gov), std::move(m_power_bal));

    std::vector<double> policy;

    // valid policy unchanged
    policy = {100};
    m_agent->validate_policy(policy);
    EXPECT_EQ(100, policy[0]);

    // NAN becomes default
    policy = {NAN};
    m_agent->validate_policy(policy);
    EXPECT_EQ(M_POWER_PACKAGE_TDP, policy[0]);

    // clamp to min
    policy = {M_POWER_PACKAGE_MIN - 1};
    m_agent->validate_policy(policy);
    EXPECT_EQ(M_POWER_PACKAGE_MIN, policy[0]);

    // clamp to max
    policy = {M_POWER_PACKAGE_MAX + 1};
    m_agent->validate_policy(policy);
    EXPECT_EQ(M_POWER_PACKAGE_MAX, policy[0]);

}
