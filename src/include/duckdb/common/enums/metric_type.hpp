//-------------------------------------------------------------------------
//                         DuckDB
//
//
// duckdb/common/enums/metrics_type.hpp
// 
// This file is automatically generated by scripts/generate_metric_enums.py
// Do not edit this file manually, your changes will be overwritten
//-------------------------------------------------------------------------

#pragma once

#include "duckdb/common/constants.hpp"

namespace duckdb {

enum class MetricsType : uint8_t {
    QUERY_NAME,
    IDLE_THREAD_TIME,
    CPU_TIME,
    EXTRA_INFO,
    CUMULATIVE_CARDINALITY,
    OPERATOR_TYPE,
    OPERATOR_CARDINALITY,
    CUMULATIVE_ROWS_SCANNED,
    OPERATOR_ROWS_SCANNED,
    OPERATOR_TIMING,
    ALL_OPTIMIZERS,
    CUMULATIVE_OPTIMIZER_TIMING,
    OPTIMIZER_EXPRESSION_REWRITER_TIMING,
    OPTIMIZER_FILTER_PULLUP_TIMING,
    OPTIMIZER_FILTER_PUSHDOWN_TIMING,
    OPTIMIZER_CTE_FILTER_PUSHER_TIMING,
    OPTIMIZER_REGEX_RANGE_TIMING,
    OPTIMIZER_IN_CLAUSE_TIMING,
    OPTIMIZER_JOIN_ORDER_TIMING,
    OPTIMIZER_DELIMINATOR_TIMING,
    OPTIMIZER_UNNEST_REWRITER_TIMING,
    OPTIMIZER_UNUSED_COLUMNS_TIMING,
    OPTIMIZER_STATISTICS_PROPAGATION_TIMING,
    OPTIMIZER_COMMON_SUBEXPRESSIONS_TIMING,
    OPTIMIZER_COMMON_AGGREGATE_TIMING,
    OPTIMIZER_COLUMN_LIFETIME_TIMING,
    OPTIMIZER_BUILD_SIDE_PROBE_SIDE_TIMING,
    OPTIMIZER_LIMIT_PUSHDOWN_TIMING,
    OPTIMIZER_TOP_N_TIMING,
    OPTIMIZER_COMPRESSED_MATERIALIZATION_TIMING,
    OPTIMIZER_DUPLICATE_GROUPS_TIMING,
    OPTIMIZER_REORDER_FILTER_TIMING,
    OPTIMIZER_JOIN_FILTER_PUSHDOWN_TIMING,
    OPTIMIZER_EXTENSION_TIMING,
    OPTIMIZER_MATERIALIZED_CTE_TIMING,
};

} // namespace duckdb