// Copyright 2023 PingCAP, Inc.
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

#include <DataStreams/TiRemoteBlockInputStream.h>
#include <Flash/Statistics/ConnectionProfileInfo.h>
#include <Flash/Statistics/TableScanImpl.h>
#include <Interpreters/Join.h>
#include <Storages/DeltaMerge/ReadThread/UnorderedInputStream.h>
#include <Storages/DeltaMerge/Remote/RNSegmentInputStream.h>
#include <Storages/DeltaMerge/ScanContext.h>

namespace DB
{
String TableScanTimeDetail::toJson() const
{
    auto max_cost_ms = max_stream_cost_ns < 0 ? 0 : max_stream_cost_ns / 1'000'000.0;
    auto min_cost_ms = min_stream_cost_ns < 0 ? 0 : min_stream_cost_ns / 1'000'000.0;
    return fmt::format(R"("max":{},"min":{})", max_cost_ms, min_cost_ms);
}
String LocalTableScanDetail::toJson() const
{
    return fmt::format(R"({{"is_local":true,"bytes":{},{}}})", bytes, time_detail.toJson());
}
String RemoteTableScanDetail::toJson() const
{
    return fmt::format(
        R"({{"{}":{{"packets":{},"bytes":{}}},"{}":{{"packets":{},"bytes":{}}},{}}})",
        inner_zone_conn_profile_info.getTypeString(),
        inner_zone_conn_profile_info.packets,
        inner_zone_conn_profile_info.bytes,
        inter_zone_conn_profile_info.getTypeString(),
        inter_zone_conn_profile_info.packets,
        inter_zone_conn_profile_info.bytes,
        time_detail.toJson());
}
void TableScanStatistics::appendExtraJson(FmtBuffer & fmt_buffer) const
{
    auto scan_ctx_it = dag_context.scan_context_map.find(executor_id);
    fmt_buffer.fmtAppend(
        R"("connection_details":[{},{}],"scan_details":{})",
        local_table_scan_detail.toJson(),
        remote_table_scan_detail.toJson(),
        scan_ctx_it != dag_context.scan_context_map.end() ? scan_ctx_it->second->toJson()
                                                          : "{}" // empty json object for nullptr
    );
}

void TableScanStatistics::updateTableScanDetail(const std::vector<ConnectionProfileInfo> & connection_profile_infos)
{
    for (const auto & connection_profile_info : connection_profile_infos)
    {
        if (connection_profile_info.type == ConnectionProfileInfo::InnerZoneRemote)
        {
            remote_table_scan_detail.inner_zone_conn_profile_info.packets += connection_profile_info.packets;
            remote_table_scan_detail.inner_zone_conn_profile_info.bytes += connection_profile_info.bytes;
        }
        else
        {
            remote_table_scan_detail.inter_zone_conn_profile_info.packets += connection_profile_info.packets;
            remote_table_scan_detail.inter_zone_conn_profile_info.bytes += connection_profile_info.bytes;
        }
        // Whether it's for remote read (non-disaggregated mode) or communication between CN and WN (disaggregated mode),
        // send bytes are always collected on the node that sends the request (i.e., the node where the table scan executor resides).
        // Therefore, both send bytes and receive bytes are collected here.
        base.updateSendConnectionInfo(connection_profile_info);
        base.updateReceiveConnectionInfo(connection_profile_info);
    }
}

void TableScanStatistics::updateTableScanDetailForDisaggIfNecessary(const IProfilingBlockInputStream * stream)
{
    if (const auto * unordered_stream = dynamic_cast<const DM::UnorderedInputStream *>(stream))
        updateTableScanDetail(unordered_stream->getConnectionProfileInfos());
    else if (const auto * rn_segment_stream = dynamic_cast<const DM::Remote::RNSegmentInputStream *>(stream))
        updateTableScanDetail(rn_segment_stream->getConnectionProfileInfos());
}

void TableScanStatistics::collectExtraRuntimeDetail()
{
    switch (dag_context.getExecutionMode())
    {
    case ExecutionMode::None:
        break;
    case ExecutionMode::Stream:
        transformInBoundIOProfileForStream(dag_context, executor_id, [&](const IBlockInputStream & stream) {
            if (const auto * cop_stream = dynamic_cast<const CoprocessorBlockInputStream *>(&stream); cop_stream)
            {
                /// remote read
                updateTableScanDetail(cop_stream->getConnectionProfileInfos());
                // TODO: Can not get the execution time of remote read streams?
            }
            else if (const auto * local_stream = dynamic_cast<const IProfilingBlockInputStream *>(&stream);
                     local_stream)
            {
                updateTableScanDetailForDisaggIfNecessary(local_stream);

                /// local read input stream also is IProfilingBlockInputStream
                const auto & prof = local_stream->getProfileInfo();
                local_table_scan_detail.bytes += prof.bytes;
                const double this_execution_time = prof.execution_time * 1.0;
                if (local_table_scan_detail.time_detail.max_stream_cost_ns < 0.0 // not inited
                    || local_table_scan_detail.time_detail.max_stream_cost_ns < this_execution_time)
                    local_table_scan_detail.time_detail.max_stream_cost_ns = this_execution_time;
                if (local_table_scan_detail.time_detail.min_stream_cost_ns < 0.0 // not inited
                    || local_table_scan_detail.time_detail.min_stream_cost_ns > this_execution_time)
                    local_table_scan_detail.time_detail.min_stream_cost_ns = this_execution_time;
            }
            else
            {
                /// Streams like: NullBlockInputStream.
            }
        });
        break;
    case ExecutionMode::Pipeline:
        transformInBoundIOProfileForPipeline(dag_context, executor_id, [&](const IOProfileInfo & profile_info) {
            if (profile_info.is_local)
            {
                local_table_scan_detail.bytes += profile_info.operator_info->bytes;
                const double this_execution_time = profile_info.operator_info->execution_time * 1.0;
                if (local_table_scan_detail.time_detail.max_stream_cost_ns < 0.0 // not inited
                    || local_table_scan_detail.time_detail.max_stream_cost_ns < this_execution_time)
                    local_table_scan_detail.time_detail.max_stream_cost_ns = this_execution_time;
                if (local_table_scan_detail.time_detail.min_stream_cost_ns < 0.0 // not inited
                    || local_table_scan_detail.time_detail.min_stream_cost_ns > this_execution_time)
                    local_table_scan_detail.time_detail.min_stream_cost_ns = this_execution_time;
            }
            else
            {
                updateTableScanDetail(profile_info.connection_profile_infos);
                const double this_execution_time = profile_info.operator_info->execution_time * 1.0;
                if (remote_table_scan_detail.time_detail.max_stream_cost_ns < 0.0 // not inited
                    || remote_table_scan_detail.time_detail.max_stream_cost_ns < this_execution_time)
                    remote_table_scan_detail.time_detail.max_stream_cost_ns = this_execution_time;
                if (remote_table_scan_detail.time_detail.min_stream_cost_ns < 0.0 // not inited
                    || remote_table_scan_detail.time_detail.max_stream_cost_ns > this_execution_time)
                    remote_table_scan_detail.time_detail.min_stream_cost_ns = this_execution_time;
            }
        });
        break;
    }

    if (auto it = dag_context.scan_context_map.find(executor_id); it != dag_context.scan_context_map.end())
    {
        it->second->setStreamCost(
            std::max(local_table_scan_detail.time_detail.min_stream_cost_ns, 0.0),
            std::max(local_table_scan_detail.time_detail.max_stream_cost_ns, 0.0),
            std::max(remote_table_scan_detail.time_detail.min_stream_cost_ns, 0.0),
            std::max(remote_table_scan_detail.time_detail.max_stream_cost_ns, 0.0));
    }
}

TableScanStatistics::TableScanStatistics(const tipb::Executor * executor, DAGContext & dag_context_)
    : TableScanStatisticsBase(executor, dag_context_)
{}
} // namespace DB
