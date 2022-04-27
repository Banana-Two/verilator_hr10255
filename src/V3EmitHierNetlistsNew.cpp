/*************************************************************************
  > File Name: src/V3EmitHierNetlistsNew.cpp
  > Author: 16hxliang3
  > Mail: 16hxliang3@stu.edu.cn
  > Created Time: Mon 11 Apr 2022 08:18:10 PM CST
 ************************************************************************/
#include "V3EmitNetlistsNew.h"
#include "V3Number.h"

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "V3Ast.h"
#include "V3Error.h"
#include "netlistsdefine.h"

namespace MultipleBitsNetlist
{
struct ModAndItsHierLevel
{
    std::string moduleDefName;
    uint32_t level;
    bool isFlatted = false;
};
// When writing visit functions, there are some keys we should know:
// (1)Everytime we only write either AstAssignW/AstAssign or AstPin information
// until all its information have been obtained.
// (2)Only AstVarRef or AstSel can be lValue of assign statement.
// (3)AstConst, AstExtend and AstConcat can't be lValue of assign statement.
// (4)Know the difference between m_nextp and m_opxp, some information about
// them I have written before the declaration of visit function.
// (5)AstAssign only has one bit information.
// (6)We only can writ one AstVarRef information at the same time.
// (7)Only such AstNode that has children pointed by m_opxp and we need the
// information of its children can call iterateChildren(nodep) function.
class HierCellsNetListsVisitor final : public AstNVisitor
{
  public:
    // std::string -> Current Module name.
    // Module -> All information about current module include port,
    // submodule instance, assign statement and so on.
    // AstNetlist
    std::unordered_map<std::string, Module> _modsNameMapTheirDefinition;
    // use to optimize flattern netlist.
    // AstNetlist
    std::vector<ModAndItsHierLevel> _modAndItsHierLevels;

  private:
    // A module = ItsName + Port  + Wire + Assign staement + Submodule Instance
    // AstModule
    std::string _curModuleName;
    // AstCell
    std::string _curSubmoduleName;
    std::string _curSubmoduleInstanceName;

    // AstAssignW/AstAssign Status
    bool _isAssignStatement = false;
    bool _isAssignStatementLvalue = false;
    // Themporary message of current visited assign statement
    MultipleBitsAssignStatement _multipleBitsAssignStatementTmp;

    // Themporary instance message of current visited port
    MultipleBitsPortAssignment _multipleBitsPortAssignmentTmp;
    // Port Instance message of current visited submodule instance
    std::vector<MultipleBitsPortAssignment>
      _curSubModInsMultipleBitsPortAssignmentsTmp;

    // AstSel Status:1 = m_op1p, 2 = m_op2p, 3 = m_op3p, 4 = m_op4p
    uint32_t _whichAstSelChildren = 0;
    MultipleBitsVarRef _multipleBitsVarRefTmp;

  private:
    // All information we can get from this Ast tree by using the
    // polymorphic preperties to call different visit function. And
    // AstNetlist is the first node of the Ast tree. Becaus we have no
    // information needed to get from it, we only iterate over it to
    // access its children nodes.

    // A visit function will be popped up from or not pushed to the function
    // stack after finishing obtainning data only in the following three cases:
    // (1)node has no children. For example, AstConst node.(Not pushed to)
    // (2)node is pointed by m_nextp and its children pointed by m_opxp have
    // been visited, such as AstAssignW node.(Popped up from)
    // (3)node is pointed by m_opxp and its children pointed by m_opxp have
    // been visited. But its iterateAndNext() function will be popped up from
    // function stack only when its all descendants have been visited such as
    // AstModule node.(Popped up from)
    // Note: m_opxp = m_op1p or m_op2p or m_op3p or m_op4p

    // m_opxp means parent node owns its children.
    // m_nextp means parent node and its children is parallel in verilog source
    // code.
    // For example, if A -> B by m_opxp, A owns B and B's descendants,like
    // AstAssign can own AstVarRef.
    // If A -> B by m_nextp, A and B are parallel, like AstAssign and AstAssign
    // or AstCell.
    virtual void visit(AstNode *nodep) override { iterateChildren(nodep); };
    virtual void visit(AstNetlist *nodep) override { iterateChildren(nodep); };

    virtual void visit(AstModule *nodep) override;
    virtual void visit(AstVar *nodep) override;
    virtual void visit(AstAssignW *nodep) override;
    virtual void visit(AstAssign *nodep) override;
    virtual void visit(AstCell *nodep) override;
    virtual void visit(AstPin *nodep) override;
    virtual void visit(AstConcat *nodep) override { iterateChildren(nodep); };
    virtual void visit(AstSel *nodep) override;
    virtual void visit(AstVarRef *nodep) override;
    virtual void visit(AstExtend *nodep) override;
    virtual void visit(AstExtendS *nodep) override;
    virtual void visit(AstReplicate *nodep) override;
    virtual void visit(AstConst *nodep) override;
    // Prevent idling iteration
    virtual void visit(AstTypeTable *nodep) override { return; }

  public:
    std::unordered_map<std::string, Module> GetHierCellsNetLists();

  public:
    // AstNetlist is ConElement
    HierCellsNetListsVisitor(AstNetlist *nodep) { nodep->accept(*this); }
    virtual ~HierCellsNetListsVisitor() override{};
};

// Get module name and hierLevel,
// create Module object to store module information.
void HierCellsNetListsVisitor::visit(AstModule *nodep)
{
  auto createModule = [this](const std::string &moduleDefName,
                             uint32_t level = -1) -> void
  {
    Module moduleTmp;
    moduleTmp.moduleDefName = moduleDefName;
    moduleTmp.level = level;
    _modsNameMapTheirDefinition[moduleDefName] = std::move(moduleTmp);
    _curModuleName = moduleDefName;

    ModAndItsHierLevel modAndItsHierLevel;
    modAndItsHierLevel.moduleDefName = moduleDefName;
    modAndItsHierLevel.level = level;
    _modAndItsHierLevels.push_back(std::move(modAndItsHierLevel));
  };

  if(nodep->prettyName() == "@CONST-POOL@")
  {
    return;
  }
  else
  {
    createModule(nodep->prettyName(), nodep->level());
  }
  iterateChildren(nodep);
}

void HierCellsNetListsVisitor::visit(AstVar *nodep)
{
  PortDefinition portDefinition;

  if(nodep->isIO())
  {
    switch(nodep->direction())
    {
    case VDirection::INPUT:
      portDefinition.portType = PortType::INPUT;
      break;
    case VDirection::OUTPUT:
      portDefinition.portType = PortType::OUTPUT;
      break;
    case VDirection::INOUT:
      portDefinition.portType = PortType::INOUT;
      break;
    default:
      throw std::runtime_error(
        "only support input，output and inout, other like ref or "
        "constref aren't not support!!!");
      break;
    }
  }
  else if(nodep->isGParam())
  {
    std::cout << "We know " << nodep->prettyName() << " is a parameter in "
              << _curModuleName << ". But we don't care about it."
              << std::endl;
    return;
  }
  else
  {
    portDefinition.portType = PortType::WIRE;
  }
  portDefinition.portDefName = nodep->prettyName();

  if(nodep->basicp() && nodep->basicp()->width() != 1)
  {
    portDefinition.isVector = true;
    portDefinition.bitWidth = nodep->basicp()->width();
  }

  switch(portDefinition.portType)
  {
  case PortType::INPUT:
    _modsNameMapTheirDefinition[_curModuleName].inputs.push_back(
      std::move(portDefinition));
    break;
  case PortType::OUTPUT:
    _modsNameMapTheirDefinition[_curModuleName].outputs.push_back(
      std::move(portDefinition));
    break;
  case PortType::INOUT:
    _modsNameMapTheirDefinition[_curModuleName].inouts.push_back(
      std::move(portDefinition));
    break;
  case PortType::WIRE:
    _modsNameMapTheirDefinition[_curModuleName].wires.push_back(
      std::move(portDefinition));
    break;
  default:
    break;
  }
}

void HierCellsNetListsVisitor::visit(AstAssignW *nodep)
{
  _isAssignStatement = true;
  _multipleBitsAssignStatementTmp.rValue.clear();
  iterateChildren(nodep);
  _modsNameMapTheirDefinition[_curModuleName].assigns.push_back(
    _multipleBitsAssignStatementTmp);
  _isAssignStatement = false;
}

void HierCellsNetListsVisitor::visit(AstAssign *nodep)
{
  _isAssignStatement = true;
  _multipleBitsAssignStatementTmp.rValue.clear();
  iterateChildren(nodep);
  _modsNameMapTheirDefinition[_curModuleName].assigns.push_back(
    _multipleBitsAssignStatementTmp);
  _isAssignStatement = false;
}

void HierCellsNetListsVisitor::visit(AstCell *nodep)
{
  _curSubmoduleName = nodep->modp()->prettyName();
  _curSubmoduleInstanceName = nodep->prettyName();
  _curSubModInsMultipleBitsPortAssignmentsTmp.clear();
  iterateChildren(nodep);
  _modsNameMapTheirDefinition[_curModuleName]
    .subModInsNameMapPortAssignments[_curSubmoduleInstanceName] =
    _curSubModInsMultipleBitsPortAssignmentsTmp;
  _modsNameMapTheirDefinition[_curModuleName]
    .subModInsNameMapSubModDefName[_curSubmoduleInstanceName] =
    _curSubmoduleName;
  _modsNameMapTheirDefinition[_curModuleName].subModuleInstanceNames.push_back(
    _curSubmoduleInstanceName);
}

void HierCellsNetListsVisitor::visit(AstPin *nodep)
{
  _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.clear();
  _multipleBitsPortAssignmentTmp.portDefName = nodep->modVarp()->name();
  iterateChildren(nodep);
  _curSubModInsMultipleBitsPortAssignmentsTmp.push_back(
    _multipleBitsPortAssignmentTmp);
}

void HierCellsNetListsVisitor::visit(AstSel *nodep)
{
  _whichAstSelChildren = 1;
  iterateChildren(nodep);
  if(_isAssignStatement)
  { // Now, AstSel is a child or a descendant of AstAssignW
    if(_isAssignStatementLvalue)
    { // Now, AstSel is a child of AstAssignW
      _multipleBitsAssignStatementTmp.lValue = _multipleBitsVarRefTmp;
      _isAssignStatementLvalue = false;
    }
    else
    { // Now, AstSel is a child of AstAssign or AstExtend or AstConcat
      _multipleBitsAssignStatementTmp.rValue.push_back(_multipleBitsVarRefTmp);
    }
  }
  else
  {
    // Now, AstVarRef is a child of AstPin or AstExtend or AstConcat
    _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.push_back(
      _multipleBitsVarRefTmp);
  }
  _whichAstSelChildren = 0;
}

// If AstVarRef is a child of AstSel, it references some part of a var.
// Sometimes, whole part.
// For example, C[2:1];
// Otherwise, it references the whole part of a var.
// For example, C[n-1:0] or ci;
void HierCellsNetListsVisitor::visit(AstVarRef *nodep)
{
  _multipleBitsVarRefTmp.varRefName = nodep->prettyName();
  if(_whichAstSelChildren)
  { // Now, AstVarRef is a child of AstSel
    _whichAstSelChildren++;
    if(_isAssignStatement && (nodep->access() == VAccess::WRITE))
    {
      _isAssignStatementLvalue = true;
    }
  }
  else
  {
    _multipleBitsVarRefTmp.hasValueX = false;
    // Make sure all var like A[1:3] or A[3:1] become A[2:0]
    if(nodep->dtypep()->basicp()->nrange().left() >
       nodep->dtypep()->basicp()->nrange().right())
    {
      _multipleBitsVarRefTmp.varRefRange.end =
        nodep->dtypep()->basicp()->nrange().left();
      _multipleBitsVarRefTmp.varRefRange.start =
        nodep->dtypep()->basicp()->nrange().right();
    }
    else
    {
      _multipleBitsVarRefTmp.varRefRange.end =
        nodep->dtypep()->basicp()->nrange().right();
      _multipleBitsVarRefTmp.varRefRange.start =
        nodep->dtypep()->basicp()->nrange().left();
    }
    if(_multipleBitsVarRefTmp.varRefRange.start > 0)
    {
      _multipleBitsVarRefTmp.varRefRange.end =
        _multipleBitsVarRefTmp.varRefRange.end -
        _multipleBitsVarRefTmp.varRefRange.start;
      _multipleBitsVarRefTmp.varRefRange.start = 0;
    }
    _multipleBitsVarRefTmp.width = _multipleBitsVarRefTmp.varRefRange.end -
                                   _multipleBitsVarRefTmp.varRefRange.start +
                                   1;
    if(_multipleBitsVarRefTmp.width > 1)
      _multipleBitsVarRefTmp.isVector = true;
    else
      _multipleBitsVarRefTmp.isVector = false;
    if(_isAssignStatement)
    { // Now, AstVarRef is a child of AstAssign or a descendant of AstAssignW
      if(nodep->access() == VAccess::WRITE)
      { // Now, AstVarRef is a child of AstAssign
        _multipleBitsAssignStatementTmp.lValue = _multipleBitsVarRefTmp;
      }
      else
      { // Now, AstVarRef is a child of AstAssign or AstExtend or AstConcat or
        // AstReplicate
        _multipleBitsAssignStatementTmp.rValue.push_back(
          _multipleBitsVarRefTmp);
      }
    }
    else
    {
      // Now, AstVarRef is a child of AstPin or AstExtend or AstConcat or
      // AstReplicate
      _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.push_back(
        _multipleBitsVarRefTmp);
    }
  }
}

void HierCellsNetListsVisitor::visit(AstExtend *nodep)
{
  uint32_t extendWidth = nodep->width() - nodep->lhsp()->width();
  _multipleBitsVarRefTmp.varRefName = "";
  _multipleBitsVarRefTmp.constValueAndValueX.value = 0;
  _multipleBitsVarRefTmp.constValueAndValueX.valueX = 0;
  if(extendWidth > 1)
    _multipleBitsVarRefTmp.isVector = true;
  else
    _multipleBitsVarRefTmp.isVector = false;
  _multipleBitsVarRefTmp.hasValueX = false;
  _multipleBitsVarRefTmp.width = std::move(extendWidth);
  if(_isAssignStatement)
  { // Now, AstExtend is a child of AstAssign or AstAssignW or AstConcat
    _multipleBitsAssignStatementTmp.rValue.push_back(_multipleBitsVarRefTmp);
  }
  else
  { // Now, AstExtend is a child of AstPin or AstConcat
    _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.push_back(
      _multipleBitsVarRefTmp);
  }
  iterateChildren(nodep);
}

void HierCellsNetListsVisitor::visit(AstExtendS *nodep)
{
  uint32_t extendSWidth = nodep->width() - nodep->lhsp()->width();
  _multipleBitsVarRefTmp.varRefName = "";
  _multipleBitsVarRefTmp.constValueAndValueX.value = (1 << extendSWidth) - 1;
  _multipleBitsVarRefTmp.constValueAndValueX.valueX = 0;
  if(extendSWidth > 1)
    _multipleBitsVarRefTmp.isVector = true;
  else
    _multipleBitsVarRefTmp.isVector = false;
  _multipleBitsVarRefTmp.hasValueX = false;
  _multipleBitsVarRefTmp.width = std::move(extendSWidth);
  if(_isAssignStatement)
  { // Now, AstExtend is a child of AstAssign or AstAssignW or AstConcat
    _multipleBitsAssignStatementTmp.rValue.push_back(_multipleBitsVarRefTmp);
  }
  else
  { // Now, AstExtend is a child of AstPin or AstConcat
    _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.push_back(
      _multipleBitsVarRefTmp);
  }
  iterateChildren(nodep);
}

void HierCellsNetListsVisitor::visit(AstReplicate *nodep)
{
  iterateChildren(nodep);
  if(_isAssignStatement)
  { // Now, AstReplicate is a child of AstAssignW or AstExtend or AstConcat
    uint32_t replicateTimes =
      _multipleBitsAssignStatementTmp.rValue.back().constValueAndValueX.value;
    _multipleBitsAssignStatementTmp.rValue.pop_back();
    while(replicateTimes != 1)
    {
      _multipleBitsAssignStatementTmp.rValue.push_back(
        _multipleBitsAssignStatementTmp.rValue.back());
      replicateTimes--;
    }
  }
  else
  { // Now, AstReplicate is a child of AstPin or AstExtend or AstConcat
    uint32_t replicateTimes =
      _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.back()
        .constValueAndValueX.value;
    _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.pop_back();
    while(replicateTimes != 1)
    {
      _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.push_back(
        _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.back());
      replicateTimes--;
    }
  }
}

void HierCellsNetListsVisitor::visit(AstConst *nodep)
{
  if(_whichAstSelChildren == 2)
  { // Now, AstConst is a child of AstSel
    _whichAstSelChildren++;
    _multipleBitsVarRefTmp.varRefRange.start =
      nodep->num().value().getValue32();
  }
  else if(_whichAstSelChildren == 3)
  { // Now, AstConst is a child of AstSel
    _multipleBitsVarRefTmp.width = nodep->num().value().getValue32();
    _multipleBitsVarRefTmp.varRefRange.end =
      _multipleBitsVarRefTmp.varRefRange.start + _multipleBitsVarRefTmp.width -
      1;
    _multipleBitsVarRefTmp.isVector = true;
    _multipleBitsVarRefTmp.hasValueX = false;
  }
  else
  { // Now, AstConst is a rValue of assign statement or refValue of a port or
    // the number of AstReplicate.
    _multipleBitsVarRefTmp.varRefName = "";
    _multipleBitsVarRefTmp.constValueAndValueX.value =
      nodep->num().value().getValue32();
    _multipleBitsVarRefTmp.width = nodep->width();
    if(_multipleBitsVarRefTmp.width > 1)
      _multipleBitsVarRefTmp.isVector = true;
    else
      _multipleBitsVarRefTmp.isVector = false;
    if(nodep->num().isAnyXZ())
    { // Now, the const value has value x or z.
      _multipleBitsVarRefTmp.constValueAndValueX.valueX =
        nodep->num().value().getValueX32();
      _multipleBitsVarRefTmp.hasValueX = true;
    }
    else
    {
      _multipleBitsVarRefTmp.constValueAndValueX.valueX = 0;
      _multipleBitsVarRefTmp.hasValueX = false;
    }
    if(_multipleBitsVarRefTmp.width > 32)
      _multipleBitsVarRefTmp.biggerValue.push_back(
        nodep->num().value().getValueAndX64());
    if(_multipleBitsVarRefTmp.width > 64)
    {
      const std::vector<V3NumberData::ValueAndX> valueAndX128Tmp =
        nodep->num().value().getValueAndX128();
      _multipleBitsVarRefTmp.biggerValue.insert(
        _multipleBitsVarRefTmp.biggerValue.end(), valueAndX128Tmp.begin(),
        valueAndX128Tmp.end());
    }
    if(_isAssignStatement)
    { // Now, AstConst is a child of AstAssign or AstAssignW or AstConcat or
      // AstReplicate
      _multipleBitsAssignStatementTmp.rValue.push_back(_multipleBitsVarRefTmp);
      if(_multipleBitsVarRefTmp.width > 32)
        _multipleBitsVarRefTmp.biggerValue.clear();
    }
    else
    { // Now, AstConst is a child of AstPin or AstConcat or AstReplicate
      _multipleBitsPortAssignmentTmp.multipleBitsVarRefs.push_back(
        _multipleBitsVarRefTmp);
      if(_multipleBitsVarRefTmp.width > 32)
        _multipleBitsVarRefTmp.biggerValue.clear();
    }
  }
}

std::unordered_map<std::string, Module>
HierCellsNetListsVisitor::GetHierCellsNetLists()
{
  return _modsNameMapTheirDefinition;
}

void V3EmitHierNetLists::emitHierNetLists(
  std::unordered_map<std::string, Module> &hierCellsNetLists)
{
  // v3Global will return a AstNetlist*
  HierCellsNetListsVisitor hierCellsNetListsVisitor(v3Global.rootp());
  hierCellsNetLists = hierCellsNetListsVisitor.GetHierCellsNetLists();
}

void V3EmitHierNetLists::multipleBitsToOneBit(
  std::unordered_map<std::string, MultipleBitsNetlist::Module>
    &multipleBitsHierCellsNetLists,
  std::unordered_map<std::string, OneBitNetlist::Module>
    &oneBitHierCellsNetLists)
{
  std::string curModuleName;

  OneBitNetlist::AssignStatement oneBitAssignStatement;
  int lEnd, rEnd;
  uint32_t rWidth;
  uint32_t hotCode = 1 << 31;

  std::string curSubmoduleInstanceName;
  OneBitNetlist::PortAssignment oPortAssignment;
  OneBitNetlist::VarRef oVarRef;
  std::vector<OneBitNetlist::PortAssignment> oPortAssignments;

  for(auto &mBHCN: multipleBitsHierCellsNetLists)
  {
    curModuleName = mBHCN.first;
    oneBitHierCellsNetLists[curModuleName].moduleDefName =
      mBHCN.second.moduleDefName;
    oneBitHierCellsNetLists[curModuleName].level = mBHCN.second.level;
    oneBitHierCellsNetLists[curModuleName].inputs = mBHCN.second.inputs;
    oneBitHierCellsNetLists[curModuleName].outputs = mBHCN.second.outputs;
    oneBitHierCellsNetLists[curModuleName].inouts = mBHCN.second.inouts;
    oneBitHierCellsNetLists[curModuleName].wires = mBHCN.second.wires;
    oneBitHierCellsNetLists[curModuleName].subModuleInstanceNames =
      mBHCN.second.subModuleInstanceNames;
    oneBitHierCellsNetLists[curModuleName].subModInsNameMapSubModDefName =
      mBHCN.second.subModInsNameMapSubModDefName;

    for(auto &assigns: mBHCN.second.assigns)
    { // One AstAssignW/AstAssign Node
      auto &lValue = assigns.lValue;
      lEnd = lValue.varRefRange.end;
      for(auto &rValue: assigns.rValue)
      {
        oneBitAssignStatement.lValue.varRefName = lValue.varRefName;
        oneBitAssignStatement.lValue.isVector = lValue.isVector;
        oneBitAssignStatement.rValue.varRefName = rValue.varRefName;
        if(rValue.varRefName == "")
        {
          rWidth = rValue.width;
          oneBitAssignStatement.rValue.varRefName = "anonymous";
          oneBitAssignStatement.rValue.isVector = false;
          while(rWidth >= 1)
          {
            oneBitAssignStatement.rValue.initialVal =
              ((rValue.constValueAndValueX.value &
                (hotCode >> (32 - rWidth))) > 0)
                ? 1
                : 0;
            oneBitAssignStatement.lValue.index = lEnd;
            oneBitHierCellsNetLists[curModuleName].assigns.push_back(
              oneBitAssignStatement);
            rWidth--;
            lEnd--;
          }
        }
        else
        {
          rEnd = rValue.varRefRange.end;
          oneBitAssignStatement.rValue.varRefName = rValue.varRefName;
          oneBitAssignStatement.rValue.isVector = rValue.isVector;
          while(rEnd >= int(rValue.varRefRange.start))
          {
            oneBitAssignStatement.rValue.index = rEnd;
            oneBitAssignStatement.lValue.index = lEnd;
            oneBitHierCellsNetLists[curModuleName].assigns.push_back(
              oneBitAssignStatement);
            rEnd--;
            lEnd--;
          }
        }
      }
    }
    for(auto &sMINMPIM: mBHCN.second.subModInsNameMapPortAssignments)
    { // One AstCell
      curSubmoduleInstanceName = sMINMPIM.first;
      oPortAssignments.clear();
      for(auto &mPortAssignment: sMINMPIM.second)
      { // One AstPin
        oPortAssignment.portDefName = mPortAssignment.portDefName;
        oPortAssignment.varRefs.clear();
        for(auto &mVarRef: mPortAssignment.multipleBitsVarRefs)
        {
          if(mVarRef.varRefName == "")
          {
            rWidth = mVarRef.width;
            oVarRef.varRefName = "anonymous";
            oVarRef.isVector = false;
            while(rWidth >= 1)
            {
              oVarRef.initialVal = ((mVarRef.constValueAndValueX.value &
                                     (hotCode >> (32 - rWidth))) > 0)
                                     ? 1
                                     : 0;
              oPortAssignment.varRefs.push_back(oVarRef);
              rWidth--;
            }
          }
          else
          {
            rEnd = mVarRef.varRefRange.end;
            oVarRef.varRefName = mVarRef.varRefName;
            oVarRef.isVector = mVarRef.isVector;
            while(rEnd >= int(mVarRef.varRefRange.start))
            {
              oVarRef.index = rEnd;
              oPortAssignment.varRefs.push_back(oVarRef);
              rEnd--;
            }
          }
        }
        oPortAssignments.push_back(oPortAssignment);
      }
      oneBitHierCellsNetLists[curModuleName]
        .subModInsNameMapPortAssignments[curSubmoduleInstanceName] =
        oPortAssignments;
    }
  }
}
}
