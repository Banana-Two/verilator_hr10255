/*************************************************************************
  > File Name: VerilogNetlist.h
  > Author: 16hxliang3
  > Mail: 16hxliang3@stu.edu.cn
  > Created Time: Sat 23 Apr 2022 05:20:27 PM CST
 ************************************************************************/

#pragma once
#include "OneBitHierNetlist.h"
#include <cstdint>

class VerilogNetlist final
{
  private:
    // emptyStdCellsInjson : like PLL, which is empty in stdcells.json
    uint32_t _totalUsedNotEmptyStdCells;
    // stdCells : all written into LibBlackbox.v or stdcells.json
    uint32_t _totalUsedStdCells;
    // blackBoxes : including all stdCells and some empty module which
    // are written by hardware designer.
    uint32_t _totalUsedBlackBoxes;
    // notEmptyInstance : all stdCells Instances whose definition are
    // not empty in stdcells.json
    uint32_t _totalUsedNotEmptyInsInTop;
    // notTieConstantAssign : all assignments whose rvalue is not a constant,
    // for example, assing a = b, not a = 1'b1 or 1'b0 or 1'bz or 1'bx;
    uint32_t _totalNotTieConstantAssign;
    std::vector<Module> _hierNetlist;
    std::vector<Module> _flatNetlist;
    // given a moduel name, map it to the corresponding index in _hierNetlist
    // or _flatNetlist.
    std::unordered_map<std::string, uint32_t> _moduleNameMapIndex;

  public:
    VerilogNetlist() :
      _totalUsedStdCells(0), _totalUsedNotEmptyStdCells(0),
      _totalUsedBlackBoxes(0), _totalUsedNotEmptyInsInTop(0),
      _totalNotTieConstantAssign(0)
    {
      ;
    }
    const std::vector<Module> &hierNet() const { return _hierNetlist; }
    const std::vector<Module> &flatNet() const { return _flatNetlist; }
    uint32_t totalUsedStdCells() const { return _totalUsedStdCells; }
    uint32_t totalUsedNotEmptyStdCells() { return _totalUsedNotEmptyStdCells; }
    uint32_t totalUsedBlackBoxes() const { return _totalUsedBlackBoxes; }
    uint32_t totalNotTieConstantAssign() const
    {
      return _totalNotTieConstantAssign;
    }
    const std::unordered_map<std::string, uint32_t> &moduleNameMapIndex() const
    {
      return _moduleNameMapIndex;
    }
    void callFlattenHierNet()
    {
      flattenHierNet(_hierNetlist, _flatNetlist, _totalUsedBlackBoxes);
    }
    void printHierNet()
    {
      printNetlist(_hierNetlist, _totalUsedStdCells, _totalUsedBlackBoxes);
    }
    void printFlatNet()
    {
      printNetlist(_flatNetlist, _totalUsedStdCells, _totalUsedBlackBoxes,
                   "FlatNetlist.v",
                   _hierNetlist[_totalUsedBlackBoxes].level());
    }
    // Get a hierarchical netlist from ast
    void genHierNet(std::unordered_set<std::string> emptyStdCellsInJson = {
                      "MemGen_16_10", "PLL" });
    // Print a Netlist
    void printNetlist(const std::vector<Module> &hierNetlist,
                      const uint32_t totalUsedStdCells,
                      const uint32_t totalUsedBlackBoxes,
                      std::string fileName = "HierNetlist.v",
                      const uint32_t maxHierLevel = UINT32_MAX);
    // Flatten Hierarchical netlist
    void flattenHierNet(const std::vector<Module> &hierNetlist,
                        std::vector<Module> &flatNetlist,
                        const uint32_t totalUsedBlackBoxes);
    void parseHierNet(int argc, char **argv, char **env);

  private:
    void sortInsOrderInTop(const uint32_t moduleIndex);
    void sortAssignOrderInTop(const uint32_t moduleIndex);
    void computePortsPositionInOneMod(const uint32_t moduleIndex);
};
