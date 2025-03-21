LIBRARY()

SRCS(
    ydb_resources.cpp
    ydb_ca.cpp
)

RESOURCE(
    contrib/ydb/public/sdk/cpp/src/client/resources/ydb_sdk_version.txt ydb_sdk_version_dev.txt
    contrib/ydb/public/sdk/cpp/src/client/resources/ydb_root_ca.pem ydb_root_ca_dev.pem
)

END()
