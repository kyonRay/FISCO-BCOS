/**
 *  Copyright (C) 2024 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file test_FeaturesL2Hardfork.cpp
 * @brief L2 hardfork enum flags + sparse activation map (A6.15).
 */
#include <bcos-framework/ledger/Features.h>
#include <boost/test/unit_test.hpp>
#include <array>
#include <string_view>

using namespace bcos::ledger;

BOOST_AUTO_TEST_SUITE(FeaturesL2HardforkTest)

// The 9 OP-Stack hardfork flag names, in enum (== uint8 id) order.
static constexpr std::array<std::string_view, 9> c_l2HardforkNames = {"feature_l2_hardfork_bedrock",
    "feature_l2_hardfork_regolith", "feature_l2_hardfork_canyon", "feature_l2_hardfork_delta",
    "feature_l2_hardfork_ecotone", "feature_l2_hardfork_fjord", "feature_l2_hardfork_granite",
    "feature_l2_hardfork_holocene", "feature_l2_hardfork_isthmus"};

BOOST_AUTO_TEST_CASE(EachHardforkHasFlagAndStringName)
{
    for (size_t id = 0; id < c_l2HardforkNames.size(); ++id)
    {
        Features::Flag flag{};
        BOOST_CHECK_NO_THROW(flag = Features::string2Flag(c_l2HardforkNames[id]));
        // string2Flag must map to the contiguous enum range, id == offset from bedrock.
        auto expected = static_cast<Features::Flag>(
            static_cast<int>(Features::Flag::feature_l2_hardfork_bedrock) + static_cast<int>(id));
        BOOST_CHECK(flag == expected);
    }
}

BOOST_AUTO_TEST_CASE(ActivationBlockOfDefaultZero)
{
    Features features;
    for (auto name : c_l2HardforkNames)
    {
        BOOST_CHECK_EQUAL(features.activationBlockOf(Features::string2Flag(name)), 0U);
    }
}

BOOST_AUTO_TEST_CASE(ActivationBlockOfRoundTrip)
{
    Features features;
    auto flag = Features::Flag::feature_l2_hardfork_ecotone;
    BOOST_CHECK_EQUAL(features.activationBlockOf(flag), 0U);
    features.setActivationBlockOf(flag, 12345);
    BOOST_CHECK_EQUAL(features.activationBlockOf(flag), 12345U);
    // Setting one flag must not leak into a neighbour.
    BOOST_CHECK_EQUAL(features.activationBlockOf(Features::Flag::feature_l2_hardfork_fjord), 0U);
}

BOOST_AUTO_TEST_SUITE_END()
