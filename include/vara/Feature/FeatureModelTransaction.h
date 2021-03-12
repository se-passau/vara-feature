#ifndef VARA_FEATURE_FEATUREMODELTRANSACTION_H
#define VARA_FEATURE_FEATUREMODELTRANSACTION_H

#include "vara/Feature/FeatureModel.h"

#include "llvm/ADT/StringRef.h"

#include <algorithm>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

namespace vara::feature {

namespace detail {
class CopyTransactionMode;
class ModifyTransactionMode;
class FeatureModelCopyTransactionBase;
class FeatureModelModifyTransactionBase;

using ConsistencyCheck =
    FeatureModelConsistencyChecker<ExactlyOneRootNode,
                                   EveryFeatureRequiresParent,
                                   CheckFeatureParentChildRelationShip>;
} // namespace detail

using FeatureVariant = std::variant<std::string, Feature *>;
using FeatureTreeNodeVariant = std::variant<std::string, FeatureTreeNode *>;

template <typename CopyMode>
class FeatureModelTransaction
    : private std::conditional_t<
          std::is_same_v<CopyMode, detail::CopyTransactionMode>,
          detail::FeatureModelCopyTransactionBase,
          detail::FeatureModelModifyTransactionBase> {
  static inline constexpr bool IsCopyMode =
      std::is_same_v<CopyMode, detail::CopyTransactionMode>;
  using TransactionBaseTy =
      std::conditional_t<IsCopyMode, detail::FeatureModelCopyTransactionBase,
                         detail::FeatureModelModifyTransactionBase>;

public:
  /// \brief Opens a new Transaction for the given FeatureModel. The Transaction
  /// enables the user to modify the underlying FeatureModel via a specific
  /// API, preserving the correctness of the FeatureModel.
  static FeatureModelTransaction openTransaction(FeatureModel &FM) {
    assert(detail::ConsistencyCheck::isFeatureModelValid(FM) &&
           "Passed FeatureModel was in an invalid state.");
    return FeatureModelTransaction(FM);
  }

  // Transaction can only be moved. Copying a currently open Transaction has no
  // well defined meaning.
  FeatureModelTransaction(const FeatureModelTransaction &) = delete;
  FeatureModelTransaction &operator=(const FeatureModelTransaction &) = delete;
  FeatureModelTransaction(FeatureModelTransaction &&) noexcept = default;
  FeatureModelTransaction &
  operator=(FeatureModelTransaction &&) noexcept = default;
  ~FeatureModelTransaction() {
    if constexpr (IsCopyMode) {
      assert(!this->isUncommited() &&
             "Transaction in CopyMode should be commited before destruction.");
    } else {
      if (this->isUncommited()) { // In modification mode we should ensure that
                                  // changes are committed before destruction
        llvm::errs() << "error: uncommitted modifications before destruction"
                     << '\n';
        commit();
      }
    }
  }

  /// \brief Commit and finalize the FeatureModelTransaction.
  /// In CopyMode a new FeatureModel is returned.
  /// In ModifyMode the specified changes are applied to the underlying
  /// FeatureModel.
  ///
  /// \returns a FeatureModel or nothing
  decltype(auto) commit() { return this->commitImpl(); }

  /// Abort the current Transaction, throwing away all changes.
  void abort() { this->abortImpl(); }

  /// \brief Add a Feature to the FeatureModel
  ///
  /// \returns a pointer to the inserted Feature in CopyMode, otherwise,
  ///          nothing.
  decltype(auto) addFeature(std::unique_ptr<Feature> NewFeature,
                            Feature *Parent = nullptr) {
    if constexpr (IsCopyMode) {
      return this->addFeatureImpl(std::move(NewFeature), Parent);
    } else {
      this->addFeatureImpl(std::move(NewFeature), Parent);
    }
  }

  decltype(auto) addRelationship(
      Relationship::RelationshipKind Kind, const FeatureVariant &Parent,
      std::variant<std::set<std::string>, std::set<Feature *>> Children) {
    if constexpr (IsCopyMode) {
      return this->addRelationshipImpl(Kind, Parent, Children);
    } else {
      this->addRelationshipImpl(Kind, Parent, Children);
    }
  }

  decltype(auto) addConstraint(std::unique_ptr<Constraint> Constraint) {
    if constexpr (IsCopyMode) {
      return this->addConstraintImpl(std::move(Constraint));
    } else {
      this->addConstraintImpl(std::move(Constraint));
    }
  }

  void setName(std::string Name) { return this->setNameImpl(std::move(Name)); }

  void setCommit(std::string Commit) {
    return this->setCommitImpl(std::move(Commit));
  }

  void setPath(fs::path Path) { return this->setPathImpl(std::move(Path)); }

  decltype(auto) setRoot(std::unique_ptr<RootFeature> Root) {
    if constexpr (IsCopyMode) {
      return this->setRootImpl(std::move(Root));
    } else {
      this->setRootImpl(std::move(Root));
    }
  }

  decltype(auto) addChild(const FeatureTreeNodeVariant &Parent,
                          const FeatureTreeNodeVariant &Child) {
    if constexpr (IsCopyMode) {
      return this->addChildImpl(Parent, Child);
    } else {
      this->addChildImpl(Parent, Child);
    }
  }

private:
  FeatureModelTransaction(FeatureModel &FM) : TransactionBaseTy(FM) {}
};

using FeatureModelCopyTransaction =
    FeatureModelTransaction<detail::CopyTransactionMode>;
using FeatureModelModifyTransaction =
    FeatureModelTransaction<detail::ModifyTransactionMode>;

//===----------------------------------------------------------------------===//
//                    Transaction Implementation Details
//===----------------------------------------------------------------------===//

namespace detail {

class CopyTransactionMode {};
class ModifyTransactionMode {};

class FeatureModelModification {
  friend class FeatureModelCopyTransactionBase;
  friend class FeatureModelModifyTransactionBase;

public:
  virtual ~FeatureModelModification() = default;

  /// \brief Execute the modification on the given FeatureModel.
  virtual void exec(FeatureModel &FM) = 0;

protected:
  /// \brief Set the parent of a \a Feature.
  static void setParent(FeatureTreeNode &F, FeatureTreeNode &Parent) {
    F.setParent(&Parent);
  }

  /// \brief Remove the parent of a \a Feature.
  static void removeParent(FeatureTreeNode &F) { F.setParent(nullptr); }

  /// \brief Add a \a Feature Child to F.
  static void addEdge(FeatureTreeNode &F, FeatureTreeNode &Child) {
    F.addEdge(&Child);
  }

  /// \brief Remove \a Feature Child from F.
  static void removeEdge(FeatureTreeNode &F, FeatureTreeNode &Child) {
    F.removeEdge(&Child);
  }

  static void addConstraint(Feature &F, PrimaryFeatureConstraint &Constraint) {
    F.addConstraint(&Constraint);
  }

  static void setFeature(PrimaryFeatureConstraint &Constraint, Feature &F) {
    Constraint.setFeature(&F);
  }

  /// \brief Adds a new \a Feature to the FeatureModel.
  ///
  /// \param FM model to add to
  /// \param NewFeature the Feature to add
  ///
  /// \returns A pointer to the inserted Feature.
  static Feature *addFeature(FeatureModel &FM,
                             std::unique_ptr<Feature> NewFeature) {
    return FM.addFeature(std::move(NewFeature));
  }

  static Relationship *
  addRelationship(FeatureModel &FM,
                  std::unique_ptr<Relationship> NewRelationship) {
    return FM.addRelationship(std::move(NewRelationship));
  }

  static Constraint *addConstraint(FeatureModel &FM,
                                   std::unique_ptr<Constraint> Constraint) {
    return FM.addConstraint(std::move(Constraint));
  }

  static void setName(FeatureModel &FM, std::string NewName) {
    FM.setName(std::move(NewName));
  }

  static void setCommit(FeatureModel &FM, std::string NewCommit) {
    FM.setCommit(std::move(NewCommit));
  }

  static void setPath(FeatureModel &FM, fs::path NewPath) {
    FM.setPath(std::move(NewPath));
  }

  static RootFeature *setRoot(FeatureModel &FM, RootFeature &NewRoot) {
    return FM.setRoot(NewRoot);
  }

  static void sort(FeatureModel &FM) { FM.sort(); }

  /// \brief Remove \a Feature from a \a FeatureModel.
  ///
  /// \param FM model to remove from
  /// \param F the Feature to delete
  static void removeFeature(FeatureModel &FM, Feature &F) {
    FM.removeFeature(F);
  }

  template <class Ty>
  static Ty *resolveVariant(FeatureModel &FM,
                            std::variant<std::string, Ty *> V) {
    return std::holds_alternative<std::string>(V)
               ? FM.getFeature(std::get<std::string>(V))
               : std::get<Ty *>(V);
  }

  template <typename ModTy, typename... ArgTys>
  static ModTy make_modification(ArgTys &&...Args) {
    return ModTy(std::forward<ArgTys>(Args)...);
  }

  template <typename ModTy, typename... ArgTys>
  static std::unique_ptr<ModTy> make_unique_modification(ArgTys &&...Args) {
    return std::unique_ptr<ModTy>(new ModTy(std::forward<ArgTys>(Args)...));
  }
};

//===----------------------------------------------------------------------===//
//                          AddFeatureToModel
//===----------------------------------------------------------------------===//

class AddFeatureToModel : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  Feature *operator()(FeatureModel &FM) {
    auto *InsertedFeature = addFeature(FM, std::move(NewFeature));
    if (!InsertedFeature) {
      return nullptr;
    }
    if (Parent) {
      setParent(*InsertedFeature, *Parent);
      addEdge(*Parent, *InsertedFeature);
    } else if (FM.getRoot()) {
      setParent(*InsertedFeature, *FM.getRoot());
      addEdge(*FM.getRoot(), *InsertedFeature);
    }
    return InsertedFeature;
  }

private:
  AddFeatureToModel(std::unique_ptr<Feature> NewFeature,
                    Feature *Parent = nullptr)
      : NewFeature(std::move(NewFeature)), Parent(Parent) {}

  std::unique_ptr<Feature> NewFeature;
  Feature *Parent;
};

//===----------------------------------------------------------------------===//
//                              AddRelationshipToModel
//===----------------------------------------------------------------------===//

class AddRelationshipToModel : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  Relationship *operator()(FeatureModel &FM) {
    auto *InsertedRelationship =
        addRelationship(FM, std::make_unique<Relationship>(Kind));
    if (!InsertedRelationship) {
      return nullptr;
    }
    auto *P = resolveVariant(FM, Parent);
    setParent(*InsertedRelationship, *P);
    addEdge(*P, *InsertedRelationship);
    if (std::holds_alternative<std::set<std::string>>(Children)) {
      for (auto Child : std::get<std::set<std::string>>(Children)) {
        auto *C = resolveVariant<Feature>(FM, Child);
        removeEdge(*C->getParent(), *C);
        addEdge(*InsertedRelationship, *C);
        setParent(*C, *InsertedRelationship);
      }
    } else {
      for (auto *C : std::get<std::set<Feature *>>(Children)) {
        removeEdge(*C->getParent(), *C);
        addEdge(*InsertedRelationship, *C);
        setParent(*C, *InsertedRelationship);
      }
    }
    return InsertedRelationship;
  }

private:
  AddRelationshipToModel(
      Relationship::RelationshipKind Kind, FeatureVariant Parent,
      std::variant<std::set<std::string>, std::set<Feature *>> Children)
      : Kind(Kind), Parent(std::move(Parent)), Children(std::move(Children)) {}

  Relationship::RelationshipKind Kind;
  FeatureVariant Parent;
  std::variant<std::set<std::string>, std::set<Feature *>> Children;
};

//===----------------------------------------------------------------------===//
//                       AddConstraintToModel
//===----------------------------------------------------------------------===//

class AddConstraintToModel : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  Constraint *operator()(FeatureModel &FM) {
    auto *InsertedConstraint = addConstraint(FM, std::move(NewConstraint));
    if (!InsertedConstraint) {
      return nullptr;
    }
    auto V = AddConstraintToModelVisitor(&FM);
    InsertedConstraint->accept(V);
    return InsertedConstraint;
  }

private:
  class AddConstraintToModelVisitor : public ConstraintVisitor {

  public:
    AddConstraintToModelVisitor(FeatureModel *FM) : FM(FM) {}

    void visit(PrimaryFeatureConstraint *C) override {
      auto *F = FM->getFeature(C->getFeature()->getName());
      AddConstraintToModel::setFeature(*C, *F);
      AddConstraintToModel::addConstraint(*F, *C);
    };

  private:
    FeatureModel *FM;
  };

  AddConstraintToModel(std::unique_ptr<Constraint> NewConstraint)
      : NewConstraint(std::move(NewConstraint)) {}

  std::unique_ptr<Constraint> NewConstraint;
};

//===----------------------------------------------------------------------===//
//                              SetName
//===----------------------------------------------------------------------===//

class SetName : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  void operator()(FeatureModel &FM) { setName(FM, Name); }

private:
  SetName(std::string Name) : Name(std::move(Name)) {}

  std::string Name;
};

//===----------------------------------------------------------------------===//
//                              SetCommit
//===----------------------------------------------------------------------===//

class SetCommit : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  void operator()(FeatureModel &FM) { setCommit(FM, Commit); }

private:
  SetCommit(std::string Commit) : Commit(std::move(Commit)) {}

  std::string Commit;
};

//===----------------------------------------------------------------------===//
//                              SetPath
//===----------------------------------------------------------------------===//

class SetPath : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  void operator()(FeatureModel &FM) { setPath(FM, Path); }

private:
  SetPath(fs::path Path) : Path(std::move(Path)) {}

  fs::path Path;
};

//===----------------------------------------------------------------------===//
//                              SetRoot
//===----------------------------------------------------------------------===//

class SetRoot : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  RootFeature *operator()(FeatureModel &FM) {
    if (auto *NewRoot = llvm::dyn_cast_or_null<RootFeature>(
            addFeature(FM, std::move(Root)));
        NewRoot) {
      if (FM.getRoot()) {
        for (auto *C : FM.getRoot()->children()) {
          setParent(*C, *NewRoot);
          removeEdge(*FM.getRoot(), *C);
          addEdge(*NewRoot, *C);
        }
        removeFeature(FM, *FM.getRoot());
      }
      setRoot(FM, *NewRoot);
    }
    sort(FM);
    return FM.getRoot();
  }

private:
  SetRoot(std::unique_ptr<RootFeature> Root) : Root(std::move(Root)) {}

  std::unique_ptr<RootFeature> Root;
};

//===----------------------------------------------------------------------===//
//                              AddChild
//===----------------------------------------------------------------------===//

class AddChild : public FeatureModelModification {
  friend class FeatureModelModification;

public:
  void exec(FeatureModel &FM) override { (*this)(FM); }

  void operator()(FeatureModel &FM) {
    auto *C = resolveVariant(FM, Child);
    auto *P = resolveVariant(FM, Parent);
    removeEdge(*C->getParent(), *C);
    addEdge(*P, *C);
    setParent(*C, *P);
    sort(FM);
  }

private:
  AddChild(FeatureTreeNodeVariant Parent, FeatureTreeNodeVariant Child)
      : Child(std::move(Child)), Parent(std::move(Parent)) {}

  FeatureTreeNodeVariant Child;
  FeatureTreeNodeVariant Parent;
};

class FeatureModelCopyTransactionBase {
protected:
  FeatureModelCopyTransactionBase(FeatureModel &FM) : FM(FM.clone()) {}

  [[nodiscard]] inline std::unique_ptr<FeatureModel> commitImpl() {
    if (FM && ConsistencyCheck::isFeatureModelValid(*FM)) {
      return std::move(FM);
    }
    return nullptr;
  };

  void abortImpl() { FM.reset(); }

  [[nodiscard]] inline bool isUncommited() const { return FM != nullptr; }

  //===--------------------------------------------------------------------===//
  // Modifications

  Feature *addFeatureImpl(std::unique_ptr<Feature> NewFeature,
                          Feature *Parent) {
    if (!FM) {
      return nullptr;
    }

    if (Parent) {
      // To correctly add a parent, we need to translate it to a Feature in
      // our copied FeatureModel
      return FeatureModelModification::make_modification<AddFeatureToModel>(
          std::move(NewFeature), TranslateFeature(*Parent))(*FM);
    }

    return FeatureModelModification::make_modification<AddFeatureToModel>(
        std::move(NewFeature))(*FM);
  }

  Relationship *addRelationshipImpl(
      Relationship::RelationshipKind Kind, FeatureVariant Parent,
      const std::variant<std::set<std::string>, std::set<Feature *>>
          &Children) {
    if (!FM) {
      return nullptr;
    }

    std::set<Feature *> TranslatedChildren;
    if (std::holds_alternative<std::set<std::string>>(Children)) {
      for (auto C : std::get<std::set<std::string>>(Children)) {
        TranslatedChildren.insert(TranslateFeature(
            *AddRelationshipToModel::resolveVariant<Feature>(*FM, C)));
      }
    } else {
      for (auto *C : std::get<std::set<Feature *>>(Children)) {
        TranslatedChildren.insert(TranslateFeature(
            *AddRelationshipToModel::resolveVariant<Feature>(*FM, C)));
      }
    }

    return FeatureModelModification::make_modification<AddRelationshipToModel>(
        Kind,
        TranslateFeature(
            *AddRelationshipToModel::resolveVariant(*FM, std::move(Parent))),
        TranslatedChildren)(*FM);
  }

  Constraint *addConstraintImpl(std::unique_ptr<Constraint> NewConstraint) {
    if (!FM) {
      return nullptr;
    }

    return FeatureModelModification::make_modification<AddConstraintToModel>(
        std::move(NewConstraint))(*FM);
  }

  void setNameImpl(std::string Name) {
    assert(FM && "");

    FeatureModelModification::make_modification<SetName>(std::move(Name))(*FM);
  }

  void setCommitImpl(std::string Commit) {
    assert(FM && "");

    FeatureModelModification::make_modification<SetCommit>(std::move(Commit))(
        *FM);
  }

  void setPathImpl(fs::path Path) {
    assert(FM && "");

    FeatureModelModification::make_modification<SetPath>(std::move(Path))(*FM);
  }

  RootFeature *setRootImpl(std::unique_ptr<RootFeature> Root) {
    if (!FM) {
      return nullptr;
    }

    return FeatureModelModification::make_modification<SetRoot>(
        std::move(Root))(*FM);
  }

  static void addChildImpl(const FeatureTreeNodeVariant &Parent,
                           const FeatureTreeNodeVariant &Child) {
    FeatureModelModification::make_modification<AddChild>(Parent, Child);
  }

private:
  std::unique_ptr<FeatureModel> FM;

  [[nodiscard]] Feature *TranslateFeature(Feature &F) {
    return FM->getFeature(F.getName());
  }
};

class FeatureModelModifyTransactionBase {
protected:
  FeatureModelModifyTransactionBase(FeatureModel &FM) : FM(&FM) {}

  bool commitImpl() {
    assert(FM && "Cannot commit Modifications without a FeatureModel present.");
    if (FM) {
      std::for_each(
          Modifications.begin(), Modifications.end(),
          [this](const std::unique_ptr<FeatureModelModification> &FMM) {
            FMM->exec(*FM);
          });
      if (ConsistencyCheck::isFeatureModelValid(*FM)) {
        FM = nullptr;
        return true;
      }
      // TODO (se-passau/VaRA#723): implement rollback
    }
    return false;
  };

  void abortImpl() { Modifications.clear(); };

  [[nodiscard]] inline bool isUncommited() const { return FM != nullptr; }

  //===--------------------------------------------------------------------===//
  // Modifications

  void addFeatureImpl(std::unique_ptr<Feature> NewFeature, Feature *Parent) {
    assert(FM && "");

    Modifications.push_back(
        FeatureModelModification::make_unique_modification<AddFeatureToModel>(
            std::move(NewFeature), Parent));
  }

  void addRelationshipImpl(Relationship::RelationshipKind Kind,
                           const FeatureVariant &Parent,
                           const std::variant<std::set<std::string>,
                                              std::set<Feature *>> &Children) {
    Modifications.push_back(FeatureModelModification::make_unique_modification<
                            AddRelationshipToModel>(Kind, Parent, Children));
  }

  void addConstraintImpl(std::unique_ptr<Constraint> NewConstraint) {
    Modifications.push_back(FeatureModelModification::make_unique_modification<
                            AddConstraintToModel>(std::move(NewConstraint)));
  }

  void setNameImpl(std::string Name) {
    assert(FM && "");

    Modifications.push_back(
        FeatureModelModification::make_unique_modification<SetName>(
            std::move(Name)));
  }

  void setCommitImpl(std::string Commit) {
    assert(FM && "");

    Modifications.push_back(
        FeatureModelModification::make_unique_modification<SetCommit>(
            std::move(Commit)));
  }

  void setPathImpl(fs::path Path) {
    assert(FM && "");

    Modifications.push_back(
        FeatureModelModification::make_unique_modification<SetPath>(
            std::move(Path)));
  }

  void setRootImpl(std::unique_ptr<RootFeature> Root) {
    assert(FM && "");

    Modifications.push_back(
        FeatureModelModification::make_unique_modification<SetRoot>(
            std::move(Root)));
  }

  void addChildImpl(const FeatureTreeNodeVariant &Parent,
                    const FeatureTreeNodeVariant &Child) {
    assert(FM && "");

    Modifications.push_back(
        FeatureModelModification::make_unique_modification<AddChild>(Parent,
                                                                     Child));
  }

private:
  FeatureModel *FM;
  std::vector<std::unique_ptr<FeatureModelModification>> Modifications;
};

} // namespace detail

} // namespace vara::feature

#endif // VARA_FEATURE_FEATUREMODELTRANSACTION_H
