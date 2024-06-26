// Copyright(C) 1999-2020, 2024 National Technology & Engineering Solutions
// of Sandia, LLC (NTESS).  Under the terms of Contract DE-NA0003525 with
// NTESS, the U.S. Government retains certain rights in this software.
//
// See packages/seacas/LICENSE for details

#include "Ioss_Transform.h"
#include "Ioss_TransformFactory.h"
#include <vector>

namespace Ioss {

  class Field;

  Transform *Transform::create(const std::string &transform)
  {
    return TransformFactory::create(transform);
  }

  bool Transform::execute(const Ioss::Field &field, void *data)
  {
    return internal_execute(field, data);
  }

  void Transform::set_property(const std::string & /*unused*/, int /*unused*/) {}
  void Transform::set_property(const std::string & /*unused*/, double /*unused*/) {}
  void Transform::set_properties(const std::string & /*unused*/,
                                 const std::vector<int> & /*unused*/)
  {
  }
  void Transform::set_properties(const std::string & /*unused*/,
                                 const std::vector<double> & /*unused*/)
  {
  }
} // namespace Ioss
