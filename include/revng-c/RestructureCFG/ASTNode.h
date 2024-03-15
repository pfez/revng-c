#pragma once

//
// This file is distributed under the MIT License. See LICENSE.md for details.
//

#include <cstdlib>

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/Support/Casting.h"

#include "revng-c/RestructureCFG/BasicBlockNodeBB.h"
#include "revng-c/RestructureCFG/ExprNode.h"

// Forward declarations
namespace llvm {
class ConstantInt;
} // namespace llvm

template<class NodeT>
class BasicBlockNode;

class ASTNode {

public:
  enum NodeKind {
    NK_Code,
    NK_Break,
    NK_Continue,
    NK_If,
    NK_Scs,
    NK_List,
    NK_Switch,
    NK_SwitchBreak,
    NK_Set
  };

  enum class DispatcherKind {
    DK_NotADispatcher,
    DK_Entry,
    DK_Exit,
  };

  using ASTNodeMap = std::map<ASTNode *, ASTNode *>;
  // Steal the `BasicBlockNodeBB` definition from the external namespace
  using BasicBlockNodeBB = ::BasicBlockNodeBB;
  using BBNodeMap = std::map<BasicBlockNodeBB *, BasicBlockNodeBB *>;
  using ExprNodeMap = std::map<ExprNode *, ExprNode *>;

private:
  const NodeKind Kind;

protected:
  llvm::BasicBlock *BB = nullptr;
  std::string Name;
  ASTNode *Successor = nullptr;

  /// Unique Node ID inside a ASTNode, useful for printing to graphviz
  /// This field is initialized to 0, and will be re-assigned when the ASTNode
  /// will be inserted in an ASTTree.
  unsigned ID = 0;

  ASTNode(NodeKind K, const std::string &Name) : Kind(K), Name(Name) {}

  ASTNode(NodeKind K, const std::string &Name, llvm::BasicBlock *BB) :
    Kind(K), BB(BB), Name(Name) {}

public:
  ASTNode(NodeKind K, BasicBlockNodeBB *CFGNode, ASTNode *Successor = nullptr) :
    Kind(K),
    BB(CFGNode->isCode() ? CFGNode->getOriginalNode() : nullptr),
    Name(CFGNode->getNameStr()),
    Successor(Successor) {}

  inline ASTNode *Clone() const;

  ASTNode &operator=(ASTNode &&) = delete;
  ASTNode &operator=(const ASTNode &) = delete;
  ASTNode(ASTNode &&) = delete;
  ASTNode() = delete;

public:
  static void deleteASTNode(ASTNode *A);

protected:
  ASTNode(const ASTNode &) = default;
  ~ASTNode() = default;

public:
  NodeKind getKind() const { return Kind; }

  inline bool isEqual(const ASTNode *Node) const;

  std::string getName() const {
    return "ID:" + std::to_string(getID()) + " Name:" + Name;
  }

  void setID(unsigned NewID) { ID = NewID; }

  unsigned getID() const { return ID; }

  llvm::BasicBlock *getBB() const { return BB; }

  ASTNode *getSuccessor() const { return Successor; }

  ASTNode *consumeSuccessor() {
    ASTNode *SuccessorTmp = Successor;
    Successor = nullptr;
    return SuccessorTmp;
  }

  bool isDummy() {

    // An empty node, is a dummy node on the `RegionCFG`, which we model in the
    // AST as a `CodeNode`, with the `BB` field set to `nullptr`
    return Kind == NK_Code and BB == nullptr;
  }

  llvm::BasicBlock *getOriginalBB() const { return BB; }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  void dumpSuccessor(llvm::raw_fd_ostream &ASTFile);

  inline void updateASTNodesPointers(ASTNodeMap &SubstitutionMap);
};

class CodeNode : public ASTNode {
  friend class ASTNode;

private:
  bool ImplicitReturn = false;

public:
  CodeNode(BasicBlockNodeBB *CFGNode, ASTNode *Successor) :
    ASTNode(NK_Code, CFGNode, Successor) {}

protected:
  CodeNode(const CodeNode &) = default;
  CodeNode(CodeNode &&) = delete;
  ~CodeNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const;

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_Code; }

  bool containsImplicitReturn() const { return ImplicitReturn; }
  void setImplicitReturn() { ImplicitReturn = true; }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  ASTNode *Clone() const { return new CodeNode(*this); }
};

class IfNode : public ASTNode {
  friend class ASTNode;

public:
  using links_container = std::vector<llvm::BasicBlock *>;
  using links_iterator = typename links_container::iterator;
  using links_range = llvm::iterator_range<links_iterator>;

protected:
  ASTNode *Then;
  ASTNode *Else;
  ExprNode *ConditionExpression;

  // Field that represents if the enclosing node needs the emission of the
  // associated basic block instructions. This is currently used to prevent the
  // double emission of the instructions in case of `IfNode`s that are the
  // result of a `DualSwitch` promotion from a weaved switch.
  bool IsWeaved = false;

public:
  // Constructor used in the `RegionCFG` creation phase
  IfNode(BasicBlockNodeBB *CFGNode,
         ExprNode *CondExpr,
         ASTNode *Then,
         ASTNode *Else,
         ASTNode *PostDom) :
    ASTNode(NK_If, CFGNode, PostDom),
    Then(Then),
    Else(Else),
    ConditionExpression(CondExpr),
    IsWeaved(CFGNode->isWeaved()) {}

  // Constructor used in the beautify phase, where the `CFGNode`s underlying the
  // `ASTNode`s have gone out of scope. The `PostDom` field is not necessary
  // here, because we have exit the hybrid state of the AST where we have a link
  // to our postdominator, in favour of having `Sequence` nodes to represent
  // consequentiality, as customary in an AST representation.
  IfNode(ExprNode *CondExpr, ASTNode *Then, ASTNode *Else) :
    ASTNode(NK_If, "dispatcher_if"),
    Then(Then),
    Else(Else),
    ConditionExpression(CondExpr) {}

  // Constructor used in the beautify phase, where the `CFGNode`s underlying the
  // `ASTNode`s have gone out of scope. The `PostDom` field is not necessary
  // here, because we have exit the hybrid state of the AST where we have a link
  // to our postdominator, in favour of having `Sequence` nodes to represent
  // consequentiality, as customary in an AST representation. The many
  // parameters, are necessary to propagate attributes that cannot be extracted
  // anymore directly from the `CFGNode`.
  IfNode(ExprNode *CondExpr,
         ASTNode *Then,
         ASTNode *Else,
         const std::string &Name,
         bool IsWeaved,
         llvm::BasicBlock *BB) :
    ASTNode(NK_If, Name, BB),
    Then(Then),
    Else(Else),
    ConditionExpression(CondExpr),
    IsWeaved(IsWeaved) {}

protected:
  IfNode(const IfNode &) = default;
  IfNode(IfNode &&) = delete;
  ~IfNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const;

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_If; }

  ASTNode *getThen() const { return Then; }

  ASTNode *getElse() const { return Else; }

  void setThen(ASTNode *Node) { Then = Node; }

  void setElse(ASTNode *Node) { Else = Node; }

  bool hasThen() const {
    if (Then != nullptr) {
      return true;
    }
    return false;
  }

  bool hasElse() const {
    if (Else != nullptr) {
      return true;
    }
    return false;
  }

  bool hasBothBranches() {
    if ((Then != nullptr) and (Else != nullptr)) {
      return true;
    } else {
      return false;
    }
  }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  void updateASTNodesPointers(ASTNodeMap &SubstitutionMap);

  ASTNode *Clone() const { return new IfNode(*this); }

  ExprNode *getCondExpr() const { return ConditionExpression; }

  ExprNode **getCondExprAddress() { return &ConditionExpression; }

  void replaceCondExpr(ExprNode *NewExpr) { ConditionExpression = NewExpr; }

  void updateCondExprPtr(ExprNodeMap &Map);

  bool isWeaved() const { return IsWeaved; }
};

class ScsNode : public ASTNode {
  friend class ASTNode;

public:
  enum class Type {
    WhileTrue,
    While,
    DoWhile,
  };

private:
  ASTNode *Body;
  Type LoopType = Type::WhileTrue;
  IfNode *RelatedCondition = nullptr;

public:
  ScsNode(BasicBlockNodeBB *CFGNode, ASTNode *Body) :
    ASTNode(NK_Scs, CFGNode, nullptr), Body(Body) {}

  ScsNode(BasicBlockNodeBB *CFGNode, ASTNode *Body, ASTNode *Successor) :
    ASTNode(NK_Scs, CFGNode, Successor), Body(Body) {}

protected:
  ScsNode(const ScsNode &) = default;
  ScsNode(ScsNode &&) = delete;
  ~ScsNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const;

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_Scs; }

  bool hasBody() const { return Body != nullptr; }

  ASTNode *getBody() const { return Body; }

  void setBody(ASTNode *Node) { Body = Node; }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  void updateASTNodesPointers(ASTNodeMap &SubstitutionMap);

  ASTNode *Clone() const { return new ScsNode(*this); }

  bool isWhileTrue() const { return LoopType == Type::WhileTrue; }

  bool isWhile() const { return LoopType == Type::While; }

  bool isDoWhile() const { return LoopType == Type::DoWhile; }

  void setWhile(IfNode *Condition) {
    revng_assert(LoopType == Type::WhileTrue);
    LoopType = Type::While;
    RelatedCondition = Condition;
  }

  void setDoWhile(IfNode *Condition) {
    revng_assert(LoopType == Type::WhileTrue);
    LoopType = Type::DoWhile;
    RelatedCondition = Condition;
  }

  IfNode *getRelatedCondition() const {
    revng_assert(LoopType == Type::While or LoopType == Type::DoWhile);
    revng_assert(RelatedCondition != nullptr);

    return RelatedCondition;
  }
};

class SequenceNode : public ASTNode {
  friend class ASTNode;

public:
  using links_container = std::vector<ASTNode *>;
  using links_iterator = typename links_container::iterator;
  using links_range = llvm::iterator_range<links_iterator>;
  using const_links_iterator = typename links_container::const_iterator;
  using const_links_range = llvm::iterator_range<const_links_iterator>;

private:
  links_container NodeVec;

  SequenceNode(const std::string &Name) : ASTNode(NK_List, Name) {}

public:
  static SequenceNode *createEmpty(const std::string &Name) {
    return new SequenceNode(Name);
  }

protected:
  SequenceNode(const SequenceNode &) = default;
  SequenceNode(SequenceNode &&) = delete;
  ~SequenceNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const;

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_List; }

  links_range nodes() {
    return llvm::make_range(NodeVec.begin(), NodeVec.end());
  }

  const_links_range nodes() const {
    return llvm::make_range(NodeVec.begin(), NodeVec.end());
  }

  void addNode(ASTNode *Node) {
    NodeVec.push_back(Node);
    if (Node->getSuccessor() != nullptr) {
      this->addNode(Node->consumeSuccessor());
    }
  }

  void removeNode(ASTNode *Node) {
    NodeVec.erase(std::remove(NodeVec.begin(), NodeVec.end(), Node),
                  NodeVec.end());
  }

  links_container::size_type length() const { return NodeVec.size(); }

  ASTNode *getNodeN(links_container::size_type N) const { return NodeVec[N]; }

  links_container &getChildVec() { return NodeVec; }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  void updateASTNodesPointers(ASTNodeMap &SubstitutionMap);

  ASTNode *Clone() const {
    return reinterpret_cast<ASTNode *>(new SequenceNode(*this));
  }
};

class ContinueNode : public ASTNode {
  friend class ASTNode;

private:
  IfNode *ComputationIf = nullptr;
  bool IsImplicit = false;

public:
  ContinueNode(BasicBlockNodeBB *CFGNode) : ASTNode(NK_Continue, CFGNode) {}

protected:
  ContinueNode(const ContinueNode &) = default;
  ContinueNode(ContinueNode &&) = delete;
  ~ContinueNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const {
    return nullptr != llvm::dyn_cast_or_null<ContinueNode>(Node);
  }

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_Continue; }

  ASTNode *Clone() const { return new ContinueNode(*this); }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  bool hasComputation() const { return ComputationIf != nullptr; }

  void addComputationIfNode(IfNode *ComputationIfNode);

  IfNode *getComputationIfNode() const;

  bool isImplicit() const { return IsImplicit; }

  void setImplicit() { IsImplicit = true; }
};

class BreakNode : public ASTNode {
  friend class ASTNode;

public:
  BreakNode(BasicBlockNodeBB *CFGNode) : ASTNode(NK_Break, CFGNode) {}

  static bool classof(const ASTNode *N) { return N->getKind() == NK_Break; }

protected:
  BreakNode(const BreakNode &) = default;
  BreakNode(BreakNode &&) = delete;
  ~BreakNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const {
    return nullptr != llvm::dyn_cast_or_null<BreakNode>(Node);
  }

public:
  ASTNode *Clone() const { return new BreakNode(*this); }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  bool breaksFromWithinSwitch() const { return BreakFromWithinSwitch; }

  void setBreakFromWithinSwitch(bool B = true) { BreakFromWithinSwitch = B; }

protected:
  bool BreakFromWithinSwitch = false;
};

class SetNode : public ASTNode {
  friend class ASTNode;

private:
  unsigned StateVariableValue;

  // The `DispatcherKind` field is not needed in the `IfNode` too, but only in
  // `SwitchNode`, because the `InlineDispatcherSwitch` beautify pass is
  // scheduled before the promotion of two (or one) `case`s `switch`es to
  // `IfNode`s. Therefore, the `InlineDispatcherSwitch` pass will never query
  // such attribute in `IfNode`. The consequence is that rebus sic stantibus, we
  // cannot swap the order of execution between `InlineDispatcherSwitch` and
  // `simplifyDualSwitch` passes.
  DispatcherKind DKind;

public:
  SetNode(BasicBlockNodeBB *CFGNode, ASTNode *Successor = nullptr) :
    ASTNode(NK_Set, CFGNode, Successor),
    StateVariableValue(CFGNode->getStateVariableValue()) {
    using Type = BasicBlockNodeBB::Type;
    Type CFGNodeType = CFGNode->getDispatcherType();
    if (CFGNodeType == Type::EntrySet) {
      DKind = DispatcherKind::DK_Entry;
    } else if (CFGNodeType == Type::ExitSet) {
      DKind = DispatcherKind::DK_Exit;
    } else {
      revng_abort("Unexpected DispatcherKind");
    }
  }

protected:
  SetNode(const SetNode &) = default;
  SetNode(SetNode &&) = delete;
  ~SetNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const;

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_Set; }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  ASTNode *Clone() const { return new SetNode(*this); }

  unsigned getStateVariableValue() const { return StateVariableValue; }

  DispatcherKind getDispatcherKind() const {
    revng_assert(DKind != DispatcherKind::DK_NotADispatcher);
    return DKind;
  }
};

// Abstract SwitchNode. It has the concept of cases (other ASTNodes) but no
// concept of values for which those cases are activated.
class SwitchNode : public ASTNode {
  friend class ASTNode;

protected:
  static const constexpr int SwitchNumCases = 16;

public:
  using label_set_t = llvm::SmallSet<uint64_t, 1>;
  using labeled_case_t = std::pair<label_set_t, ASTNode *>;
  using case_container = llvm::SmallVector<labeled_case_t, SwitchNumCases>;
  using case_iterator = typename case_container::iterator;
  using case_range = llvm::iterator_range<case_iterator>;
  using case_const_iterator = typename case_container::const_iterator;
  using case_const_range = llvm::iterator_range<case_const_iterator>;

public:
  // To represent the `default` case, if present, contained in the `SwitchNode`,
  // we employ an empty set in the `LabeledCases` `SmallVector`.
  SwitchNode(BasicBlockNodeBB *CFGNode,
             llvm::Value *Cond,
             case_container &&LabeledCases,
             ASTNode *Successor) :
    ASTNode(NK_Switch, CFGNode, Successor),
    Condition(Cond),
    LabelCaseVec(std::move(LabeledCases)),
    IsWeaved(CFGNode->isWeaved()) {
    if (CFGNode->isDispatcher()) {
      using Type = BasicBlockNodeBB::Type;
      Type CFGNodeType = CFGNode->getDispatcherType();
      if (CFGNodeType == Type::EntryDispatcher) {
        DKind = DispatcherKind::DK_Entry;
      } else if (CFGNodeType == Type::ExitDispatcher) {
        DKind = DispatcherKind::DK_Exit;
      } else {
        revng_abort("Unexpected DispatcherKind");
      }
    }
  }

  SwitchNode(const SwitchNode &) = default;
  SwitchNode(SwitchNode &&) = delete;
  ~SwitchNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const;

public:
  static bool classof(const ASTNode *N) { return N->getKind() == NK_Switch; }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  ASTNode *Clone() const { return new SwitchNode(*this); }

  case_container &cases() { return LabelCaseVec; }

  case_const_range cases_const_range() const {
    return llvm::iterator_range(LabelCaseVec.begin(), LabelCaseVec.end());
  }

  size_t cases_size() { return LabelCaseVec.size(); }

  void removeCaseN(size_t N) {
    revng_assert(N < LabelCaseVec.size());
    LabelCaseVec.erase(LabelCaseVec.begin() + N);
  }

  void updateASTNodesPointers(ASTNodeMap &SubstitutionMap);

  bool needsStateVariable() const { return NeedStateVariable; }

  void setNeedsStateVariable(bool N = true) { NeedStateVariable = N; }

  bool needsLoopBreakDispatcher() const { return NeedLoopBreakDispatcher; }

  void setNeedsLoopBreakDispatcher(bool N = true) {
    NeedLoopBreakDispatcher = N;
  }

  ASTNode *getDefault() const {
    ASTNode *Default = nullptr;
    for (const auto &[LabelSet, Successor] : LabelCaseVec) {

      // The `default` case is signaled by having an empty associated label set.
      if (LabelSet.empty() == true) {

        // We should have not already found the `default`.
        revng_assert(Default == nullptr);

        Default = Successor;
      }
    }

    return Default;
  }

  void removeDefault() {
    for (auto &Group : llvm::enumerate(LabelCaseVec)) {
      unsigned Index = Group.index();
      const auto &[LabelSet, Successor] = Group.value();

      // The `default` case is signaled by having an empty associated label set
      if (LabelSet.empty() == true) {
        removeCaseN(Index);
        return;
      }
    }
  }

  bool hasDefault() const { return nullptr != getDefault(); }

  llvm::Value *getCondition() const { return Condition; }

  bool isWeaved() const { return IsWeaved; }

  DispatcherKind getDispatcherKind() const {
    revng_assert(DKind != DispatcherKind::DK_NotADispatcher);
    revng_assert(this->Condition == nullptr);
    return DKind;
  }

protected:
  llvm::Value *Condition;
  case_container LabelCaseVec;
  bool IsWeaved;
  bool NeedStateVariable = false; // for breaking directly out of a loop
  bool NeedLoopBreakDispatcher = false; // to dispatchg breaks out of a loop
  DispatcherKind DKind;
};

class SwitchBreakNode : public ASTNode {
  friend class ASTNode;

private:
  SwitchNode *ParentSwitch = nullptr;

public:
  SwitchBreakNode(SwitchNode *SN) :
    ASTNode(NK_SwitchBreak, "switch break"), ParentSwitch(SN) {}

protected:
  SwitchBreakNode(const SwitchBreakNode &) = default;
  SwitchBreakNode(SwitchBreakNode &&) = delete;
  ~SwitchBreakNode() = default;

  bool nodeIsEqual(const ASTNode *Node) const {
    return nullptr != llvm::dyn_cast_or_null<SwitchBreakNode>(Node);
  }

public:
  static bool classof(const ASTNode *N) {
    return N->getKind() == NK_SwitchBreak;
  }

  ASTNode *Clone() const { return new SwitchBreakNode(*this); }

  void dump(llvm::raw_fd_ostream &ASTFile);

  void dumpEdge(llvm::raw_fd_ostream &ASTFile);

  void updateASTNodesPointers(ASTNodeMap &SubstitutionMap);

  void setParentSwitch(SwitchNode *Switch) { ParentSwitch = Switch; }

  SwitchNode *getParentSwitch() const {
    revng_assert(ParentSwitch != nullptr);
    return ParentSwitch;
  }
};

inline ASTNode *ASTNode::Clone() const {
  switch (getKind()) {
  case NK_Code:
    return llvm::cast<CodeNode>(this)->Clone();
  case NK_Break:
    return llvm::cast<BreakNode>(this)->Clone();
  case NK_Continue:
    return llvm::cast<ContinueNode>(this)->Clone();
  case NK_If:
    return llvm::cast<IfNode>(this)->Clone();
  case NK_Scs:
    return llvm::cast<ScsNode>(this)->Clone();
  case NK_List:
    return llvm::cast<SequenceNode>(this)->Clone();
  case NK_Switch:
    return llvm::cast<SwitchNode>(this)->Clone();
  case NK_SwitchBreak:
    return llvm::cast<SwitchBreakNode>(this)->Clone();
  case NK_Set:
    return llvm::cast<SetNode>(this)->Clone();
  }
  return nullptr;
}

inline void ASTNode::updateASTNodesPointers(ASTNodeMap &SubstitutionMap) {
  if (Successor)
    Successor = SubstitutionMap.at(Successor);

  switch (getKind()) {
  case ASTNode::NK_If: {
    auto *If = llvm::cast<IfNode>(this);
    If->updateASTNodesPointers(SubstitutionMap);
  } break;

  case ASTNode::NK_Switch: {
    auto *Switch = llvm::dyn_cast<SwitchNode>(this);
    Switch->updateASTNodesPointers(SubstitutionMap);
  } break;

  case ASTNode::NK_Scs: {
    auto *Scs = llvm::dyn_cast<ScsNode>(this);
    Scs->updateASTNodesPointers(SubstitutionMap);
  } break;

  case ASTNode::NK_Continue: {
    auto *Continue = llvm::dyn_cast<ContinueNode>(this);
    // If it has a computation we have to update it.
    revng_assert(not Continue->hasComputation());
  } break;

  case ASTNode::NK_SwitchBreak: {
    auto *SwitchBreak = llvm::dyn_cast<SwitchBreakNode>(this);
    SwitchBreak->updateASTNodesPointers(SubstitutionMap);
  } break;

  case ASTNode::NK_Code:
  case ASTNode::NK_Break:
  case ASTNode::NK_Set: {
    // They only have a successor
  } break;

  case ASTNode::NK_List: {
    auto *Seq = llvm::cast<SequenceNode>(this);
    Seq->updateASTNodesPointers(SubstitutionMap);
  } break;

  default:
    revng_abort("AST node type not expected");
  }
}

inline bool ASTNode::isEqual(const ASTNode *Node) const {
  switch (getKind()) {
  case NK_Code:
    return llvm::cast<CodeNode>(this)->nodeIsEqual(Node);
  case NK_Break:
    return llvm::cast<BreakNode>(this)->nodeIsEqual(Node);
  case NK_Continue:
    return llvm::cast<ContinueNode>(this)->nodeIsEqual(Node);
  // ---- IfNode kinds
  case NK_If:
    return llvm::cast<IfNode>(this)->nodeIsEqual(Node);
  // ---- end IfNode kinds
  case NK_Scs:
    return llvm::cast<ScsNode>(this)->nodeIsEqual(Node);
  case NK_List:
    return llvm::cast<SequenceNode>(this)->nodeIsEqual(Node);
  case NK_Switch:
    return llvm::cast<SwitchNode>(this)->nodeIsEqual(Node);
  case NK_SwitchBreak:
    return llvm::cast<SwitchBreakNode>(this)->nodeIsEqual(Node);
  case NK_Set:
    return llvm::cast<SetNode>(this)->nodeIsEqual(Node);
  default:
    revng_abort();
  }
}
