// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2015 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef OPENMVG_SFM_SFM_DATA_IO_PLY_HPP
#define OPENMVG_SFM_SFM_DATA_IO_PLY_HPP

#include "openMVG/sfm/sfm_data_io.hpp"

#include <fstream>
#include <iomanip>
#include <limits>
#include <string>

namespace openMVG {
namespace sfm {

/// Save the structure and camera positions of a SfM_Data container as 3D points in a PLY ASCII/BIN file.
inline bool Save_PLY
(
  const SfM_Data & sfm_data,
  const std::string & filename,
  ESfM_Data flags_part,
  bool b_write_in_ascii = false
)
{
  const bool b_structure = (flags_part & STRUCTURE) == STRUCTURE;
  const bool b_control_points = (flags_part & CONTROL_POINTS) == CONTROL_POINTS;
  const bool b_extrinsics = (flags_part & EXTRINSICS) == EXTRINSICS;

  if (!(b_structure || b_extrinsics || b_control_points))
    return false; // No 3D points to display, so it would produce an empty PLY file

  // Create the stream and check its status
  std::ofstream stream(filename.c_str(), std::ios::out | std::ios::binary);
  if (!stream)
    return false;

  bool bOk = false;
  {
    // Count how many views having valid poses:
    IndexT view_with_pose_count = 0;
    IndexT view_with_pose_prior_count = 0;
    if (b_extrinsics)
    {
      for (const auto & view : sfm_data.GetViews())
      {
        view_with_pose_count += sfm_data.IsPoseAndIntrinsicDefined(view.second.get());
      }

      for (const auto & view : sfm_data.GetViews())
      {
        if (const sfm::ViewPriors *prior = dynamic_cast<sfm::ViewPriors*>(view.second.get()))
        {
            view_with_pose_prior_count += prior->b_use_pose_center_;
        }
      }
    }

    stream << std::fixed << std::setprecision (std::numeric_limits<double>::digits10 + 1);

    using Vec3uc = Eigen::Matrix<unsigned char, 3, 1>;

    stream << "ply"
      << '\n' << "format "
              << (b_write_in_ascii ? "ascii 1.0" : "binary_little_endian 1.0")
      << '\n' << "comment generated by OpenMVG"
      << '\n' << "element vertex "
        // Vertex count: (#landmark + #GCP + #view_with_valid_pose)
        << (  (b_structure ? sfm_data.GetLandmarks().size() : 0)
            + (b_control_points ? sfm_data.GetControl_Points().size() : 0)
            + view_with_pose_count
            + view_with_pose_prior_count)
      << '\n' << "property double x"
      << '\n' << "property double y"
      << '\n' << "property double z"
      << '\n' << "property uchar red"
      << '\n' << "property uchar green"
      << '\n' << "property uchar blue"
      << '\n' << "end_header" << std::endl;

      if (b_extrinsics)
      {
        for (const auto & view : sfm_data.GetViews())
        {
          // Export pose as Green points
          if (sfm_data.IsPoseAndIntrinsicDefined(view.second.get()))
          {
            const geometry::Pose3 pose = sfm_data.GetPoseOrDie(view.second.get());
            if (b_write_in_ascii)
            {
              stream
                << pose.center()(0) << ' '
                << pose.center()(1) << ' '
                << pose.center()(2) << ' '
                << "0 255 0\n";
            }
            else
            {
              stream.write( reinterpret_cast<const char*> ( pose.center().data() ), sizeof( Vec3 ) );
              stream.write( reinterpret_cast<const char*> ( Vec3uc(0, 255, 0).data() ), sizeof( Vec3uc ) );
            }
          }

          // Export pose priors as Blue points
          if (const sfm::ViewPriors *prior = dynamic_cast<sfm::ViewPriors*>(view.second.get()))
          {
            if (prior->b_use_pose_center_)
            {
              if (b_write_in_ascii)
              {
                stream
                  << prior->pose_center_(0) << ' '
                  << prior->pose_center_(1) << ' '
                  << prior->pose_center_(2) << ' '
                  << "0 0 255\n";
              }
              else
              {
                stream.write( reinterpret_cast<const char*> ( prior->pose_center_.data() ), sizeof( Vec3 ) );
                stream.write( reinterpret_cast<const char*> ( Vec3uc(0, 0, 255).data() ), sizeof( Vec3uc ) );
              }
            }
          }
        }
      }

      if (b_structure)
      {
        // Export structure points as White points
        const Landmarks & landmarks = sfm_data.GetLandmarks();
        for ( const auto & iterLandmarks : landmarks )
        {
          if (b_write_in_ascii)
          {
            stream
              << iterLandmarks.second.X(0) << ' '
              << iterLandmarks.second.X(1) << ' '
              << iterLandmarks.second.X(2) << ' '
              << "255 255 255\n";
          }
          else
          {
            stream.write( reinterpret_cast<const char*> ( iterLandmarks.second.X.data() ), sizeof( Vec3 ) );
            stream.write( reinterpret_cast<const char*> ( Vec3uc(255, 255, 255).data() ), sizeof( Vec3uc ) );
          }
        }
      }

      if (b_control_points)
      {
        // Export GCP as Red points
        const Landmarks & landmarks = sfm_data.GetControl_Points();
        for ( const auto & iterGCP : landmarks )
        {
          if (b_write_in_ascii)
          {
            stream
              << iterGCP.second.X(0) << ' '
              << iterGCP.second.X(1) << ' '
              << iterGCP.second.X(2) << ' '
              << "255 0 0\n";
          }
          else
          {
            stream.write( reinterpret_cast<const char*> ( iterGCP.second.X.data() ), sizeof( Vec3 ) );
            stream.write( reinterpret_cast<const char*> ( Vec3uc(255, 0, 0).data() ), sizeof( Vec3uc ) );
          }
        }
      }

      stream.flush();
      bOk = stream.good();
      stream.close();
  }
  return bOk;
}

} // namespace sfm
} // namespace openMVG

#endif // OPENMVG_SFM_SFM_DATA_IO_PLY_HPP
