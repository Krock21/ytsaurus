LIBRARY()

SRCS(
    columns_set.cpp
    GLOBAL sub_columns_fetching.cpp
    GLOBAL default_fetching.cpp
    constructor.cpp
    context.cpp
    fetch_steps.cpp
    fetched_data.cpp
    fetching.cpp
    iterator.cpp
    source.cpp
)

PEERDIR(
    contrib/ydb/core/tx/columnshard/engines/scheme
    yql/essentials/minikql
)

GENERATE_ENUM_SERIALIZATION(columns_set.h)

END()
