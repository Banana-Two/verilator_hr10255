#pragma once

#include "netlistsdefine.h"

class V3EmitHierNetLists final {
public:
  static void emitHireNetLists(
      std::unordered_map<std::string, MoudleMsg> &hierCellsNetLists);
  static void printHireNetLists(
      std::unordered_map<std::string, MoudleMsg> &hierCellsNetLists,
      std::string filename);
};

class V3EmitPlainNetLists final {
public:
  static void emitPlainNetLists(
      std::unordered_map<std::string, MoudleMsg> &hierCellsNetLists,
      std::unordered_map<std::string, MoudleMsg> &plainCellsNetLists);
  static void printPlainNetLists(
      std::unordered_map<std::string, MoudleMsg> &hierCellsNetLists,
      std::string filename) {
    V3EmitHierNetLists::printHireNetLists(hierCellsNetLists, filename);
  }
};