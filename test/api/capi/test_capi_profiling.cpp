#include "capi_tester.hpp"

using namespace duckdb;
using namespace std;

string BuildProfilingSettingsString(const duckdb::vector<string> &settings) {
	string result = "'{";
	for (idx_t i = 0; i < settings.size(); i++) {
		result += "\"" + settings[i] + "\": \"true\"";
		if (i < settings.size() - 1) {
			result += ", ";
		}
	}
	result += "}'";
	return result;
}

void RetrieveAllMetrics(duckdb_profiling_info profiling_info, duckdb::map<string, double> &cumulative_counter,
                        duckdb::map<string, double> &cumulative_result) {
	auto count = duckdb_profiling_info_get_key_count(profiling_info);
	for (idx_t i = 0; i < count; i++) {
		auto key = duckdb_profiling_info_get_key(profiling_info, i);
		auto c_key = duckdb_get_varchar(key);
		auto value = duckdb_profiling_info_get_value(profiling_info, c_key);
		if (value != nullptr) {
			if (!strcmp(c_key, "EXTRA_INFO")) {
				duckdb_destroy_value(&value);
				continue;
			}
			if (!strcmp(c_key, "QUERY_NAME") || !strcmp(c_key, "OPERATOR_TYPE")) {
				auto str_value = duckdb_get_varchar(value);
				REQUIRE(str_value != nullptr);
				duckdb_free(str_value);
			}
			double result = 0;
			try {
				auto str_value = duckdb_get_varchar(value);
				if (!strcmp(c_key, "QUERY_NAME") || !strcmp(c_key, "OPERATOR_TYPE")) {
					REQUIRE(str_value != nullptr);
				} else {
					result = std::stod(str_value);
				}
				duckdb_free(str_value);
			} catch (std::invalid_argument &e) {
				REQUIRE(false);
			}

			if (cumulative_counter.find(c_key) != cumulative_counter.end()) {
				cumulative_counter[c_key] += result;
			}

			// only take the root node's result
			if (cumulative_result.find(c_key) != cumulative_result.end() && cumulative_result[c_key] == 0) {
				cumulative_result[c_key] = result;
			}

			duckdb_destroy_value(&value);
			REQUIRE(result >= 0);
		}
		duckdb_destroy_value(&key);
		duckdb_free(c_key);
	}
}

// Traverse the tree and retrieve all metrics
void TraverseTree(duckdb_profiling_info profiling_info, duckdb::map<string, double> &cumulative_counter,
                  duckdb::map<string, double> &cumulative_result) {
	RetrieveAllMetrics(profiling_info, cumulative_counter, cumulative_result);

	// Recurse.
	auto child_count = duckdb_profiling_info_get_child_count(profiling_info);
	for (idx_t i = 0; i < child_count; i++) {
		auto child = duckdb_profiling_info_get_child(profiling_info, i);
		TraverseTree(child, cumulative_counter, cumulative_result);
		duckdb_profiling_info_destroy(&child);
	}
}

int ConvertToInt(double value) {
	return static_cast<int>(value * 1000);
}

TEST_CASE("Test Profiling with Single Metric", "[capi]") {
	CAPITester tester;
	duckdb::unique_ptr<CAPIResult> result;

	// open the database in in-memory mode
	REQUIRE(tester.OpenDatabase(nullptr));

	REQUIRE_NO_FAIL(tester.Query("PRAGMA enable_profiling = 'no_output'"));

	// test only CPU_TIME profiling
	duckdb::vector<string> settings = {"CPU_TIME"};
	REQUIRE_NO_FAIL(tester.Query("PRAGMA custom_profiling_settings=" + BuildProfilingSettingsString(settings)));

	REQUIRE_NO_FAIL(tester.Query("SELECT 42"));

	auto profiling_info = duckdb_get_profiling_info(tester.connection);
	REQUIRE(profiling_info != nullptr);

	// Retrieve metric that is not enabled
	REQUIRE(duckdb_profiling_info_get_value(profiling_info, "EXTRA_INFO") == nullptr);

	duckdb::map<string, double> cumulative_counter;
	duckdb::map<string, double> cumulative_result;

	TraverseTree(profiling_info, cumulative_counter, cumulative_result);
	duckdb_profiling_info_destroy(&profiling_info);

	// Cleanup
	tester.Cleanup();
}

TEST_CASE("Test Profiling with All Metrics", "[capi]") {
	CAPITester tester;
	duckdb::unique_ptr<CAPIResult> result;

	// open the database in in-memory mode
	REQUIRE(tester.OpenDatabase(nullptr));

	REQUIRE_NO_FAIL(tester.Query("PRAGMA enable_profiling = 'no_output'"));

	// test all profiling metrics
	duckdb::vector<string> settings = {"BLOCKED_THREAD_TIME",  "CPU_TIME",       "CUMULATIVE_CARDINALITY", "EXTRA_INFO",
	                                   "OPERATOR_CARDINALITY", "OPERATOR_TIMING"};
	REQUIRE_NO_FAIL(tester.Query("PRAGMA custom_profiling_settings=" + BuildProfilingSettingsString(settings)));

	REQUIRE_NO_FAIL(tester.Query("SELECT 42"));

	auto profiling_info = duckdb_get_profiling_info(tester.connection);
	REQUIRE(profiling_info != nullptr);

	duckdb::map<string, double> cumulative_counter = {{"OPERATOR_TIMING", 0}, {"OPERATOR_CARDINALITY", 0}};
	duckdb::map<string, double> cumulative_result {
	    {"CPU_TIME", 0},
	    {"CUMULATIVE_CARDINALITY", 0},
	};

	TraverseTree(profiling_info, cumulative_counter, cumulative_result);
	duckdb_profiling_info_destroy(&profiling_info);

	REQUIRE(ConvertToInt(cumulative_result["CPU_TIME"]) == ConvertToInt(cumulative_counter["OPERATOR_TIMING"]));
	REQUIRE(ConvertToInt(cumulative_result["CUMULATIVE_CARDINALITY"]) ==
	        ConvertToInt(cumulative_counter["OPERATOR_CARDINALITY"]));

	// Cleanup
	tester.Cleanup();
}

TEST_CASE("Test Profiling without Enabling Profiling", "[capi]") {
	CAPITester tester;
	duckdb::unique_ptr<CAPIResult> result;

	// open the database in in-memory mode
	REQUIRE(tester.OpenDatabase(nullptr));

	// Retrieve profiling info without enabling profiling
	auto profiling_info = duckdb_get_profiling_info(tester.connection);
	REQUIRE(profiling_info == nullptr);

	duckdb_profiling_info_destroy(&profiling_info);

	// Cleanup
	tester.Cleanup();
}

TEST_CASE("Test Profiling With Detailed Profiling Mode On", "[capi]") {
	CAPITester tester;
	duckdb::unique_ptr<CAPIResult> result;

	// open the database in in-memory mode
	REQUIRE(tester.OpenDatabase(nullptr));

	REQUIRE_NO_FAIL(tester.Query("PRAGMA enable_profiling = 'no_output'"));

	REQUIRE_NO_FAIL(tester.Query("PRAGMA profiling_mode = 'detailed'"));

	REQUIRE_NO_FAIL(tester.Query("SELECT 42"));

	auto profiling_info = duckdb_get_profiling_info(tester.connection);
	REQUIRE(profiling_info != nullptr);

	duckdb::map<string, double> cumulative_counter;
	duckdb::map<string, double> cumulative_result;
	TraverseTree(profiling_info, cumulative_counter, cumulative_result);
	duckdb_profiling_info_destroy(&profiling_info);

	// Cleanup
	tester.Cleanup();
}
