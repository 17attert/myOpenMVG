// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

// Copyright (c) 2012, 2013, 2014 Pierre MOULON.

// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef TOOLS_PRECISION_EVALUATION_TO_GT_HPP
#define TOOLS_PRECISION_EVALUATION_TO_GT_HPP

#include "openMVG/numeric/numeric.h"
#include "openMVG/geometry/rigid_transformation3D_srt.hpp"

#include "third_party/htmlDoc/htmlDoc.hpp"
#include "third_party/histogram/histogram.hpp"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"

#include <vector>

namespace openMVG
{
/// Compute a 7DOF rigid transform between the two camera trajectories
bool computeSimilarity(
  const std::vector<Vec3> & vec_camPosGT,
  const std::vector<Vec3> & vec_camPosComputed,
  std::vector<Vec3> & vec_camPosComputed_T,
  double *Sout, Mat3 * Rout, Vec3 * tout)
{
  if (vec_camPosGT.size() != vec_camPosComputed.size()) {
    OPENMVG_LOG_ERROR << "Cannot perform registration, vector sizes are different";
    return false;
  }

  // Move input points in appropriate container
  const Eigen::Map<const Mat3X> x1(vec_camPosComputed[0].data(), 3, vec_camPosComputed.size());
  const Eigen::Map<const Mat3X> x2(vec_camPosGT[0].data(), 3, vec_camPosGT.size());

  // Compute rigid transformation p'i = S R pi + t

  double S;
  Vec3 t;
  Mat3 R;
  openMVG::geometry::FindRTS(x1, x2, &S, &t, &R);
  OPENMVG_LOG_INFO << "Non linear refinement";
  openMVG::geometry::Refine_RTS(x1,x2,&S,&t,&R);

  vec_camPosComputed_T.resize(vec_camPosGT.size());
  for (size_t i = 0; i  < vec_camPosGT.size(); ++i)
  {
    const Vec3 newPos = S * R * ( vec_camPosComputed[i]) + t;
    vec_camPosComputed_T[i] = newPos;
  }

  *Sout = S;
  *Rout = R;
  *tout = t;
  return true;
}

/// Export to PLY two camera trajectories
static bool exportToPly(const std::vector<Vec3> & vec_camPosGT,
  const std::vector<Vec3> & vec_camPosComputed,
  const std::string & sFileName)
{
  std::ofstream outfile;
  outfile.open(sFileName.c_str(), std::ios_base::out);

  outfile << "ply"
    << '\n' << "format ascii 1.0"
    << '\n' << "element vertex " << vec_camPosGT.size()+vec_camPosComputed.size()
    << '\n' << "property float x"
    << '\n' << "property float y"
    << '\n' << "property float z"
    << '\n' << "property uchar red"
    << '\n' << "property uchar green"
    << '\n' << "property uchar blue"
    << '\n' << "end_header" << "\n";

  for (size_t i=0; i < vec_camPosGT.size(); ++i)  {
    outfile << vec_camPosGT[i].transpose()
      << " 0 255 0" << "\n";
  }

  for (size_t i=0; i < vec_camPosComputed.size(); ++i)  {
    outfile << vec_camPosComputed[i].transpose()
      << " 255 255 0" << "\n";
  }
  outfile.flush();
  const bool bOk = outfile.good();
  outfile.close();
  return bOk;
}

/// Compare two camera path (translation and rotation residual after a 7DOF rigid registration)
/// Export computed statistics to a HTLM stream
void EvaluateToGT(
  const std::vector<Vec3> & vec_camPosGT,
  const std::vector<Vec3> & vec_camPosComputed,
  const std::vector<Mat3> & vec_camRotGT,
  const std::vector<Mat3> & vec_camRotComputed,
  const std::string & sOutPath,
  htmlDocument::htmlDocumentStream * _htmlDocStream,
  std::vector<double>& vec_distance_residuals,
  std::vector<double>& vec_rotation_angular_residuals
  )
{
  // Compute global 3D similarity between the camera position
  std::vector<Vec3> vec_camPosComputed_T;
  Mat3 R;
  Vec3 t;
  double S;
  computeSimilarity(vec_camPosGT, vec_camPosComputed, vec_camPosComputed_T, &S, &R, &t);

  // Compute statistics and export them
  // -a. distance between camera center
  // -b. angle between rotation matrix

  // -a. distance between camera center
  vec_distance_residuals.clear();
  {
    for (size_t i = 0; i  < vec_camPosGT.size(); ++i) {
      const double dResidual = (vec_camPosGT[i] - vec_camPosComputed_T[i]).norm();
      vec_distance_residuals.push_back(dResidual);
    }
  }

  // -b. angle between rotation matrix
  vec_rotation_angular_residuals.clear();
  {
    std::vector<Mat3>::const_iterator iter1 = vec_camRotGT.begin();
    for (std::vector<Mat3>::const_iterator iter2 = vec_camRotComputed.begin();
      iter2 != vec_camRotComputed.end(); ++iter2, ++iter1) {
        const Mat3 R1 = *iter1; //GT
        const Mat3 R2T = *iter2 * R.transpose(); // Computed

        const double angularErrorDegree = R2D(getRotationMagnitude(R1 * R2T.transpose()));
        vec_rotation_angular_residuals.push_back(angularErrorDegree);
    }
  }

  // Display residual errors :
  std::ostringstream os;
  os << "Baseline residuals (in GT unit)\n";
  copy(vec_distance_residuals.cbegin(), vec_distance_residuals.cend(), std::ostream_iterator<double>(os, " , "));
  os << "\nAngular residuals (Degree) \n";
  copy(vec_rotation_angular_residuals.cbegin(), vec_rotation_angular_residuals.cend(), std::ostream_iterator<double>(os, " , "));

  os << "\nBaseline error statistics: \n";
  minMaxMeanMedian<double>(vec_distance_residuals.cbegin(), vec_distance_residuals.cend(), os);

  os << "\nAngular error statistics: \n";
  minMaxMeanMedian<double>(vec_rotation_angular_residuals.cbegin(), vec_rotation_angular_residuals.cend(), os);
  OPENMVG_LOG_INFO << os.str();


  double minB, maxB, meanB, medianB;
  minMaxMeanMedian<double>(vec_distance_residuals.cbegin(), vec_distance_residuals.cend(), minB, maxB, meanB, medianB);

  double minA, maxA, meanA, medianA;
  minMaxMeanMedian<double>(vec_rotation_angular_residuals.cbegin(), vec_rotation_angular_residuals.cend(), minA, maxA, meanA, medianA);


  // Export camera position (viewable)
  exportToPly(vec_camPosGT, vec_camPosComputed_T,
    stlplus::create_filespec(sOutPath, "camera_Registered", "ply"));

  exportToPly(vec_camPosGT, vec_camPosComputed,
    stlplus::create_filespec(sOutPath, "camera_original", "ply"));

  //-- Export residual to the HTML report
  {
    using namespace htmlDocument;
    _htmlDocStream->pushInfo("<hr>");
    _htmlDocStream->pushInfo(htmlMarkup("h1", "Compare GT camera position and looking direction."));
    _htmlDocStream->pushInfo(" Display per camera after a 3D similarity estimation:<br>");
    _htmlDocStream->pushInfo("<ul><li>Baseline_Residual -> localization error of camera center to GT (in GT unit),</li>");
    _htmlDocStream->pushInfo("<li>Angular_residuals -> direction error as an angular degree error.</li></ul>");

    std::ostringstream os;
    os << "Baseline_Residual=[";
    std::copy(vec_distance_residuals.cbegin(), vec_distance_residuals.cend(), std::ostream_iterator<double>(os, " "));
    os <<"];";
    _htmlDocStream->pushInfo("<hr>");
    _htmlDocStream->pushInfo( htmlDocument::htmlMarkup("pre", os.str()));

    os.str("");
    os << "mean = " << meanB;
    _htmlDocStream->pushInfo("<hr>");
    _htmlDocStream->pushInfo( htmlDocument::htmlMarkup("pre", os.str()));

    os.str("");
    os << "median = " << medianB;
    _htmlDocStream->pushInfo( htmlDocument::htmlMarkup("pre", os.str()));
    _htmlDocStream->pushInfo("<hr>");

    os.str("");
    os << "Angular_residuals=[";
    std::copy(vec_rotation_angular_residuals.begin(), vec_rotation_angular_residuals.end(), std::ostream_iterator<double>(os, " "));
    os <<"];";
    _htmlDocStream->pushInfo("<br>");
    _htmlDocStream->pushInfo( htmlDocument::htmlMarkup("pre", os.str()));

    os.str("");
    os << "mean = " << meanA;
    _htmlDocStream->pushInfo("<hr>");
    _htmlDocStream->pushInfo( htmlDocument::htmlMarkup("pre", os.str()));

    os.str("");
    os << "median = " << medianA;
    _htmlDocStream->pushInfo( htmlDocument::htmlMarkup("pre", os.str()));
    _htmlDocStream->pushInfo("<hr>");
  }
}

} //namespace openMVG

#endif // TOOLS_PRECISION_EVALUATION_TO_GT_HPP
