#include "catch.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/function/scalar/string_common.hpp"

using namespace duckdb;

// SubstringASCII/Unicode/Grapheme must agree exactly on ASCII input (bytes, codepoints and grapheme
// clusters coincide there). This file has had two independent bugs where that broke silently - sweep
// offset/length pairs, including the values that triggered them, as a standing regression gate.
TEST_CASE("substring ASCII/unicode/grapheme paths agree on ASCII input", "[substring]") {
	// mirrors SUPPORTED_UPPER_BOUND/SUPPORTED_LOWER_BOUND in substring.cpp (not exported, so
	// duplicated here as literals)
	const int64_t supported_upper_bound = 4294967295LL;
	const int64_t supported_lower_bound = -4294967296LL;

	const string str = "hello world!!";
	string_t input(str.c_str(), UnsafeNumericCast<uint32_t>(str.size()));

	const duckdb::vector<int64_t> offsets = {supported_lower_bound,
	                                         -12345678, // the exact value from the original clamping bug
	                                         -100,
	                                         -14,
	                                         -13,
	                                         -6,
	                                         -2,
	                                         -1,
	                                         0,
	                                         1,
	                                         2,
	                                         6,
	                                         7,
	                                         13,
	                                         14,
	                                         100,
	                                         supported_upper_bound};
	const duckdb::vector<int64_t> lengths = {supported_lower_bound, -100, -12, -5, -1, 1, 5, 12, 13, 100,
	                                         supported_upper_bound};

	Vector ascii_vec(LogicalType::VARCHAR);
	Vector unicode_vec(LogicalType::VARCHAR);
	Vector grapheme_vec(LogicalType::VARCHAR);

	for (auto offset : offsets) {
		for (auto length : lengths) {
			INFO("offset=" << offset << " length=" << length);
			auto ascii_result = SubstringASCII(ascii_vec, input, offset, length);
			auto unicode_result = SubstringUnicode(unicode_vec, input, offset, length);
			auto grapheme_result = SubstringGrapheme(grapheme_vec, input, offset, length);

			REQUIRE(ascii_result.GetString() == unicode_result.GetString());
			REQUIRE(unicode_result.GetString() == grapheme_result.GetString());
		}
	}
}
