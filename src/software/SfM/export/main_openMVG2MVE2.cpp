// This file is part of OpenMVG, an Open Multiple View Geometry C++ library.

/* v.0.17 October 14th, 2015
 * Kevin CAIN, www.insightdigital.org
 * Adapted from the openMVG libraries,
 * Copyright (c) 2012-2015 Pierre MOULON.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "openMVG/cameras/Camera_Pinhole.hpp"
#include "openMVG/cameras/Camera_undistort_image.hpp"
#include "openMVG/features/feature.hpp"
#include "openMVG/image/image_io.hpp"
#include "openMVG/image/image_resampling.hpp"
#include "openMVG/sfm/sfm_data.hpp"
#include "openMVG/sfm/sfm_data_io.hpp"
#include "openMVG/system/loggerprogress.hpp"


using namespace openMVG;
using namespace openMVG::cameras;
using namespace openMVG::geometry;
using namespace openMVG::image;
using namespace openMVG::sfm;
using namespace openMVG::features;

#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <iomanip>
#include <atomic>

/// Naive image bilinear resampling of an image for thumbnail generation
template <typename ImageT>
ImageT
create_thumbnail
(
  const ImageT & image,
  int thumb_width,
  int thumb_height
);

/* Notes:
 * - An MVE2 scene appears to duplicate camera rot matrix and trans vector per-view data in 'meta.ini'
 *   within the first section of 'synth_0.out'.
 * - We do not save the original, instead we rely on the undistorted image from openMVG.
 * - We do not output EXIF blobs, as these appear only to be used for the GUI UMVE.
 * - To avoid encoding loss, openMVG images should be written as .PNG if undistorted images are *not* computed.
 * - In OpenMVG, some views may have some missing poses; MVE does *not* require a contiguous camera index.
 *
 *  For information on the target for this conversion, please see the MVE (v2) File format:
 *  https://github.com/simonfuhrmann/mve/wiki/MVE-File-Format
 */

bool exportToMVE2Format(
  const SfM_Data & sfm_data,
  const std::string & sOutDirectory // Output MVE2 files directory
  )
{
  std::atomic<bool> bOk(true);
  // Create basis directory structure
  if (!stlplus::is_folder(sOutDirectory))
  {
    OPENMVG_LOG_INFO << "[Creating directory: " << sOutDirectory << "]";
    stlplus::folder_create(sOutDirectory);
    bOk = stlplus::is_folder(sOutDirectory);
  }

  if (!bOk)
  {
    OPENMVG_LOG_ERROR << "Cannot access one of the desired output directories";
    return false;
  }

  // Export the SfM_Data scene to the MVE2 format
  {
    // Prepare to write bundle file
    // Get cameras and features from OpenMVG
    const Views & views = sfm_data.GetViews();
    const size_t cameraCount = views.size();
    // Tally global set of feature landmarks
    const Landmarks & landmarks = sfm_data.GetLandmarks();
    const size_t featureCount = landmarks.size();
    const std::string filename = "synth_0.out";
    OPENMVG_LOG_INFO << "Writing bundle (" << cameraCount << " cameras, "
        << featureCount << " features): to " << filename << "...\n";
    std::ofstream out(stlplus::folder_append_separator(sOutDirectory) + filename);
    out << "drews 1.0\n";  // MVE expects this header
    out << cameraCount << " " << featureCount << "\n";

    for (const auto & views_it : views)
    {
        const View * view = views_it.second.get();
        if (sfm_data.IsPoseAndIntrinsicDefined(view))
        {
            Intrinsics::const_iterator iterIntrinsic = sfm_data.GetIntrinsics().find(view->id_intrinsic);
            const IntrinsicBase * cam = iterIntrinsic->second.get();
            const Pose3 & pose = sfm_data.GetPoseOrDie(view);
            const Pinhole_Intrinsic * pinhole_cam = static_cast<const Pinhole_Intrinsic *>(cam);
            const Mat3 & rotation = pose.rotation();
            const Vec3 & translation = pose.translation();
            // Focal length and principal point must be normalized (0..1)
            const float flen = pinhole_cam->focal() / static_cast<double>(std::max(cam->w(), cam->h()));
            out
              << flen << " " << "0" << " " << "0" << "\n"  // Write '0' distortion values for pre-corrected images
              << rotation(0, 0) << " " << rotation(0, 1) << " " << rotation(0, 2) << "\n"
              << rotation(1, 0) << " " << rotation(1, 1) << " " << rotation(1, 2) << "\n"
              << rotation(2, 0) << " " << rotation(2, 1) << " " << rotation(2, 2) << "\n"
              << translation[0] << " " << translation[1] << " " << translation[2] << "\n";
        }
        else
        {
            // export a camera without pose & intrinsic info (export {0})
            // see: https://github.com/simonfuhrmann/mve/blob/952a80b0be48e820b8c72de1d3df06efc3953bd3/libs/mve/bundle_io.cc#L448
            for (int i = 0; i < 5 * 3; ++i)
              out << "0" << (i % 3 == 2 ? "\n" : " ");
        }
    }

    // For each feature, write to bundle: position XYZ[0-3], color RGB[0-2], all ref.view_id & ref.feature_id
    // The following method is adapted from Simon Fuhrmann's MVE project:
    // https://github.com/simonfuhrmann/mve/blob/e3db7bc60ce93fe51702ba77ef480e151f927c23/libs/mve/bundle_io.cc
    for (const auto & landmarks_it : landmarks)
    {
      const Vec3 & exportPoint = landmarks_it.second.X;
      out << exportPoint.x() << " " << exportPoint.y() << " " << exportPoint.z() << "\n";
      out << 250 << " " << 100 << " " << 150 << "\n";  // Write arbitrary RGB color, see above note

      // Write number of observations (features)
      const Observations & obs = landmarks_it.second.obs;
      out << obs.size();

      for (const auto & obs_it : obs)
      {
        const IndexT viewId = obs_it.first;
        const IndexT featId = obs_it.second.id_feat;
        out << " " << viewId << " " << featId << " 0";
      }
      out << "\n";
    }
    out.close();

    // Export (calibrated) views as undistorted images in parallel
    OPENMVG_LOG_INFO << "Exporting views...";

    // Create 'views' subdirectory
    const std::string sOutViewsDirectory = stlplus::folder_append_separator(sOutDirectory) + "views";
    if (!stlplus::folder_exists(sOutViewsDirectory))
    {
      OPENMVG_LOG_INFO << "[Creating directory:  " << sOutViewsDirectory << "]";
      stlplus::folder_create(sOutViewsDirectory);
    }

    system::LoggerProgress my_progress_bar(views.size());

    #pragma omp parallel for schedule(dynamic)
    for (int i = 0; i < static_cast<int>(views.size()); ++i)
    {
      if (!bOk) continue;
      auto views_it = views.begin();
      std::advance(views_it, i);
      const View * view = views_it->second.get();

      if (!sfm_data.IsPoseAndIntrinsicDefined(view))
          continue;

      // Create current view subdirectory 'view_xxxx.mve'
      std::ostringstream padding;
      padding << std::setw(4) << std::setfill('0') << view->id_view;
      std::string sOutViewIteratorDirectory;
      sOutViewIteratorDirectory = stlplus::folder_append_separator(sOutViewsDirectory) + "view_" + padding.str() + ".mve";

      // We have a valid view with a corresponding camera & pose
      const std::string srcImage = stlplus::create_filespec(sfm_data.s_root_path, view->s_Img_path);
      const std::string dstImage =
        stlplus::create_filespec(stlplus::folder_append_separator(sOutViewIteratorDirectory), "undistorted","png");

      if (!stlplus::folder_exists(sOutViewIteratorDirectory))
        stlplus::folder_create(sOutViewIteratorDirectory);

      Image<RGBColor> image, image_ud, thumbnail;
      // Undistort and save the image
      if (!ReadImage(srcImage.c_str(), &image))
      {
         std::cerr
           << "Unable to read the input image as a RGB image:\n"
           << srcImage << std::endl;
         bOk = false;
         continue;
      }
      Intrinsics::const_iterator iterIntrinsic = sfm_data.GetIntrinsics().find(view->id_intrinsic);
      const IntrinsicBase * cam = iterIntrinsic->second.get();
      if (cam->have_disto())
      {
        UndistortImage(image, cam, image_ud, BLACK);
        if (!WriteImage(dstImage.c_str(), image_ud))
        {
          OPENMVG_LOG_ERROR
            << "Unable to write the output image as a RGB image: " << dstImage;
          bOk = false;
          continue;
        }
      }
      else // (no distortion)
      {
        // If extensions match, copy the PNG image
        if (stlplus::extension_part(srcImage) == "PNG" ||
            stlplus::extension_part(srcImage) == "png")
        {
          stlplus::file_copy(srcImage, dstImage);
        }
        else
        {
          if (!ReadImage( srcImage.c_str(), &image) ||
              !WriteImage( dstImage.c_str(), image))
          {
            OPENMVG_LOG_ERROR << "Unable to read and write the image";
            bOk = false;
            continue;
          }
        }
      }

      // Prepare to write an MVE 'meta.ini' file for the current view
      const Pose3 & pose = sfm_data.GetPoseOrDie(view);
      const Pinhole_Intrinsic * pinhole_cam = static_cast<const Pinhole_Intrinsic *>(cam);
      const Mat3 & rotation = pose.rotation();
      const Vec3 & translation = pose.translation();
      // Pixel aspect: assuming square pixels
      const float pixelAspect = 1.f;
      // Focal length and principal point must be normalized (0..1)
      const float flen = pinhole_cam->focal() / static_cast<double>(std::max(cam->w(), cam->h()));
      const float ppX = std::abs(pinhole_cam->principal_point()(0)/cam->w());
      const float ppY = std::abs(pinhole_cam->principal_point()(1)/cam->h());

      // For each camera, write to bundle: focal length, radial distortion[0-1],
      // rotation matrix[0-8], translation vector[0-2]
      std::ostringstream fileOut;
      fileOut
        << "# MVE view meta data is stored in INI-file syntax." << fileOut.widen('\n')
        << "# This file is generated, formatting will get lost." << fileOut.widen('\n')
        << fileOut.widen('\n')
        << "[camera]" << fileOut.widen('\n')
        << "focal_length = " << flen << fileOut.widen('\n')
        << "pixel_aspect = " << pixelAspect << fileOut.widen('\n')
        << "principal_point = " << ppX << " " << ppY << fileOut.widen('\n')
        << "rotation = " << rotation(0, 0) << " " << rotation(0, 1) << " " << rotation(0, 2) << " "
        << rotation(1, 0) << " " << rotation(1, 1) << " " << rotation(1, 2) << " "
        << rotation(2, 0) << " " << rotation(2, 1) << " " << rotation(2, 2) << fileOut.widen('\n')
        << "translation = " << translation[0] << " " << translation[1] << " "
        << translation[2] << " " << fileOut.widen('\n')
        << fileOut.widen('\n')
        << "[view]" << fileOut.widen('\n')
        << "id = " << view->id_view << fileOut.widen('\n')
        << "name = " << stlplus::filename_part(srcImage.c_str()) << fileOut.widen('\n');

      // To do:  trim any extra separator(s) from openMVG name we receive, e.g.:
      // '/home/insight/openMVG_KevinCain/openMVG_Build/software/SfM/ImageDataset_SceauxCastle/images//100_7100.JPG'
      std::ofstream file(
        stlplus::create_filespec(stlplus::folder_append_separator(sOutViewIteratorDirectory),
        "meta","ini").c_str());
      file << fileOut.str();
      file.close();

      // Save a thumbnail image "thumbnail.png", 50x50 pixels
      thumbnail = create_thumbnail(image, 50, 50);
      const std::string dstThumbnailImage =
        stlplus::create_filespec(stlplus::folder_append_separator(sOutViewIteratorDirectory), "thumbnail","png");
      WriteImage(dstThumbnailImage.c_str(), thumbnail);

      ++my_progress_bar;
    }
  }
  return bOk;
}

int main(int argc, char *argv[])
{

  CmdLine cmd;
  std::string sSfM_Data_Filename;
  std::string sOutDir = "";
  cmd.add( make_option('i', sSfM_Data_Filename, "sfmdata") );
  cmd.add( make_option('o', sOutDir, "outdir") );
  OPENMVG_LOG_INFO << "Note:  this program writes output in MVE file format.";

  try {
    if (argc == 1) throw std::string("Invalid command line parameter.");
    cmd.process(argc, argv);
  } catch (const std::string& s) {
    OPENMVG_LOG_INFO << "Usage: " << argv[0] << '\n'
      << "[-i|--sfmdata] filename, the SfM_Data file to convert\n"
      << "[-o|--outdir] path";

    OPENMVG_LOG_ERROR << s;
    return EXIT_FAILURE;
  }

  // Create output dir
  if (!stlplus::folder_exists(sOutDir))
    stlplus::folder_create(sOutDir);

  // Read the input SfM scene
  SfM_Data sfm_data;
  if (!Load(sfm_data, sSfM_Data_Filename, ESfM_Data(ALL))) {
    OPENMVG_LOG_ERROR << "The input SfM_Data file \""<< sSfM_Data_Filename << "\" cannot be read.";
    return EXIT_FAILURE;
  }

  if (exportToMVE2Format(sfm_data, stlplus::folder_append_separator(sOutDir) + "MVE"))
    return EXIT_SUCCESS;
  else
    return EXIT_FAILURE;
}

/// Naive image bilinear resampling of an image for thumbnail generation
/// Inspired by create_thumbnail from MVE (cropping is here ignored)
template <typename ImageT>
ImageT
create_thumbnail
(
  const ImageT & image,
  int thumb_width,
  int thumb_height
)
{
  const int width = image.Width();
  const int height = image.Height();
  const float image_aspect = static_cast<float>(width) / height;
  const float thumb_aspect = static_cast<float>(thumb_width) / thumb_height;

  int rescale_width, rescale_height;
  if (image_aspect > thumb_aspect)
  {
    rescale_width = std::ceil(thumb_height * image_aspect);
    rescale_height = thumb_height;
  }
  else
  {
    rescale_width = thumb_width;
    rescale_height = std::ceil(thumb_width / image_aspect);
  }

  // Generation of the sampling grid
  std::vector<std::pair<float,float>> sampling_grid;
  sampling_grid.reserve(rescale_height * rescale_width);
  for ( int i = 0; i < rescale_height; ++i )
  {
    for ( int j = 0; j < rescale_width; ++j )
    {
      const float dx = static_cast<float>(j) * width / rescale_width;
      const float dy = static_cast<float>(i) * height / rescale_height;
      sampling_grid.push_back( std::make_pair( dy , dx ) );
    }
  }

  const Sampler2d<SamplerLinear> sampler;
  ImageT imageOut;
  GenericRessample(image, sampling_grid, rescale_width, rescale_height, sampler, imageOut);
  return imageOut;
}
