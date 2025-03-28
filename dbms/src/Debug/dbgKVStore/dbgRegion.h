// Copyright 2024 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <Storages/KVStore/MultiRaft/RegionData.h>
#include <Storages/KVStore/MultiRaft/RegionMeta.h>
#include <Storages/KVStore/Region.h>

namespace DB::RegionBench
{
struct DebugRegion
{
    DebugRegion(RegionPtr region_ptr)
        : region(*region_ptr)
    {}
    RegionPtr debugSplitInto(RegionMeta && meta);
    RegionData & debugData();
    Region * operator->() { return &region; }
    Region * operator->() const { return &region; }

private:
    Region & region;
};
} // namespace DB::RegionBench
