# Putting part of the query into a separate file

Here's the mechanism for putting part of the query into a separate attached file:

* [PRAGMA Library](pragma.md#library) marks the attached file as available for import.
## `Export`
* `EXPORT $my_symbol1, $my_symbol2, ...;` lists the names of named expressions in the library that are available for import.
## `Import`
* `IMPORT my_library SYMBOLS $my_symbol1, $my_symbol2, ...;` makes the listed named expressions available for further use.

{% note info "Note" %}

[Lambdas](expressions.md#lambda), [actions](action.md), [named subqueries](subquery.md), constant values, and expressions can be transferred to the library, but __subqueries or aggregate functions__ cannot be.

{% endnote %}

{% note warning "Warning" %}

The file linked by the [PRAGMA Library](pragma.md#library) must be attached to the query. __You can't use [PRAGMA File](pragma.md#file) for this purpose__.

{% endnote %}


**Examples:**

my_lib.sql:
```yql
$Square = ($x) -> { RETURN $x * $x; };
$Sqrt = ($x) -> { RETURN Math::Sqrt($x); };

-- Aggregation functions created using
-- AggregationFactory, convenient to transfer to the library
$Agg_sum = AggregationFactory("SUM");
$Agg_max = AggregationFactory("MAX");

EXPORT $Square, $Sqrt, $Agg_sum, $Agg_max.
```

Query:
```yql
PRAGMA Library("my_lib.sql");
IMPORT my_lib SYMBOLS $Square, $Sqrt, $Agg_sum, $Agg_max;
SELECT
  $Square(2), -- 4
  $Sqrt(4);   -- 2

SELECT
  AGGREGATE_BY(x, $Agg_sum), -- 5
  AGGREGATE_BY(x, $Agg_max)  -- 3
FROM (
  SELECT 2 AS x
  UNION ALL
  SELECT 3 AS x
)
```


