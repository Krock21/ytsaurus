#pragma once

#include <yql/essentials/core/expr_nodes/yql_expr_nodes.h>
#include <yql/essentials/providers/common/provider/yql_provider_names.h>
#include <contrib/ydb/library/yql/providers/generic/expr_nodes/yql_generic_expr_nodes.gen.h>

namespace NYql {
    namespace NNodes {

#include <contrib/ydb/library/yql/providers/generic/expr_nodes/yql_generic_expr_nodes.decl.inl.h>

        class TGenDataSource: public NGenerated::TGenDataSourceStub<TExprBase, TCallable, TCoAtom> {
        public:
            explicit TGenDataSource(const TExprNode* node)
                : TGenDataSourceStub(node)
            {
            }

            explicit TGenDataSource(const TExprNode::TPtr& node)
                : TGenDataSourceStub(node)
            {
            }

            static bool Match(const TExprNode* node) {
                if (!TGenDataSourceStub::Match(node)) {
                    return false;
                }

                if (node->Child(0)->Content() != GenericProviderName) {
                    return false;
                }

                return true;
            }
        };

#include <contrib/ydb/library/yql/providers/generic/expr_nodes/yql_generic_expr_nodes.defs.inl.h>

    } // namespace NNodes
} // namespace NYql
