//=- CFLSteensAliasAnalysis.h - Unification-based Alias Analysis ---*- C++-*-=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for LLVM's unification-based alias analysis
/// implemented with CFL graph reachability.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CFLSTEENSALIASANALYSIS_H
#define LLVM_ANALYSIS_CFLSTEENSALIASANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/None.h"
#include "llvm/ADT/Optional.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
#include <forward_list>

namespace llvm {

class TargetLibraryInfo;

namespace cflaa {
struct AliasSummary;
}

class CFLSteensAAResult : public AAResultBase<CFLSteensAAResult> {
  friend AAResultBase<CFLSteensAAResult>;
  class FunctionInfo;

public:
  explicit CFLSteensAAResult(const TargetLibraryInfo &);
  CFLSteensAAResult(CFLSteensAAResult &&Arg);
  ~CFLSteensAAResult();

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  bool invalidate(Function &, const PreservedAnalyses &) { return false; }

  /// \brief Inserts the given Function into the cache.
  void scan(Function *Fn);

  void evict(Function *Fn);

  /// \brief Ensures that the given function is available in the cache.
  /// Returns the appropriate entry from the cache.
  const Optional<FunctionInfo> &ensureCached(Function *Fn);

  /// \brief Get the alias summary for the given function
  /// Return nullptr if the summary is not found or not available
  const cflaa::AliasSummary *getAliasSummary(Function &Fn);

  AliasResult query(const MemoryLocation &LocA, const MemoryLocation &LocB);

  AliasResult alias(const MemoryLocation &LocA, const MemoryLocation &LocB) {
    if (LocA.Ptr == LocB.Ptr)
      return LocA.Size == LocB.Size ? MustAlias : PartialAlias;

    // Comparisons between global variables and other constants should be
    // handled by BasicAA.
    // CFLSteensAA may report NoAlias when comparing a GlobalValue and
    // ConstantExpr, but every query needs to have at least one Value tied to a
    // Function, and neither GlobalValues nor ConstantExprs are.
    if (isa<Constant>(LocA.Ptr) && isa<Constant>(LocB.Ptr))
      return AAResultBase::alias(LocA, LocB);

    AliasResult QueryResult = query(LocA, LocB);
    if (QueryResult == MayAlias)
      return AAResultBase::alias(LocA, LocB);

    return QueryResult;
  }

private:
  struct FunctionHandle final : public CallbackVH {
    FunctionHandle(Function *Fn, CFLSteensAAResult *Result)
        : CallbackVH(Fn), Result(Result) {
      assert(Fn != nullptr);
      assert(Result != nullptr);
    }

    void deleted() override { removeSelfFromCache(); }
    void allUsesReplacedWith(Value *) override { removeSelfFromCache(); }

  private:
    CFLSteensAAResult *Result;

    void removeSelfFromCache() {
      assert(Result != nullptr);
      auto *Val = getValPtr();
      Result->evict(cast<Function>(Val));
      setValPtr(nullptr);
    }
  };

  const TargetLibraryInfo &TLI;

  /// \brief Cached mapping of Functions to their StratifiedSets.
  /// If a function's sets are currently being built, it is marked
  /// in the cache as an Optional without a value. This way, if we
  /// have any kind of recursion, it is discernable from a function
  /// that simply has empty sets.
  DenseMap<Function *, Optional<FunctionInfo>> Cache;
  std::forward_list<FunctionHandle> Handles;

  FunctionInfo buildSetsFrom(Function *F);
};

/// Analysis pass providing a never-invalidated alias analysis result.
///
/// FIXME: We really should refactor CFL to use the analysis more heavily, and
/// in particular to leverage invalidation to trigger re-computation of sets.
class CFLSteensAA : public AnalysisInfoMixin<CFLSteensAA> {
  friend AnalysisInfoMixin<CFLSteensAA>;
  static char PassID;

public:
  typedef CFLSteensAAResult Result;

  CFLSteensAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the CFLSteensAAResult object.
class CFLSteensAAWrapperPass : public ImmutablePass {
  std::unique_ptr<CFLSteensAAResult> Result;

public:
  static char ID;

  CFLSteensAAWrapperPass();

  CFLSteensAAResult &getResult() { return *Result; }
  const CFLSteensAAResult &getResult() const { return *Result; }

  void initializePass() override;
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

//===--------------------------------------------------------------------===//
//
// createCFLSteensAAWrapperPass - This pass implements a set-based approach to
// alias analysis.
//
ImmutablePass *createCFLSteensAAWrapperPass();
}

#endif
