//
// Copyright rev.ng Labs Srl. See LICENSE.md for details.
//

#include "revng/Support/Debug.h"

#include "revng-c/RestructureCFG/ExprNode.h"

void ExprNode::deleteExprNode(ExprNode *E) {
  switch (E->getKind()) {
  case NodeKind::NK_ValueCompare:
    delete static_cast<ValueCompareNode *>(E);
    break;
  case NodeKind::NK_LoopStateCompare:
    delete static_cast<LoopStateCompareNode *>(E);
    break;
  case NodeKind::NK_Atomic:
    delete static_cast<AtomicNode *>(E);
    break;
  case NodeKind::NK_Not:
    delete static_cast<NotNode *>(E);
    break;
  case NodeKind::NK_And:
    delete static_cast<AndNode *>(E);
    break;
  case NodeKind::NK_Or:
    delete static_cast<OrNode *>(E);
    break;
  }
}
