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
    uint32_t _totalUsedStdCells;
    uint32_t _totalUsedNotEmptyStdCells;
    uint32_t _totalUsedBlackBoxes;
    std::vector<Module> _hierNetlist;
    std::vector<Module> _flatNetlist;

  public:
    const std::vector<Module> &getHierNet() const { return _hierNetlist; };
    const std::vector<Module> &getFlatNet() const { return _flatNetlist; };
    const uint32_t &getTotalUsedStdCells() const
    {
      return _totalUsedStdCells;
    };
    const uint32_t &getTotalUsedNotEmptyStdCells()
    {
      return _totalUsedNotEmptyStdCells;
    };
    const uint32_t &getTotalUsedBlackBoxes() const
    {
      return _totalUsedBlackBoxes;
    };
    void parseHierNet(int argc, char **argv, char **env);
    void callFlattenHierNet()
    {
      flattenHierNet(_hierNetlist, _flatNetlist, _totalUsedBlackBoxes);
    };
    void printHierNet()
    {
      printNetlist(_hierNetlist, _totalUsedStdCells, _totalUsedBlackBoxes);
    };
    void printFlatNet()
    {
      printNetlist(_flatNetlist, _totalUsedStdCells, _totalUsedBlackBoxes,
                   "FlatNetlist.v", _hierNetlist[_totalUsedBlackBoxes].level);
    };
    // Get a hierarchical netlist from ast
    void genHierNet(std::vector<Module> &hierNetlist,
                    uint32_t &totalUsedStdCells,
                    uint32_t &totalUsedNotEmptyStdCells,
                    uint32_t &totalUsedBlackBoxes);
    // Print a Netlist
    void printNetlist(const std::vector<Module> &hierNetlist,
                      const uint32_t &totalUsedStdCells,
                      const uint32_t &totalUsedBlackBoxes,
                      std::string fileName = "HierNetlist.v",
                      const uint32_t maxHierLevel = UINT32_MAX);
    // Flatten Hierarchical netlist
    void flattenHierNet(const std::vector<Module> &hierNetlist,
                        std::vector<Module> &flatNetlist,
                        const uint32_t &totalUsedBlackBoxes);
};
