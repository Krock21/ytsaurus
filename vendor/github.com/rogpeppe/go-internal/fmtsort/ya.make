GO_LIBRARY()

LICENSE(BSD-3-Clause)

VERSION(v1.13.1)

SRCS(
    mapelem.go
    sort.go
)

GO_TEST_SRCS(export_test.go)

GO_XTEST_SRCS(sort_test.go)

END()

RECURSE(
    gotest
)
