TEST_CASE("Test Profiling", "[capi]") {
	CAPITester tester;
	duckdb::unique_ptr<CAPIResult> result;

	// open the database in in-memory mode
	REQUIRE(tester.OpenDatabase(nullptr));

	REQUIRE_NO_FAIL(tester.Query("PRAGMA custom_profiling_settings='{\"CPU_TIME\": \"true\"}'"));

	result = tester.Query("SELECT 42");

	auto profiling_info = duckdb_get_profiling_info(tester.connection);

	auto query_info = duckdb_get_query_info(tester.connection);

//	REQUIRE_NO_FAIL(tester.Query("SET default_null_order='nulls_first'"));
//	// select scalar value
//	result = tester.Query("SELECT CAST(42 AS BIGINT)");
//	REQUIRE_NO_FAIL(*result);
//	REQUIRE(result->ColumnType(0) == DUCKDB_TYPE_BIGINT);
//	REQUIRE(result->ColumnData<int64_t>(0)[0] == 42);
//	REQUIRE(result->ColumnCount() == 1);
//	REQUIRE(result->row_count() == 1);
//	REQUIRE(result->Fetch<int64_t>(0, 0) == 42);
//	REQUIRE(!result->IsNull(0, 0));
//	// out of range fetch
//	REQUIRE(result->Fetch<int64_t>(1, 0) == 0);
//	REQUIRE(result->Fetch<int64_t>(0, 1) == 0);
//	// cannot fetch data chunk after using the value API
//	REQUIRE(result->FetchChunk(0) == nullptr);
}
