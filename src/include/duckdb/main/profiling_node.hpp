//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/main/profiling_node.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/deque.hpp"
#include "duckdb/common/enums/profiler_format.hpp"
#include "duckdb/common/pair.hpp"
#include "duckdb/common/profiler.hpp"
#include "duckdb/common/reference_map.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/unordered_map.hpp"
#include "duckdb/common/winapi.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "duckdb/execution/physical_operator.hpp"
#include "duckdb/main/profiling_info.hpp"

namespace duckdb {

enum class ProfilingNodeType : uint8_t { QUERY_ROOT, OPERATOR };

class QueryProfilingNode;

// Recursive tree that mirrors the operator tree
class ProfilingNode {
public:
	virtual ~ProfilingNode() {};

private:
	ProfilingInfo profiling_info;

public:
	idx_t depth = 0;
	vector<unique_ptr<ProfilingNode>> children;
	ProfilingNodeType node_type = ProfilingNodeType::OPERATOR;

public:
	idx_t GetChildCount() {
		return children.size();
	}

	ProfilingInfo &GetProfilingInfo() {
		return profiling_info;
	}

	const ProfilingInfo &GetProfilingInfo() const {
		return profiling_info;
	}

	optional_ptr<ProfilingNode> GetChild(idx_t idx) {
		return children[idx].get();
	}

	optional_ptr<ProfilingNode> AddChild(unique_ptr<ProfilingNode> child) {
		children.push_back(std::move(child));
		return children.back().get();
	}

	//	template <class CHILD_TYPE>
	//	CHILD_TYPE GetChildMetric(idx_t idx, MetricsType metric) {
	//        auto &child = children[idx];
	//        if (!child) {
	//            throw InternalException("Failed to get child metric - child not found");
	//        }
	//		switch (metric) {
	//        case MetricsType::CPU_TIME:
	//            return child->GetProfilingInfo().metrics.cpu_time;
	//        case MetricsType::EXTRA_INFO:
	//            return child->GetProfilingInfo().metrics.extra_info;
	//        case MetricsType::CUMULATIVE_CARDINALITY:
	//            return child->GetProfilingInfo().metrics.cumulative_cardinality;
	//        case MetricsType::OPERATOR_CARDINALITY:
	//            return child->GetProfilingInfo().metrics.operator_cardinality;
	//        case MetricsType::OPERATOR_TIMING:
	//            return child->GetProfilingInfo().metrics.operator_timing;
	//		}
	//        return CHILD_TYPE();
	//    }

	template <class TARGET>
	TARGET &Cast() {
		if (node_type != TARGET::TYPE) {
			throw InternalException("Failed to cast ProfilingNode - node type mismatch");
		}
		return reinterpret_cast<TARGET &>(*this);
	}

	template <class TARGET>
	const TARGET &Cast() const {
		if (node_type != TARGET::TYPE) {
			throw InternalException("Failed to cast ProfilingNode - node type mismatch");
		}
		return reinterpret_cast<const TARGET &>(*this);
	}
};

// Holds the top level query info
class QueryProfilingNode : public ProfilingNode {
public:
	~QueryProfilingNode() override {};

public:
	static constexpr const ProfilingNodeType TYPE = ProfilingNodeType::QUERY_ROOT;

	string query;
};

class OperatorProfilingNode : public ProfilingNode {
public:
	~OperatorProfilingNode() override {};

public:
	static constexpr const ProfilingNodeType TYPE = ProfilingNodeType::OPERATOR;

	PhysicalOperatorType type;
	string name;
};

} // namespace duckdb
