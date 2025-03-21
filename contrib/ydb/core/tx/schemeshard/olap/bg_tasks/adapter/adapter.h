#pragma once
#include <contrib/ydb/core/tx/columnshard/bg_tasks/templates/adapter.h>
#include <contrib/ydb/core/tx/schemeshard/schemeshard_schema.h>

namespace NKikimr::NSchemeShard::NBackground {

using TAdapter = NKikimr::NTx::NBackground::TAdapterTemplate<NSchemeShard::Schema>;

}