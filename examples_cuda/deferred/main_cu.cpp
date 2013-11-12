/*
  Copyright (c) 2011, Intel Corporation
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are
  met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.

    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived from
      this software without specific prior written permission.


   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
   IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
   TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
   PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
*/

#ifdef _MSC_VER
#define ISPC_IS_WINDOWS
#define NOMINMAX
#elif defined(__linux__)
#define ISPC_IS_LINUX
#elif defined(__APPLE__)
#define ISPC_IS_APPLE
#endif

#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <stdint.h>
#include <algorithm>
#include <assert.h>
#include <vector>
#ifdef ISPC_IS_WINDOWS
  #define WIN32_LEAN_AND_MEAN
  #include <windows.h>
#endif
#include "deferred.h"
#include "kernels_ispc.h"
#include "../timing.h"

#include <sys/time.h>
static inline double rtc(void)
{
  struct timeval Tvalue;
  double etime;
  struct timezone dummy;

  gettimeofday(&Tvalue,&dummy);
  etime =  (double) Tvalue.tv_sec +
    1.e-6*((double) Tvalue.tv_usec);
  return etime;
}
/******************************/ 
#include <cassert>
#include <iostream>
#include <cuda.h>
#include "drvapi_error_string.h"

#define checkCudaErrors(err)  __checkCudaErrors (err, __FILE__, __LINE__)
// These are the inline versions for all of the SDK helper functions
void __checkCudaErrors(CUresult err, const char *file, const int line) {
  if(CUDA_SUCCESS != err) {
    std::cerr << "checkCudeErrors() Driver API error = " << err << "\""
           << getCudaDrvErrorString(err) << "\" from file <" << file
           << ", line " << line << "\n";
    exit(-1);
  }
}

/**********************/
/* Basic CUDriver API */
CUcontext context;

void createContext(const int deviceId = 0)
{
  CUdevice device;
  int devCount;
  checkCudaErrors(cuInit(0));
  checkCudaErrors(cuDeviceGetCount(&devCount));
  assert(devCount > 0);
  checkCudaErrors(cuDeviceGet(&device, deviceId < devCount ? deviceId : 0));

  char name[128];
  checkCudaErrors(cuDeviceGetName(name, 128, device));
  std::cout << "Using CUDA Device [0]: " << name << "\n";

  int devMajor, devMinor;
  checkCudaErrors(cuDeviceComputeCapability(&devMajor, &devMinor, device));
  std::cout << "Device Compute Capability: " 
    << devMajor << "." << devMinor << "\n";
  if (devMajor < 2) {
    std::cerr << "ERROR: Device 0 is not SM 2.0 or greater\n";
    exit(1); 
  }

  // Create driver context
  checkCudaErrors(cuCtxCreate(&context, 0, device));
}
void destroyContext()
{
  checkCudaErrors(cuCtxDestroy(context));
}

CUmodule loadModule(const char * module)
{
  CUmodule cudaModule;
  // in this branch we use compilation with parameters

  const unsigned int jitNumOptions = 1;
  CUjit_option *jitOptions = new CUjit_option[jitNumOptions];
  void **jitOptVals = new void*[jitNumOptions];
  // set up pointer to set the Maximum # of registers for a particular kernel
  jitOptions[0] = CU_JIT_MAX_REGISTERS;
  int jitRegCount = 64;
  jitOptVals[0] = (void *)(size_t)jitRegCount;
#if 0

        // set up size of compilation log buffer
        jitOptions[0] = CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES;
        int jitLogBufferSize = 1024;
        jitOptVals[0] = (void *)(size_t)jitLogBufferSize;

        // set up pointer to the compilation log buffer
        jitOptions[1] = CU_JIT_INFO_LOG_BUFFER;
        char *jitLogBuffer = new char[jitLogBufferSize];
        jitOptVals[1] = jitLogBuffer;

        // set up pointer to set the Maximum # of registers for a particular kernel
        jitOptions[2] = CU_JIT_MAX_REGISTERS;
        int jitRegCount = 32;
        jitOptVals[2] = (void *)(size_t)jitRegCount;
#endif

  checkCudaErrors(cuModuleLoadDataEx(&cudaModule, module,jitNumOptions, jitOptions, (void **)jitOptVals));
  return cudaModule;
}
void unloadModule(CUmodule &cudaModule)
{
  checkCudaErrors(cuModuleUnload(cudaModule));
}

CUfunction getFunction(CUmodule &cudaModule, const char * function)
{
  CUfunction cudaFunction;
  checkCudaErrors(cuModuleGetFunction(&cudaFunction, cudaModule, function));
  return cudaFunction;
}
  
CUdeviceptr deviceMalloc(const size_t size)
{
  CUdeviceptr d_buf;
  checkCudaErrors(cuMemAlloc(&d_buf, size));
  return d_buf;
}
void deviceFree(CUdeviceptr d_buf)
{
  checkCudaErrors(cuMemFree(d_buf));
}
void memcpyD2H(void * h_buf, CUdeviceptr d_buf, const size_t size)
{
  checkCudaErrors(cuMemcpyDtoH(h_buf, d_buf, size));
}
void memcpyH2D(CUdeviceptr d_buf, void * h_buf, const size_t size)
{
  checkCudaErrors(cuMemcpyHtoD(d_buf, h_buf, size));
}
#define deviceLaunch(func,nbx,nby,nbz,params) \
  checkCudaErrors(cuFuncSetCacheConfig((func), CU_FUNC_CACHE_PREFER_SHARED)); \
  checkCudaErrors( \
      cuLaunchKernel( \
        (func), \
        ((nbx-1)/(128/32)+1), (nby), (nbz), \
        128, 1, 1, \
        0, NULL, (params), NULL \
        ));

typedef CUdeviceptr devicePtr;


/**************/
#include <vector>
std::vector<char> readBinary(const char * filename)
{
  std::vector<char> buffer;
  FILE *fp = fopen(filename, "rb");
  if (!fp )
  {
    fprintf(stderr, "file %s not found\n", filename);
    assert(0);
  }
#if 0
  char c;
  while ((c = fgetc(fp)) != EOF)
    buffer.push_back(c);
#else
  fseek(fp, 0, SEEK_END); 
  const unsigned long long size = ftell(fp);         /*calc the size needed*/
  fseek(fp, 0, SEEK_SET); 
  buffer.resize(size);

  if (fp == NULL){ /*ERROR detection if file == empty*/
    fprintf(stderr, "Error: There was an Error reading the file %s \n",filename);           
    exit(1);
  }
  else if (fread(&buffer[0], sizeof(char), size, fp) != size){ /* if count of read bytes != calculated size of .bin file -> ERROR*/
    fprintf(stderr, "Error: There was an Error reading the file %s \n", filename);
    exit(1);
  }
#endif
  fprintf(stderr, " read buffer of size= %d bytes \n", (int)buffer.size());
  return buffer;
}

extern "C" 
{

  void *CUDAAlloc(void **handlePtr, int64_t size, int32_t alignment)
  {
    return NULL;
  }
  void CUDALaunch(
      void **handlePtr, 
      const char * module_name,
      const char * module_1,
      const char * func_name,
      void **func_args, 
      int countx, int county, int countz)
  {
    assert(module_name != NULL);
    assert(module_1 != NULL);
    assert(func_name != NULL);
    assert(func_args != NULL);
#if 0
    const char * module = module_1;
#else
    const std::vector<char> module_str = readBinary("kernel.cubin");
    const char *  module = &module_str[0];
#endif
    CUmodule   cudaModule   = loadModule(module);
    CUfunction cudaFunction = getFunction(cudaModule, func_name);
    deviceLaunch(cudaFunction, countx, county, countz, func_args);
    unloadModule(cudaModule);
  }
  void CUDASync(void *handle)
  {
    checkCudaErrors(cuStreamSynchronize(0));
  }
  void ISPCSync(void *handle)
  {
    checkCudaErrors(cuStreamSynchronize(0));
  }
  void CUDAFree(void *handle)
  {
  }
}
/******************************/

///////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {
    if (argc != 2) {
        printf("usage: deferred_shading <input_file (e.g. data/pp1280x720.bin)>\n");
        return 1;
    }

    InputData *input = CreateInputDataFromFile(argv[1]);
    if (!input) {
        printf("Failed to load input file \"%s\"!\n", argv[1]);
        return 1;
    }

    Framebuffer framebuffer(input->header.framebufferWidth,
                            input->header.framebufferHeight);

//    InitDynamicC(input);
#if 0
#ifdef __cilk
    InitDynamicCilk(input);
#endif // __cilk
#endif
  
    /*******************/
  createContext();
  /*******************/

  devicePtr d_header = deviceMalloc(sizeof(ispc::InputHeader));
  devicePtr d_arrays = deviceMalloc(sizeof(ispc::InputDataArrays));
  const int buffsize = input->header.framebufferWidth*input->header.framebufferHeight;
  devicePtr d_r      = deviceMalloc(buffsize);
  devicePtr d_g      = deviceMalloc(buffsize);
  devicePtr d_b      = deviceMalloc(buffsize);

  for (int i = 0; i < buffsize; i++)
    framebuffer.r[i] = framebuffer.g[i] = framebuffer.b[i] = 0;

  
  ispc::InputDataArrays dh_arrays;
  {
    devicePtr d_chunk = deviceMalloc(input->header.inputDataChunkSize);
    memcpyH2D(d_chunk, input->chunk, input->header.inputDataChunkSize);

    dh_arrays.zBuffer = (float*)(d_chunk + input->header.inputDataArrayOffsets[idaZBuffer]);
    dh_arrays.normalEncoded_x =
        (uint16_t *)(d_chunk+input->header.inputDataArrayOffsets[idaNormalEncoded_x]);
    fprintf(stderr, "%p %p \n",
        dh_arrays.zBuffer, dh_arrays.normalEncoded_x);
    fprintf(stderr, " diff= %d  %d \n", 
        input->header.inputDataArrayOffsets[idaZBuffer],
        input->header.inputDataArrayOffsets[idaNormalEncoded_x]);

    dh_arrays.normalEncoded_y =
        (uint16_t *)(d_chunk+input->header.inputDataArrayOffsets[idaNormalEncoded_y]);
    dh_arrays.specularAmount =
        (uint16_t *)(d_chunk+input->header.inputDataArrayOffsets[idaSpecularAmount]);
    dh_arrays.specularPower =
        (uint16_t *)(d_chunk+input->header.inputDataArrayOffsets[idaSpecularPower]);
    dh_arrays.albedo_x =
        (uint8_t *)(d_chunk+input->header.inputDataArrayOffsets[idaAlbedo_x]);
    dh_arrays.albedo_y =
        (uint8_t *)(d_chunk+input->header.inputDataArrayOffsets[idaAlbedo_y]);
    dh_arrays.albedo_z =
        (uint8_t *)(d_chunk+input->header.inputDataArrayOffsets[idaAlbedo_z]);
    dh_arrays.lightPositionView_x =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightPositionView_x]);
    dh_arrays.lightPositionView_y =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightPositionView_y]);
    dh_arrays.lightPositionView_z =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightPositionView_z]);
    dh_arrays.lightAttenuationBegin =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightAttenuationBegin]);
    dh_arrays.lightColor_x =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightColor_x]);
    dh_arrays.lightColor_y =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightColor_y]);
    dh_arrays.lightColor_z =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightColor_z]);
    dh_arrays.lightAttenuationEnd =
        (float *)(d_chunk+input->header.inputDataArrayOffsets[idaLightAttenuationEnd]);
  }

  memcpyH2D(d_header, &input->header, sizeof(ispc::InputHeader));
  memcpyH2D(d_arrays, &dh_arrays, sizeof(ispc::InputDataArrays));
  memcpyH2D(d_r, framebuffer.r, buffsize);
  memcpyH2D(d_g, framebuffer.g, buffsize);
  memcpyH2D(d_b, framebuffer.b, buffsize);


    int nframes = 5;
    double ispcCycles = 1e30;
    for (int i = 0; i < 5; ++i) {
        framebuffer.clear();
        const double t0 = rtc();
        for (int j = 0; j < nframes; ++j)
            ispc::RenderStatic(
                (ispc::InputHeader*)d_header, 
                (ispc::InputDataArrays*)d_arrays,
                input->header,
                VISUALIZE_LIGHT_COUNT,
                (uint8_t*)d_r, 
                (uint8_t*)d_g, 
                (uint8_t*)d_b);
        double mcycles = 1000*(rtc() - t0) / nframes;
        fprintf(stderr, "dt= %g\n", mcycles);
        ispcCycles = std::min(ispcCycles, mcycles);
    }

    memcpyD2H(framebuffer.r, d_r, buffsize);
    memcpyD2H(framebuffer.g, d_g, buffsize);
    memcpyD2H(framebuffer.b, d_b, buffsize);

    printf("[ispc cuda]:\t\t[%.3f] million cycles to render "
           "%d x %d image\n", ispcCycles,
           input->header.framebufferWidth, input->header.framebufferHeight);
    WriteFrame("deferred-cuda.ppm", input, framebuffer);

  /*******************/
  destroyContext();
  /*******************/
    return 0;

#if 0

#ifdef __cilk
    double dynamicCilkCycles = 1e30;
    for (int i = 0; i < 5; ++i) {
        framebuffer.clear();
        reset_and_start_timer();
        for (int j = 0; j < nframes; ++j)
            DispatchDynamicCilk(input, &framebuffer);
        double mcycles = get_elapsed_mcycles() / nframes;
        dynamicCilkCycles = std::min(dynamicCilkCycles, mcycles);
    }
    printf("[ispc + Cilk dynamic]:\t\t[%.3f] million cycles to render image\n", 
           dynamicCilkCycles);
    WriteFrame("deferred-ispc-dynamic.ppm", input, framebuffer);
#endif // __cilk

    double serialCycles = 1e30;
    for (int i = 0; i < 5; ++i) {
        framebuffer.clear();
        reset_and_start_timer();
        for (int j = 0; j < nframes; ++j)
            DispatchDynamicC(input, &framebuffer);
        double mcycles = get_elapsed_mcycles() / nframes;
        serialCycles = std::min(serialCycles, mcycles);
    }
    printf("[C++ serial dynamic, 1 core]:\t[%.3f] million cycles to render image\n", 
           serialCycles);
    WriteFrame("deferred-serial-dynamic.ppm", input, framebuffer);

#ifdef __cilk
    printf("\t\t\t\t(%.2fx speedup from static ISPC, %.2fx from Cilk+ISPC)\n", 
           serialCycles/ispcCycles, serialCycles/dynamicCilkCycles);
#else
    printf("\t\t\t\t(%.2fx speedup from ISPC + tasks)\n", serialCycles/ispcCycles);
#endif // __cilk
#endif

    DeleteInputData(input);

    return 0;
}
