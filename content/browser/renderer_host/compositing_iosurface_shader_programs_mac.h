// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_RENDERER_HOST_COMPOSITING_IOSURFACE_SHADER_PROGRAMS_MAC_H_
#define CONTENT_BROWSER_RENDERER_HOST_COMPOSITING_IOSURFACE_SHADER_PROGRAMS_MAC_H_

#include <OpenGL/gl.h>

#include "base/basictypes.h"
#include "base/gtest_prod_util.h"

namespace content {

// Provides caching of the compile-and-link step for shader programs at runtime
// since, once compiled and linked, the programs can be shared.  Callers invoke
// one of the UseXXX() methods to glUseProgram() the program and have its
// uniform variables bound with the given parameters.
//
// Note: All public methods must be invoked within the the same GL context!
class CompositingIOSurfaceShaderPrograms {
 public:
  CompositingIOSurfaceShaderPrograms();
  ~CompositingIOSurfaceShaderPrograms();

  // Reset the cache, deleting any references to currently-cached shader
  // programs.  This must be called within an active OpenGL context just before
  // destruction.
  void Reset();

  // Begin using the "blit" program, which is set up to sample the texture at
  // GL_TEXTURE_0.  Returns false on error.
  bool UseBlitProgram();

  // Begin using the program that just draws solid white very efficiently.
  // Returns false on error.
  bool UseSolidWhiteProgram();

  // Begin using one of the two RGB-to-YV12 color conversion programs, as
  // specified by |pass_number| 1 or 2.  The programs will sample the texture at
  // GL_TEXTURE0, and account for scaling in the X direction by |texel_scale_x|.
  // Returns false on error.
  bool UseRGBToYV12Program(int pass_number, float texel_scale_x);

  // |format| argument to use for glReadPixels() when reading back textures
  // generated by the RGBToYV12 program.
  GLenum rgb_to_yv12_output_format() const {
    return rgb_to_yv12_output_format_;
  }

  static void SetBackgroundColor(float r, float g, float b);

 protected:
  FRIEND_TEST_ALL_PREFIXES(CompositingIOSurfaceTransformerTest,
                           TransformsRGBToYV12);

  // Side effect: Calls Reset(), deleting any cached programs.
  void SetOutputFormatForTesting(GLenum format);

 private:
  enum { kNumShaderPrograms = 4 };

  // Helper methods to cache uniform variable locations.
  GLuint GetShaderProgram(int which);
  void BindUniformTextureVariable(int which, int texture_unit_offset);
  void BindUniformTexelScaleXVariable(int which, float texel_scale_x);

  // Cached values for previously-compiled/linked shader programs, and the
  // locations of their uniform variables.
  GLuint shader_programs_[kNumShaderPrograms];
  GLint texture_var_locations_[kNumShaderPrograms];
  GLint texel_scale_x_var_locations_[kNumShaderPrograms];

  // Byte order of the quads generated by the RGBToYV12 shader program.  Must
  // always be GL_BGRA (default) or GL_RGBA (workaround case).
  GLenum rgb_to_yv12_output_format_;

  DISALLOW_COPY_AND_ASSIGN(CompositingIOSurfaceShaderPrograms);
};

}  // namespace content

#endif  // CONTENT_BROWSER_RENDERER_HOST_COMPOSITING_IOSURFACE_SHADER_PROGRAMS_MAC_H_
