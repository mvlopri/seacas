// Copyright(C) 1999-2020 National Technology & Engineering Solutions
// of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
// NTESS, the U.S. Government retains certain rights in this software.
//
// See packages/seacas/LICENSE for details

#include "gtest/gtest.h"
#include <catalyst_tests/Iocatalyst_BlockMeshSet.h>

class Iocatalyst_DatabaseIOTest : public ::testing::Test
{
protected:
  Iocatalyst::BlockMeshSet         bmSet;
  Iocatalyst::BlockMesh::Partition part;
  Iocatalyst::BlockMesh::Extent    numBlocks;
  Ioss::ParallelUtils              putils;

  Iocatalyst_DatabaseIOTest();

  bool regionsAreEqual(const std::string &fileName, const std::string &catFileName,
                       const std::string &iossDatabaseType);

  void runStructuredTest(const std::string &testName);

  void runUnstructuredTest(const std::string &testName);

  void initBlock(Iocatalyst::BlockMesh &blockMesh, int x, int y, int z);

  const std::string CGNS_DATABASE_TYPE        = "cgns";
  const std::string CGNS_FILE_EXTENSION       = ".cgns";
  const std::string EXODUS_DATABASE_TYPE      = "exodus";
  const std::string EXODUS_FILE_EXTENSION     = ".ex2";
  const std::string CATALYST_TEST_FILE_PREFIX = "catalyst_";
  const std::string CATALYST_TEST_FILE_NP     = "_np_";
};