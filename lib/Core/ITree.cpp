/*
 * ITree.cpp
 *
 *  Created on: Oct 15, 2015
 *      Author: felicia
 */

#include "ITree.h"
#include "TimingSolver.h"

#include <klee/Expr.h>
#include <klee/Solver.h>
#include <klee/util/ExprPPrinter.h>
#include <vector>

using namespace klee;

PathConditionMarker::PathConditionMarker(PathCondition *pathCondition) :
  mayBeInInterpolant(false), pathCondition(pathCondition) {}

PathConditionMarker::~PathConditionMarker() {}

void PathConditionMarker::mayIncludeInInterpolant() {
  mayBeInInterpolant = true;
}

void PathConditionMarker::includeInInterpolant() {
  if (mayBeInInterpolant) {
      pathCondition->includeInInterpolant();
  }
}

PathCondition::PathCondition(ref<Expr>& constraint) :
    constraint(constraint), inInterpolant(false), tail(0) {}

PathCondition::PathCondition(ref<Expr>& constraint, PathCondition *prev) :
    constraint(constraint), inInterpolant(false), tail(prev) {}

PathCondition::~PathCondition() {}

ref<Expr> PathCondition::car() const {
  return constraint;
}

PathCondition *PathCondition::cdr() const {
  return tail;
}

void PathCondition::includeInInterpolant() {
  inInterpolant = true;
}

bool PathCondition::carInInterpolant() {
  return inInterpolant;
}

std::vector< ref<Expr> > PathCondition::packInterpolant() const {
  std::vector< ref<Expr> > res;
  for (const PathCondition *it = this; it != 0; it = it->tail) {
      if (it->inInterpolant)
	res.push_back(it->constraint);
  }
  return res;
}

void PathCondition::dump() {
  this->print(llvm::errs());
  llvm::errs() << "\n";
}

void PathCondition::print(llvm::raw_ostream& stream) {
  stream << "[";
  for (PathCondition *it = this; it != 0; it = it->tail) {
      stream << ": " << (it->inInterpolant ? "interpolant constraint" : "non-interpolant constraint");
      if (it->tail != 0) stream << ",";
  }
  stream << "]";
}

SubsumptionTableEntry::SubsumptionTableEntry(ITreeNode *node) :
  nodeId(node->getNodeId()),
  interpolant(node->getInterpolant()) {}

SubsumptionTableEntry::~SubsumptionTableEntry() {}

bool SubsumptionTableEntry::subsumed(TimingSolver *solver,
                                     ExecutionState& state,
                                     double timeout) {
  if (state.itreeNode == 0)
    return false;

  if (state.itreeNode->getNodeId() == nodeId) {
      /// We create path condition needed constraints marking structure
      std::map< ref<Expr>, PathConditionMarker *> markerMap =
	  state.itreeNode->makeMarkerMap();

      for (std::vector< ref<Expr> >::iterator it0 = interpolant.begin();
	  it0 != interpolant.end(); it0++) {
	  ref<Expr> query = *it0;
	  Solver::Validity result;

	  /// llvm::errs() << "Querying for subsumption check:\n";
	  /// ExprPPrinter::printQuery(llvm::errs(), state.constraints, query);

	  solver->setTimeout(timeout);
	  bool success = solver->evaluate(state, query, result);
	  solver->setTimeout(0);
	  if (success && result == Solver::True) {
	    std::vector< ref<Expr> > unsat_core = solver->getUnsatCore();

	    for (std::vector< ref<Expr> >::iterator it1 = unsat_core.begin();
		it1 != unsat_core.end(); it1++) {
		markerMap[*it1]->mayIncludeInInterpolant();
	    }

	  } else {
	    return false;
	  }
      }

      llvm::errs() << "STATE SUBSUMED: " << (state.itreeNode->getNodeId()) << "\n";
      /// State subsumed, we mark needed constraints on the
      /// path condition.
      for (std::map< ref<Expr>, PathConditionMarker *>::iterator it = markerMap.begin();
	  it != markerMap.end(); it++) {
	  it->second->includeInInterpolant();
      }
      markerMap.clear();
      return true;
  }
  return false;
}

void SubsumptionTableEntry::dump() const {
  this->print(llvm::errs());
  llvm::errs() << "\n";
}

void SubsumptionTableEntry::print(llvm::raw_ostream &stream) const {
  stream << "------------ Subsumption Table Entry ------------\n";
  stream << "Program point = " << nodeId << "\n";
  stream << "interpolant = [";
  for (std::vector< ref<Expr> >::const_iterator it = interpolant.begin();
      it != interpolant.end(); it++) {
      it->get()->print(stream);
      if (it + 1 != interpolant.end()) {
	  stream << ",";
      }
  }
  stream << "]\n";
}

ITree::ITree(ExecutionState* _root) :
    currentINode(0),
    root(new ITreeNode(0, _root)) {}

ITree::~ITree() {}

bool ITree::checkCurrentStateSubsumption(TimingSolver *solver,
                                         ExecutionState& state,
                                         double timeout) {
  assert(state.itreeNode == currentINode);

  for (std::vector<SubsumptionTableEntry>::iterator it = subsumptionTable.begin();
      it != subsumptionTable.end(); it++) {
      if (it->subsumed(solver, state, timeout)) {

	  /// We mark as subsumed such that the node will not be
	  /// stored into table (the table already contains a more
	  /// general entry).
	  currentINode->isSubsumed = true;

	  return true;
      }
  }
  return false;
}

std::vector<SubsumptionTableEntry> ITree::getStore() {
  return subsumptionTable;
}

void ITree::store(SubsumptionTableEntry subItem) {
  subsumptionTable.push_back(subItem);
}

void ITree::setCurrentINode(ITreeNode *node) {
  currentINode = node;
}

void ITree::remove(ITreeNode *node) {
  assert(!node->left && !node->right);
  do {
    ITreeNode *p = node->parent;

    /// As the node is about to be deleted, it must have been completely
    /// traversed, hence the correct time to table the interpolant.
    if (!node->isSubsumed && node->introducesMarkedConstraint()) {
      SubsumptionTableEntry entry(node);
      store(entry);
    }

    delete node;
    if (p) {
      if (node == p->left) {
        p->left = 0;
      } else {
        assert(node == p->right);
        p->right = 0;
      }
    }
    node = p;
  } while (node && !node->left && !node->right);
}

std::pair<ITreeNode *, ITreeNode *> ITree::split(ITreeNode *parent, ExecutionState *left, ExecutionState *right) {
  parent->split(left, right);
  return std::pair<ITreeNode *, ITreeNode *> (parent->left, parent->right);
}

void ITree::markPathCondition(std::vector< ref<Expr> > unsat_core) {
  /// Simply return in case the unsatisfiability core is empty
  if (unsat_core.size() == 0)
      return;

  /// Process the unsat core in case it was computed (non-empty)
  PathCondition *pc = currentINode->pathCondition;

  if (pc != 0) {
      for (std::vector< ref<Expr> >::reverse_iterator it = unsat_core.rbegin();
	  it != unsat_core.rend(); it++) {
	  while (pc != 0) {
	      if (pc->car().compare(it->get()) == 0) {
		  pc->includeInInterpolant();
		  pc = pc->cdr();
		  break;
	      }
	      pc = pc->cdr();
	  }
	  if (pc == 0) break;
      }
  }
}

void ITree::recordBlock(Instruction *inst) {
  blockTable.add(inst);
}

void ITree::dumpBlock() {
  blockTable.dump();
}

void ITree::printNode(llvm::raw_ostream& stream, ITreeNode *n, std::string edges) {
  if (n->left != 0) {
      stream << edges << "+-- L:" << n->left->nodeId;
      if (this->currentINode == n->left) {
	  stream << " (active)";
      }
      stream << "\n";
      if (n->right != 0) {
	  printNode(stream, n->left, edges + "|   ");
      } else {
	  printNode(stream, n->left, edges + "    ");
      }
  }
  if (n->right != 0) {
      stream << edges << "+-- R:" << n->right->nodeId;
      if (this->currentINode == n->right) {
	  stream << " (active)";
      }
      stream << "\n";
      printNode(stream, n->right, edges + "    ");
  }
}

void ITree::print(llvm::raw_ostream& stream) {
  llvm::errs() << "------------------------- ITree Structure ---------------------------\n";
  stream << this->root->nodeId;
  if (this->root == this->currentINode) {
      stream << " (active)";
  }
  stream << "\n";
  this->printNode(stream, this->root, "");
}

void ITree::dump() {
  this->print(llvm::errs());
}

ITreeNode::ITreeNode(ITreeNode *_parent,
                     ExecutionState *_data)
: parent(_parent),
  left(0),
  right(0),
  nodeId(0),
  isSubsumed(false),
  data(_data) {

  pathCondition = (_parent != 0) ? _parent->pathCondition : 0;

  if (!(_data->constraints.empty())) {
      ref<Expr> lastConstraint = _data->constraints.back();
      if (pathCondition == 0) {
	  pathCondition = new PathCondition(lastConstraint);
      } else if (pathCondition->car().compare(lastConstraint) != 0) {
	  pathCondition = new PathCondition(lastConstraint, pathCondition);
      }
  }
}

ITreeNode::~ITreeNode() {
  /// Only delete the path condition if it's not
  /// also the parent's path condition
  if (parent != 0 &&
      pathCondition != parent->pathCondition)
    delete pathCondition;
}

unsigned int ITreeNode::getNodeId() {
  return nodeId;
}

std::vector< ref<Expr> > ITreeNode::getInterpolant() const {
  return this->pathCondition->packInterpolant();
}

void ITreeNode::setNodeLocation(unsigned int programPoint) {
  if (this->nodeId == 0)  {
    this->nodeId = programPoint;
  }
}

void ITreeNode::split(ExecutionState *leftData, ExecutionState *rightData) {
  assert (left == 0 && right == 0);
  leftData->itreeNode = left = new ITreeNode(this, leftData);
  rightData->itreeNode = right = new ITreeNode(this, rightData);
}

std::map< ref<Expr>, PathConditionMarker *> ITreeNode::makeMarkerMap() {
  std::map< ref<Expr>, PathConditionMarker *> result;
  for (PathCondition *it = pathCondition; it != 0; it = it->cdr()) {
      result.insert( std::pair< ref<Expr>, PathConditionMarker *>
	(it->car(), new PathConditionMarker(it)) );
  }
  return result;
}

bool ITreeNode::introducesMarkedConstraint() {
  if (parent != 0 &&
      pathCondition != parent->pathCondition &&
      pathCondition->carInInterpolant()) {
      return true;
  }
  return false;
}


void ITreeNode::dump() const {
  llvm::errs() << "\n------------------------- ITree Node --------------------------------\n";
  this->print(llvm::errs());
}

void ITreeNode::print(llvm::raw_ostream &stream) const {
  this->print(stream, 0);
}

void ITreeNode::print(llvm::raw_ostream &stream, const unsigned int tab_num) const {
  std::string tabs = make_tabs(tab_num);
  std::string tabs_next = tabs + "\t";

  stream << tabs << "ITreeNode\n";
  stream << tabs_next << "node Id = " << nodeId << "\n";
  stream << tabs_next << "pathCondition = ";
  if (pathCondition == 0) {
      stream << "NULL";
  } else {
      pathCondition->print(stream);
  }
  stream << "\n";
  stream << tabs_next << "Left:\n";
  if (!left) {
      stream << tabs_next << "NULL\n";
  } else {
      left->print(stream, tab_num + 1);
      stream << "\n";
  }
  stream << tabs_next << "Right:\n";
  if (!right) {
      stream << tabs_next << "NULL\n";
  } else {
      right->print(stream, tab_num + 1);
      stream << "\n";
  }
}



