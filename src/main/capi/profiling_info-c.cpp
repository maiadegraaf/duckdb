#include "duckdb/main/capi/capi_internal.hpp"

using duckdb::Connection;
using duckdb::DatabaseData;
using duckdb::DBConfig;
using duckdb::DuckDB;
using duckdb::ErrorData;

duckdb_profiling_info duckdb_get_profiling_info(duckdb_connection connection) {
	if (!connection) {
		return nullptr;
	}
	Connection *conn = reinterpret_cast<Connection *>(connection);
	auto profiling_info = conn->GetProfilingTree();
	return reinterpret_cast<duckdb_profiling_info>(profiling_info);
}

duckdb_profiling_info duckdb_get_query_info(duckdb_connection connection) {
	if (!connection) {
		return nullptr;
	}
	Connection *conn = reinterpret_cast<Connection *>(connection);
	auto query_info = conn->GetProfilingTree();
	return reinterpret_cast<duckdb_profiling_info>(query_info);
}
