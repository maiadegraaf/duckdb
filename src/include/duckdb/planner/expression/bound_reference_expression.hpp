//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/expression/bound_reference_expression.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/planner/expression.hpp"

namespace duckdb {

//! A BoundReferenceExpression represents a physical index into a DataChunk
class BoundReferenceExpression : public Expression {
public:
	static constexpr const ExpressionClass TYPE = ExpressionClass::BOUND_REF;

public:
    BoundReferenceExpression(Identifier alias, LogicalType type, idx_t index, Identifier table_alias = Identifier());

    BoundReferenceExpression(LogicalType type, storage_t index);

public:
	idx_t Index() const {
		return index;
	}
	idx_t &IndexMutable() {
		return index;
	}

    //! The alias of the binding this column was resolved from (e.g. "t1"), if known.
	//! Used to disambiguate identically-named columns from different tables in profiling/EXPLAIN output.
    const Identifier &TableAlias() const {
        return table_alias;
    }

    bool IsScalar() const override {
		return false;
	}
	bool IsFoldable() const override {
		return false;
	}

	string ToString() const override;

	hash_t Hash() const override;
	bool Equals(const BaseExpression &other) const override;

	unique_ptr<Expression> Copy() const override;

	void Serialize(Serializer &serializer) const override;
	static unique_ptr<Expression> Deserialize(Deserializer &deserializer);

private:
	//! Index used to access data in the chunks
	storage_t index;
    //! The alias of the binding this column was resolved from, if known (not serialized).
    Identifier table_alias;
};
} // namespace duckdb
