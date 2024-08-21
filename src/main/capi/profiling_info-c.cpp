#include "duckdb/main/capi/capi_internal.hpp"

using duckdb::Connection;
using duckdb::DuckDB;
using duckdb::EnumUtil;
using duckdb::MetricsType;
using duckdb::optional_ptr;
using duckdb::ProfilingInfo;
using duckdb::ProfilingNode;
using duckdb::ProfilingNodeWrapper;
using duckdb::string;
using duckdb::vector;

vector<string> GetSettingsAsVector(const ProfilingInfo &info) {
	vector<string> keys;

	for (auto &setting : info.settings) {
		keys.push_back(EnumUtil::ToString(setting));
	}
	return keys;
}

duckdb_profiling_info duckdb_get_profiling_info(duckdb_connection connection) {
	if (!connection) {
		return nullptr;
	}
	Connection *conn = reinterpret_cast<Connection *>(connection);
	optional_ptr<ProfilingNode> profiling_node;
	try {
		profiling_node = conn->GetProfilingTree();
	} catch (std::exception &ex) {
		return nullptr;
	}

	if (!profiling_node) {
		return nullptr;
	}

	auto wrapper = new ProfilingNodeWrapper();
	wrapper->profiling_node = profiling_node;
	wrapper->keys = GetSettingsAsVector(profiling_node->GetProfilingInfo());

	return reinterpret_cast<duckdb_profiling_info>(wrapper);
}

void duckdb_profiling_info_destroy(duckdb_profiling_info *info) {
	if (!info || !*info) {
		return;
	}

	auto *wrapper = reinterpret_cast<ProfilingNodeWrapper *>(*info);
	if (wrapper) {
		delete wrapper;
	}
	*info = nullptr;
}

idx_t duckdb_profiling_info_get_key_count(duckdb_profiling_info info) {
	auto wrapper = reinterpret_cast<ProfilingNodeWrapper *>(info);

	return wrapper->keys.size();
}

duckdb_value duckdb_profiling_info_get_key(duckdb_profiling_info info, idx_t key_idx) {
	auto wrapper = reinterpret_cast<ProfilingNodeWrapper *>(info);

	return duckdb_create_varchar(wrapper->keys[key_idx].c_str());
}

duckdb_value duckdb_profiling_info_get_value(duckdb_profiling_info info, const char *key) {
	if (!info) {
		return nullptr;
	}

	auto wrapper = reinterpret_cast<ProfilingNodeWrapper *>(info);

	auto &profiling_info = wrapper->profiling_node->GetProfilingInfo();
	auto key_enum = EnumUtil::FromString<MetricsType>(duckdb::StringUtil::Upper(key));
	if (!profiling_info.Enabled(key_enum)) {
		return nullptr;
	}

	auto str = profiling_info.GetMetricAsString(key_enum);
	return duckdb_create_varchar_length(str.c_str(), strlen(str.c_str()));
}

idx_t duckdb_profiling_info_get_child_count(duckdb_profiling_info info) {
	if (!info) {
		return 0;
	}
	auto wrapper = reinterpret_cast<ProfilingNodeWrapper *>(info);
	return wrapper->profiling_node->GetChildCount();
}

duckdb_profiling_info duckdb_profiling_info_get_child(duckdb_profiling_info info, idx_t index) {
	if (!info) {
		return nullptr;
	}
	auto *wrapper = reinterpret_cast<ProfilingNodeWrapper *>(info);
	if (index >= wrapper->profiling_node->GetChildCount()) {
		return nullptr;
	}

	auto new_wrapper = new ProfilingNodeWrapper();
	new_wrapper->profiling_node = wrapper->profiling_node->GetChild(index);
	new_wrapper->keys = GetSettingsAsVector(new_wrapper->profiling_node->GetProfilingInfo());

	return reinterpret_cast<duckdb_profiling_info>(new_wrapper);
}
