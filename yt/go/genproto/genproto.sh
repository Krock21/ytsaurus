#!/bin/bash

set -e
set -o pipefail
set -x

if [[ -z "${GITHUB_WORKSPACE}" ]]; then
  GIT_WORK_DIR=$(git rev-parse --show-toplevel)
else
  GIT_WORK_DIR="${GITHUB_WORKSPACE}"
fi

(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/client/api/rpc_proxy -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/bus/proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/rpc/proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/tracing/proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/yson/proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/ytree/proto/attributes.proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/misc/proto/error.proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/core/misc/proto/guid.proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt/core/rpc/unittests/lib/test_service.proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/client/chaos_client/proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/client/chunk_client/proto/data_statistics.proto -iname "*.proto"))
(cd $GIT_WORK_DIR && protoc --go_opt=module=go.ytsaurus.tech --go_out=. -I ./yt $(find ./yt/yt_proto/yt/client/hive/proto/timestamp_map.proto -iname "*.proto"))
