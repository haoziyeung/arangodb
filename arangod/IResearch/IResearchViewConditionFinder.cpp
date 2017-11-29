////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2017 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#include "IResearchViewConditionFinder.h"
#include "IResearchViewNode.h"
#include "Aql/ExecutionPlan.h"
#include "Aql/IndexNode.h"
#include "Aql/SortCondition.h"
#include "Aql/SortNode.h"

namespace arangodb {
namespace iresearch {

using namespace arangodb::aql;
using EN = arangodb::aql::ExecutionNode;

bool IResearchViewConditionFinder::before(ExecutionNode* en) {
  switch (en->getType()) {
    case EN::LIMIT:
      // LIMIT invalidates the sort expression we already found
      _sorts.clear();
      _filters.clear();
      break;

    case EN::SINGLETON:
    case EN::NORESULTS:
      // in all these cases we better abort
      return true;

    case EN::FILTER: {
      std::vector<Variable const*> invars(en->getVariablesUsedHere());
      TRI_ASSERT(invars.size() == 1);
      // register which variable is used in a FILTER
      _filters.emplace(invars[0]->id);
      break;
    }

    case EN::SORT: {
      // register which variables are used in a SORT
      if (_sorts.empty()) {
        for (auto& it : static_cast<SortNode const*>(en)->getElements()) {
          _sorts.emplace_back(it.var, it.ascending);
          TRI_IF_FAILURE("ConditionFinder::sortNode") {
            THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
          }
        }
      }
      break;
    }

    case EN::CALCULATION: {
      auto outvars = en->getVariablesSetHere();
      TRI_ASSERT(outvars.size() == 1);

      _variableDefinitions.emplace(
          outvars[0]->id,
          static_cast<CalculationNode const*>(en)->expression()->node());
      TRI_IF_FAILURE("ConditionFinder::variableDefinition") {
        THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
      }
      break;
    }

    case EN::ENUMERATE_IRESEARCH_VIEW: {
      auto node = static_cast<iresearch::IResearchViewNode const*>(en);
      if (_changes->find(node->id()) != _changes->end()) {
        // already optimized this node
        break;
      }

      auto condition = std::make_unique<Condition>(_plan->getAst());

      if (!handleFilterCondition(en, condition)) {
        break;
      }

      std::unique_ptr<SortCondition> sortCondition;
      handleSortCondition(en, node->outVariable(), condition, sortCondition);

      if (condition->isEmpty() && sortCondition->isEmpty()) {
        // no filter conditions left
        break;
      }

      auto const canUseView = condition->checkView(
        node->view().get(), node->outVariable(), sortCondition.get()
      );

      if (canUseView.first && canUseView.second) {
        auto newNode = std::make_unique<iresearch::IResearchViewNode>(
          _plan,
          _plan->nextId(),
          node->vocbase(),
          node->view(),
          node->outVariable(),
          condition.get(),
          std::move(sortCondition)
        );
        condition.release();

        TRI_IF_FAILURE("ConditionFinder::insertViewNode") {
          THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
        }

        // We keep this node's change
        _changes->emplace(node->id(), newNode.get());
        newNode.release();
      } else {
        THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_QUERY_PARSE, "filter clause "
          "not yet supported with view");
      }

      break;
    }

    default:
      // in these cases we simply ignore the intermediate nodes, note
      // that we have taken care of nodes that could throw exceptions
      // above.
      break;
  }

  return false;
}

bool IResearchViewConditionFinder::handleFilterCondition(
    ExecutionNode* en,
    std::unique_ptr<Condition>& condition) {
  bool foundCondition = false;
  for (auto& it : _variableDefinitions) {
    if (_filters.find(it.first) != _filters.end()) {
      // a variable used in a FILTER
      AstNode* var = const_cast<AstNode*>(it.second);
      if (!var->canThrow() && var->isDeterministic() && var->isSimple()) {
        // replace all variables inside the FILTER condition with the
        // expressions represented by the variables
        var = it.second->clone(_plan->getAst());

        auto func = [&](AstNode* node, void* data) -> AstNode* {
          if (node->type == NODE_TYPE_REFERENCE) {
            auto plan = static_cast<ExecutionPlan*>(data);
            auto variable = static_cast<Variable*>(node->getData());

            if (variable != nullptr) {
              auto setter = plan->getVarSetBy(variable->id);

              if (setter != nullptr && setter->getType() == EN::CALCULATION) {
                auto s = static_cast<CalculationNode*>(setter);
                auto filterExpression = s->expression();
                AstNode* inNode = filterExpression->nodeForModification();
                if (!inNode->canThrow() && inNode->isDeterministic() &&
                    inNode->isSimple()) {
                  return inNode;
                }
              }
            }
          }
          return node;
        };

        var = Ast::traverseAndModify(var, func, _plan);
      }
      condition->andCombine(var);
      foundCondition = true;
    }
  }

  // normalize the condition
  condition->normalize(_plan);
  TRI_IF_FAILURE("ConditionFinder::normalizePlan") {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
  }

  bool const conditionIsImpossible = (foundCondition && condition->isEmpty());

  if (conditionIsImpossible) {
    // condition is always false
    for (auto const& x : en->getParents()) {
      auto noRes = new NoResultsNode(_plan, _plan->nextId());
      _plan->registerNode(noRes);
      _plan->insertDependency(x, noRes);
      *_hasEmptyResult = true;
    }
    return false;
  }

  auto const& varsValid = en->getVarsValid();

  // remove all invalid variables from the condition
  if (condition->removeInvalidVariables(varsValid)) {
    // removing left a previously non-empty OR block empty...
    // this means we can't use the index to restrict the results
    return false;
  }

  return true;
}

void IResearchViewConditionFinder::handleSortCondition(
    ExecutionNode* en,
    Variable const* outVar,
    std::unique_ptr<Condition>& condition,
    std::unique_ptr<SortCondition>& sortCondition) {
  if (!en->isInInnerLoop()) {
    // we cannot optimize away a sort if we're in an inner loop ourselves
    sortCondition.reset(new SortCondition(
        _plan, _sorts, condition->getConstAttributes(outVar, false),
        _variableDefinitions));
  } else {
    sortCondition.reset(new SortCondition());
  }
}

} // iresearch
} // arangodb