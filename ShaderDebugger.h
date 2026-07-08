#pragma once

#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include "dlfcn_win32_compat.h"
#else
#include <dlfcn.h>
#endif
#include "vecmath.h"

struct ShaderDebugger {
  ShaderDebugger(std::string source) {
    handle = dlopen(source.c_str(), RTLD_NOW);
    if (!handle) return;
    entryPointSym = dlsym(handle, "exec");
  }

  ~ShaderDebugger() {
    // TODO: keep track of textures and do cleanup?!
    dlclose(handle);
  }

  void setUniformi(std::string name, int value) {
    int *sym = (int *)dlsym(handle, name.c_str());
    if (!sym) {
      std::cerr << "no such uniform: " << name << '\n';
      return;
    }
    *sym = value;
  }

  void setTextureR32F(std::string name, const std::vector<float> &vec) {
    float ***sym = (float ***)dlsym(handle, name.c_str());
    if (!sym) {
      std::cerr << "no such sampler: " << name << '\n';
      return;
    }

    auto divUp = [](int a, int b) { return (a+b-1)/b; };
    int width = std::min((int)vec.size(),4096);
    int height = divUp(vec.size(),width);

    *sym = new float*[height];
    for (int i=0; i<height; ++i) {
      (*sym)[i] = new float[width];
    }
  }

  void setTextureRGBA32F(std::string name, const std::vector<float> &vec) {
    vecmath::vec4f ***sym = (vecmath::vec4f ***)dlsym(handle, name.c_str());
    if (!sym) {
      std::cerr << "no such sampler: " << name << '\n';
      return;
    }

    auto divUp = [](int a, int b) { return (a+b-1)/b; };

    int totalPixels = divUp(vec.size(),4);
    int width = std::min(totalPixels,4096);
    if (width == 0) width = 1;
    int height = divUp(vec.size(),width);

    std::vector<float> f32(width*size_t(height)*4);
    std::memcpy(f32.data(), vec.data(), sizeof(vec[0])*vec.size());

    *sym = new vecmath::vec4f*[height];
    for (int i=0; i<height; ++i) {
      (*sym)[i] = new vecmath::vec4f[width];
      std::memcpy((*sym)[i], f32.data()+i*width*4, sizeof(vecmath::vec4f)*width);
    }
  }

  void setTextureRGBA32UI(std::string name, const std::vector<unsigned> &vec) {
    vecmath::vec4ui ***sym = (vecmath::vec4ui ***)dlsym(handle, name.c_str());
    if (!sym) {
      std::cerr << "no such sampler: " << name << '\n';
      return;
    }

    auto divUp = [](int a, int b) { return (a+b-1)/b; };

    int totalPixels = divUp(vec.size(),4);
    int width = std::min(totalPixels,4096);
    if (width == 0) width = 1;
    int height = divUp(vec.size(),width);

    std::vector<unsigned> ui32(width*size_t(height)*4);
    std::memcpy(ui32.data(), vec.data(), sizeof(vec[0])*vec.size());

    *sym = new vecmath::vec4ui*[height];
    for (int i=0; i<height; ++i) {
      (*sym)[i] = new vecmath::vec4ui[width];
      std::memcpy((*sym)[i], ui32.data()+i*width*4, sizeof(vecmath::vec4ui)*width);
    }
  }

  void fragment(float u, float v) {
    void (*entryPoint)(float u, float v);
    entryPoint = (void (*)(float u, float v))entryPointSym;
    entryPoint(u,v);
  }

  bool good() { return handle && entryPointSym; }

  void *handle{nullptr};
  void *entryPointSym{nullptr};
};
