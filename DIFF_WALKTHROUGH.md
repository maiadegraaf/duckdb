# Walkthrough: qualified column names in EXPLAIN / profiling output

Goal: `SET detailed_identifiers=true` makes ambiguous column names in join
conditions print as `t1.b` / `t2.b` instead of just `b`, in both `EXPLAIN`
and profiling `extra_info` JSON. Default is `false` — output is unchanged
unless you opt in.

11 files, ~85 lines. Read in this order — each step only makes sense given
the one before it.

---

## 1. Where the text comes from

`PhysicalBlockwiseNLJoin::ParamsToString()` (`src/execution/operator/join/physical_blockwise_nl_join.cpp:219`) builds the `"Condition"` field like this:

```cpp
result["Condition"] = condition->GetName().GetIdentifierName();
```

`GetName()` falls back to `ToString()` when there's no explicit alias, and
`ToString()` recurses down the expression tree. For a plain column, that
bottoms out in `BoundColumnRefExpression::ToString()` (or, later in the
pipeline, `BoundReferenceExpression::ToString()` — see step 5). Both of
those only ever printed the bare column name (`b`), because neither type
carried any record of *which table* the column came from — only a
`ColumnBinding{table_index, column_index}`, which is an internal planner
index, not a name.

So the fix has two parts: (a) remember the table alias at bind time and
carry it through the pipeline, (b) use it in `ToString()`, gated by a flag
so nothing changes by default.

---

## 2. Add a place to store the table alias

`src/include/duckdb/planner/expression/bound_columnref_expression.hpp`

```diff
+	//! The alias of the binding this column was resolved from (e.g. "t1"), if known.
+	//! Used to disambiguate identically-named columns from different tables in profiling/EXPLAIN output.
+	void SetTableAlias(Identifier table_alias_p) {
+		table_alias = std::move(table_alias_p);
+	}
+	const Identifier &TableAlias() const {
+		return table_alias;
+	}
 ...
 private:
 	ColumnBinding binding;
 	idx_t depth;
+	//! The alias of the binding this column was resolved from, if known (not serialized).
+	Identifier table_alias;
```

Just a new optional field with a getter/setter. Not serialized — it's
display-only, doesn't need to survive a round trip through disk.

---

## 3. Populate it where columns get bound

`src/planner/table_binding.cpp` — two call sites, `Binding::Bind` and
`TableBinding::Bind` (the latter is the one that actually fires for plain
table columns, which is what the original bug report used):

```diff
 	ColumnBinding binding = GetColumnBinding(column_index);
-	return BindResult(make_uniq<BoundColumnRefExpression>(Identifier(colref.GetName()), col_type, binding, depth));
+	auto result = make_uniq<BoundColumnRefExpression>(Identifier(colref.GetName()), col_type, binding, depth);
+	result->SetTableAlias(GetAlias());
+	return BindResult(std::move(result));
```

`GetAlias()` here is `Binding::GetAlias()` — the alias of the *table
binding* itself (e.g. `t1`), not the column. This is the one place in the
binder that actually knows which table a column came from, so it's the
only place that needs to change.

---

## 4. Use it in `ToString()` — but gated

`src/planner/expression/bound_columnref_expression.cpp`:

```diff
 	if (!alias.empty()) {
+		if (DBConfigOptions::detailed_identifiers && !table_alias.empty()) {
+			return table_alias.GetIdentifierName() + "." + alias.GetIdentifierName();
+		}
 		return alias.GetIdentifierName();
 	}
```

`DBConfigOptions::detailed_identifiers` is a new static flag (see step 7).
Without it, this is a no-op change — existing behavior and existing tests
are untouched. Why gated: an earlier version of this fix qualified
unconditionally, and broke ~3 tests elsewhere in the codebase (dynamic
filter / prefix-range-filter tests) that regex-match unqualified column
names in `EXPLAIN ANALYZE` output. `ToString()` is used far more widely
than just join `extra_info`, so an unconditional change has a much bigger
blast radius than the original ask.

Also: `Copy()` needs to preserve the new field, or it gets silently dropped
the first time the optimizer clones the expression (which happens a lot):

```diff
 unique_ptr<Expression> BoundColumnRefExpression::Copy() const {
-	return make_uniq<BoundColumnRefExpression>(alias, return_type, binding, depth);
+	auto result = make_uniq<BoundColumnRefExpression>(alias, return_type, binding, depth);
+	result->table_alias = table_alias;
+	return std::move(result);
 }
```

This one bit me during development — I built steps 2-4 first, rebuilt,
and the output was still unqualified. Turned out `Copy()` was the leak:
the optimizer copies expressions constantly, and every copy silently
dropped `table_alias` since the old constructor call never passed it along.

---

## 5. The real gotcha: `ColumnBindingResolver`

Even with steps 2-4 done and `Copy()` fixed, output was *still*
unqualified. The reason: `BoundColumnRefExpression` isn't what actually
executes. Before physical plan generation, `ColumnBindingResolver` walks
the plan and replaces every `BoundColumnRefExpression` with a
`BoundReferenceExpression` — a physical chunk-index reference. That's the
object that ends up stored in the physical join operator and is what
`ParamsToString()` actually calls `ToString()` on.

`src/execution/column_binding_resolver.cpp`:

```diff
-			return make_uniq<BoundReferenceExpression>(expr.GetAlias(), expr.GetReturnType(), i);
+			auto result = make_uniq<BoundReferenceExpression>(expr.GetAlias(), expr.GetReturnType(), i);
+			result->SetTableAlias(expr.TableAlias());
+			return std::move(result);
```

Before this line, only `alias` (the bare column name) was carried across
into the new `BoundReferenceExpression`; `table_alias` was dropped on the
floor at exactly this conversion. This is the step that actually explains
why the naive version of the fix didn't work — I found it by adding
temporary `fprintf` debug statements at each layer and noticing
`BoundColumnRefExpression::ToString()` was never even being called during
the query, which meant the object itself had already been replaced by the
time profiling ran.

---

## 6. Mirror the same three changes onto `BoundReferenceExpression`

Since that's the type actually rendered, it needs the same field, the same
`Copy()` fix, and the same gated `ToString()` logic as
`BoundColumnRefExpression` got in steps 2 and 4:

`src/include/duckdb/planner/expression/bound_reference_expression.hpp` —
add `table_alias` field + accessors (same shape as step 2).

`src/planner/expression/bound_reference_expression.cpp`:

```diff
 	if (!alias.empty()) {
+		if (DBConfigOptions::detailed_identifiers && !table_alias.empty()) {
+			return table_alias.GetIdentifierName() + "." + alias.GetIdentifierName();
+		}
 		return alias.GetIdentifierName();
 	}
 	return "#" + to_string(index);
 }
 ...
 unique_ptr<Expression> BoundReferenceExpression::Copy() const {
-	return make_uniq<BoundReferenceExpression>(alias, return_type, index);
+	auto result = make_uniq<BoundReferenceExpression>(alias, return_type, index);
+	result->table_alias = table_alias;
+	return std::move(result);
 }
```

---

## 7. Wire up the `detailed_identifiers` setting

Three files, following the exact pattern DuckDB already uses for
`allow_persistent_secrets` (a global boolean setting backed by a static
field rather than per-connection state — appropriate here since
`ToString()` has no access to a `ClientContext`).

**`src/include/duckdb/main/config.hpp`** — new static field on
`DBConfigOptions`, next to the existing (debug-only) `debug_print_bindings`:

```diff
 	static bool debug_print_bindings; // NOLINT: debug setting
+	static bool detailed_identifiers; // NOLINT: debug setting
```

**`src/main/config.cpp`** — default value, and the registration in the
settings table (this file is autogenerated from `settings.json` below, via
`python3 scripts/generate_settings.py`):

```diff
 bool DBConfigOptions::debug_print_bindings = false;
 #endif
+bool DBConfigOptions::detailed_identifiers = false;
...
     DUCKDB_SETTING(DelimJoinAsCteSetting),
+    DUCKDB_GLOBAL(DetailedIdentifiersSetting),
     DUCKDB_SETTING_CALLBACK(DialectCompatibilityModeSetting),
```

**`src/common/settings.json`** — the source of truth the generator reads:

```json
{
    "name": "detailed_identifiers",
    "description": "Qualify column names with their originating table alias (e.g. t1.b) in EXPLAIN and profiling output, to disambiguate identically-named columns from different tables",
    "type": "BOOLEAN",
    "scope": "global",
    "custom_implementation": true
}
```

`custom_implementation: true` means the generator only emits the struct
declaration (in `src/include/duckdb/main/settings.hpp`, also
autogenerated) — the actual get/set logic is hand-written:

**`src/main/settings/custom_settings.cpp`**:

```cpp
void DetailedIdentifiersSetting::SetGlobal(DatabaseInstance *db, DBConfig &config, const Value &input) {
	auto value = input.DefaultCastAs(LogicalType::BOOLEAN);
	DBConfigOptions::detailed_identifiers = value.GetValue<bool>();
}

void DetailedIdentifiersSetting::ResetGlobal(DatabaseInstance *db, DBConfig &config) {
	DBConfigOptions::detailed_identifiers = false;
}

Value DetailedIdentifiersSetting::GetSetting(const ClientContext &context) {
	return Value::BOOLEAN(DBConfigOptions::detailed_identifiers);
}
```

Note: `src/include/duckdb/main/settings.hpp` and `src/main/config.cpp` are
normally regenerated *and reformatted* by `make generate-files`, which
pipes through `clang-format`. This environment only had a mismatched
`clang-format` version available, so the generator's raw (space-indented)
output was reverted and the new struct/entry were hand-inserted with the
correct tab-indented style instead — otherwise the diff would have touched
~2800 unrelated lines of pure reformatting.

---

## Net effect

```sql
-- default: unchanged
SELECT t1.a, t2.b FROM table1 t1, table2 t2
WHERE concat(left(t2.b,1), left(t1.b,2)) = 'abc';
-- Condition: (concat("left"(b, 1), "left"(b, 2)) = 'abc')

SET detailed_identifiers=true;
-- same query now:
-- Condition: (concat("left"(t2.b, 1), "left"(t1.b, 2)) = 'abc')
```

Verified against `test/sql/join/*`, `test/sql/explain/*`, `test/sql/pragma/*`
— all pass with the flag off (default).

## Known gap

`PhysicalHashJoin::ParamsToString()` (the `a = a` style equi-join condition
line) doesn't pick this up. Those `JoinCondition` left/right expressions get
rebuilt through separate paths in the join-order optimizer and filter
pushdown (`src/optimizer/join_order/relation_manager.cpp`,
`src/optimizer/pushdown/pushdown_inner_join.cpp`, etc.) that construct
fresh `BoundColumnRefExpression`s directly, bypassing both
`Binding::Bind()` and `Copy()` — so `table_alias` never gets attached in
the first place on that path. Out of scope for this prototype.
