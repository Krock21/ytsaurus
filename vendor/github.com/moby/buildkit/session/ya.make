GO_LIBRARY()

LICENSE(Apache-2.0)

VERSION(v0.12.2)

SRCS(
    group.go
    grpc.go
    manager.go
    session.go
)

END()

RECURSE(
    content
    filesync
    grpchijack
    secrets
    sshforward
    testutil
)
