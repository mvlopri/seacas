// Copyright(C) 1999-2020 National Technology & Engineering Solutions
// of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
// NTESS, the U.S. Government retains certain rights in this software.
//
// See packages/seacas/LICENSE for details

#include "gtest/gtest.h"
#include <catalyst_tests/Iocatalyst_BlockMeshSet.h>
#include <cstddef>

class Iocatalyst_DatabaseIOTest : public ::testing::Test
{
protected:
  Iocatalyst::BlockMeshSet         bmSet;
  Iocatalyst::BlockMesh::Partition part;
  Iocatalyst::BlockMesh::Extent    blockMeshSize;
  Iocatalyst::BlockMesh::Extent    origin;
  Ioss::ParallelUtils              putils;

  Iocatalyst_DatabaseIOTest();

  bool regionsAreEqual(const std::string &fileName, const std::string &catFileName,
                       const std::string &iossDatabaseType);

  void runStructuredTest(const std::string &testName);

  void runUnstructuredTest(const std::string &testName);

  void checkZeroCopyFields(Iocatalyst::BlockMeshSet::IOSSparams &iop);

  template <typename EntityContainer>
  void checkEntityContainerZeroCopyFields(const EntityContainer &ge)
  {
    for (auto g : ge) {
      auto nameList = g->field_describe();
      for (auto name : nameList) {
        auto field = g->get_fieldref(name);
        if (field.zero_copy_enabled()) {
          std::vector<std::byte> dcBuffer(field.get_size());
          g->get_field_data(name, dcBuffer.data(), dcBuffer.size());
          void  *data;
          size_t dataSize;
          g->get_field_data(name, &data, &dataSize);
          std::byte             *b = static_cast<std::byte *>(data);
          std::vector<std::byte> zcBuffer(b, b + field.get_size());
          EXPECT_EQ(dcBuffer, zcBuffer);
        }
      }
    }
  }

  void setBlockMeshSize(unsigned int i, unsigned int j, unsigned int k);
  void setOrigin(unsigned int i, unsigned int j, unsigned int k);
  void addBlockMesh(Iocatalyst::BlockMesh &blockMesh);

  const std::string CGNS_DATABASE_TYPE        = "cgns";
  const std::string CGNS_FILE_EXTENSION       = ".cgns";
  const std::string EXODUS_DATABASE_TYPE      = "exodus";
  const std::string EXODUS_FILE_EXTENSION     = ".ex2";
  const std::string CATALYST_TEST_FILE_PREFIX = "catalyst_";
  const std::string CATALYST_TEST_FILE_NP     = "_np_";
};
