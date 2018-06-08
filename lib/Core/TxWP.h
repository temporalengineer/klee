//===-- TXWP.h - Tracer-X symbolic execution tree -------------*- C++ -*-===//
//
//               The Tracer-X KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains declarations of the classes that implements the
/// weakest precondition interpolation.
///
//===----------------------------------------------------------------------===//

#ifndef TXWP_H_
#define TXWP_H_

#include "klee/ExecutionState.h"
#include <klee/Expr.h>
#include <klee/ExprBuilder.h>
#include <klee/Internal/Support/ErrorHandling.h>
#include <klee/util/ArrayCache.h>
#include "TxPartitionHelper.h"
#include "TxDependency.h"
#include "TxTree.h"
#include "TxWPHelper.h"
#include <vector>

namespace klee {

/// \brief Implements the replacement mechanism for replacing variables in WP
/// Expr, used in replacing free with bound variables.
class TxWPArrayStore {

  static std::map<std::pair<std::string, llvm::Value *>,
                  std::pair<const Array *, ref<Expr> > > arrayStore;

public:
  static ArrayCache ac;
  static const Array *array;
  static ref<Expr> constValues;

  static void insert(llvm::Value *value, const Array *array, ref<Expr> expr);

  static ref<Expr> createAndInsert(std::string arrayName, llvm::Value *value);

  static const Array *getArrayRef(llvm::Value *value);

  static llvm::Value *getValuePointer(ref<Expr> expr);

  static llvm::Value *getValuePointer(std::string func, ref<Expr> expr);

  static std::string getFunctionName(llvm::Value *i);
};

/// \brief The class that implements weakest precondition interpolant.
class TxWeakestPreCondition {

  friend class TxTree;

  friend class ExecutionState;

  std::set<llvm::Value *> markedVariables;

  ref<Expr> WPExpr;
  std::vector<ref<Expr> > WPExprs;

  // Respective interpolation tree node
  TxTreeNode *node;

  /// \brief The dependency information for the respective interpolation tree
  /// node
  TxDependency *dependency;

  int debugSubsumptionLevel;

public:
  TxWeakestPreCondition(TxTreeNode *_node, TxDependency *_dependency);

  ~TxWeakestPreCondition();

  ref<Expr> True() {
    return ConstantExpr::alloc(1, Expr::Bool);
  };
  ref<Expr> False() {
    return ConstantExpr::alloc(0, Expr::Bool);
  };

  void resetWPExpr() { WPExpr = False(); }

  void setWPExpr(std::vector<ref<Expr> > expr) { WPExprs = expr; }

  std::vector<ref<Expr> > getWPExpr() { return WPExprs; }

  // \brief Preprocessing phase: marking the instructions that contribute
  // to the target or an infeasible path.
  // std::vector<std::pair<KInstruction *, int> > markVariables(
  //      std::vector<std::pair<KInstruction *, int> > reverseInstructionList);

  // \brief Generate and return the weakest precondition expression.
  ref<Expr> GenerateWP(
      std::vector<std::pair<KInstruction *, int> > reverseInstructionList,
      bool markAllFlag);

  // \brief Generate expression from operand of an instruction
  ref<Expr> generateExprFromOperand(llvm::Instruction *i, int operand);

  // \brief Return LHS of an instruction as a read expression
  ref<Expr> getLHS(llvm::Instruction *i);

  // \brief Instantiates the variables in WPExpr by their latest value for the
  // implication test.
  std::vector<ref<Expr> >
  instantiateWPExpression(TxDependency *dependency,
                          const std::vector<llvm::Instruction *> &callHistory,
                          std::vector<ref<Expr> > WPExpr);

  ref<Expr> instantiateSingleExpression(
      TxDependency *dependency,
      const std::vector<llvm::Instruction *> &callHistory,
      ref<Expr> singleWPExpr);

  /// \brief Perform the intersection of two weakest precondition expressions
  /// with respect to the branchCondition
  std::vector<ref<Expr> > intersectExpr(
      ref<Expr> branchCondition, std::vector<ref<Expr> > expr1,
      std::vector<ref<Expr> > expr2, ref<Expr> interpolant,
      std::set<const Array *> existentials,
      TxStore::LowerInterpolantStore concretelyAddressedHistoricalStore,
      TxStore::LowerInterpolantStore symbolicallyAddressedHistoricalStore,
      TxStore::TopInterpolantStore concretelyAddressedStore,
      TxStore::TopInterpolantStore symbolicallyAddressedStore);

  std::vector<ref<Expr> > intersectExpr_aux(std::vector<ref<Expr> > expr1,
                                            std::vector<ref<Expr> > expr2);

  // \brief Return the minimum of two constant expressions
  ref<ConstantExpr> getMinOfConstExpr(ref<ConstantExpr> expr1,
                                      ref<ConstantExpr> expr2);

  // \brief Return the maximum of two constant expressions
  ref<ConstantExpr> getMaxOfConstExpr(ref<ConstantExpr> expr1,
                                      ref<ConstantExpr> expr2);

  // \brief Return true if the destination of the LLVM instruction appears in
  // the WP expression
  bool isTargetDependent(llvm::Value *inst, ref<Expr> wp);

  // \brief Update subsumption table entry based on the WP Expr
  TxSubsumptionTableEntry *
  updateSubsumptionTableEntry(TxSubsumptionTableEntry *entry,
                              std::vector<ref<Expr> > wp);

  // \brief Update subsumption table entry based on one Partition from WP Expr
  TxSubsumptionTableEntry *
  updateSubsumptionTableEntrySinglePartition(TxSubsumptionTableEntry *entry,
                                             ref<Expr> wp);

  // \brief Update concretelyAddressedStore based on the WP Expr
  TxStore::TopInterpolantStore updateConcretelyAddressedStore(
      TxStore::TopInterpolantStore concretelyAddressedStore, ref<Expr> wp);

  // \brief Get variable stored in the Partition
  ref<Expr> getVarFromExpr(ref<Expr> wp);

  // \brief Update interpolant based on the WP Expr
  ref<Expr> updateInterpolant(ref<Expr> interpolant, ref<Expr> wp);

  // \brief Extracts unrelated frames from interpolant to be passed in
  // conjunction with the WP Expr
  ref<Expr> extractUnrelatedFrame(ref<Expr> interpolant, ref<Expr> var);

  // \brief Replace array with shadow array in an expression
  ref<Expr> replaceArrayWithShadow(ref<Expr> interpolant);

  // \brief Add new existential variables to the list
  std::set<const Array *>
  updateExistentials(std::set<const Array *> existentials, ref<Expr> wp);

  // \brief Replace arguments passed to a call with the function arguments in
  // the WPExpr
  ref<Expr> replaceCallArguments(ref<Expr> wp, llvm::Value *funcArg,
                                 llvm::Value *callArg);

  // \brief Generate and return the weakest precondition expressions.
  std::vector<ref<Expr> > GenerateWP(
      std::vector<std::pair<KInstruction *, int> > reverseInstructionList);
  ref<Expr> getPrevExpr(ref<Expr> e, llvm::Instruction *i);

private:
  ref<Expr> getCondition(llvm::Instruction *ins);
};
}
#endif /* TXWP_H_ */
