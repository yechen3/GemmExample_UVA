/**
 * util.hpp: This file contains code that modifies a kernel into a multi-GPU kernel.
 *
 * Contact: Shao Chuanming <cyunming@sjtu.edu.cn>
 */

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.


#ifndef MY_UTIL_HPP
#define MY_UTIL_HPP

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <assert.h>
#include <sys/mman.h>

#include <cuda.h>
#include <cuda_runtime.h>
#include <iostream>

extern char **environ;


// constexpr size_t tot_physical_mem = 11554717696;
// constexpr int multiProcessorCount = USEABLE_SM_CNT;

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define BLOCKDIM(dim) (dim.x * dim.y * dim.z)


#define CUDA_RUNTIME(err) do {                                          \
	if ((err) != cudaSuccess) {                                     \
            fprintf(stderr, "%s:%d %s(%s)\n", __FILE__, __LINE__, cudaGetErrorName(err), cudaGetErrorString(err)); \
	}                                                               \
    } while (0)


/* split kernel definations */
/* Documentation of the kernel split :

Add in header:
constexpr size_t __n = 4; // number of GPUs to use

To transform a kernel 

```
__global__ void setk_M(int *a, int k) {
    size_t tid = blockIdx.x * blockDim.x + threadIdx.x;
    a[tid] = k;
}
```

into:

```
KERNEL(setk_M, int *a, int k) {
    SPLIT_KERNEL_GUARD {
        size_t tid = BLOCKIDX.x * blockDim.x + threadIdx.x;
        a[tid] = k;
    }
}
```

that is, change `blockIdx' into `BLOCKIDX'

calling 

```
setk_M <<< num_block, threads_per_block>>>(a, k);
```

transform into:

```
split_kernel(setk_M, dim3(num_block), dim3(threads_per_block), a, k);
```

*/


__device__ inline size_t __current_block(size_t id, dim3 gridDim, dim3 blockIdx) {
    return (id * gridDim.x * gridDim.y + gridDim.x * blockIdx.y + blockIdx.x);
}

__device__ inline size_t blockIdx_y(size_t id, dim3 gridDim, dim3 blockIdx, dim3 original_dimblock) {
    return  __current_block(id, gridDim, blockIdx) / original_dimblock.x;
}

__device__ inline size_t blockIdx_x(size_t id, dim3 gridDim, dim3 blockIdx, dim3 original_dimblock) {
    return __current_block(id, gridDim, blockIdx) % original_dimblock.x;
}

__device__ inline size_t blockIdx_z(size_t id, dim3 gridDim, dim3 blockIdx, dim3 original_dimblock) {
    return 0;
}


__device__ inline dim3 blockIdx_(size_t id, dim3 gridDim, dim3 blockIdx, dim3 original_dimblock) {
    return dim3(blockIdx_x(id, gridDim, blockIdx, original_dimblock), 
                blockIdx_y(id, gridDim, blockIdx, original_dimblock), 
                blockIdx_z(id, gridDim, blockIdx, original_dimblock));
}


#define BLOCKIDX (blockIdx_(__id, gridDim, blockIdx, __dim_block))

#define SPLIT_KERNEL_GUARD if (BLOCKIDX.x < __dim_block.x && BLOCKIDX.y < __dim_block.y && BLOCKIDX.z < __dim_block.z)

inline size_t THREADS_PER_BLOCK(dim3 __dim_thread) {
    return (size_t(__dim_thread.x) * size_t(__dim_thread.y) * size_t(__dim_thread.z));
}

inline size_t BLOCKS_PER_KERNEL(dim3 __dim_block) {
    return (size_t(__dim_block.x) * size_t(__dim_block.y) * size_t(__dim_block.z));
}

inline size_t THREADS_PER_KERNEL(dim3 __dim_block, dim3 __dim_thread) {
    return  (BLOCKS_PER_KERNEL(__dim_block) * THREADS_PER_BLOCK(__dim_thread));
}

#define split_kernel(__kernel, __dim_block, __dim_thread, ...) do {\
    dim3 __fake_dim_block((BLOCKS_PER_KERNEL(__dim_block)) / __n + ((BLOCKS_PER_KERNEL(__dim_block) % __n == 0) ? 0 : 1)); \
    printf("orig Blocks : {%d, %d, %d} threads %d gpus.\n", __dim_block.x, __dim_block.y, __dim_block.z, __n);\
    std::cout<<size_t(__dim_block.y)<<"\n";\
    int __original_device;\
    cudaGetDevice(&__original_device);\
    for (int __i = 0; __i < __n; __i++) {\
        cudaSetDevice(__i);\
        printf("Blocks : {%d, %d, %d} threads.\n", __fake_dim_block.x, __fake_dim_block.y, __fake_dim_block.z);\
        __kernel <<< __fake_dim_block, __dim_thread >>> (__VA_ARGS__ , __i, __dim_block) ; \
    }\
    cudaDeviceSynchronize();\
    cudaSetDevice(__original_device);\
    } while (0)

#define KERNEL(__kernel, ...) __global__ void __kernel(__VA_ARGS__, int __id, dim3 __dim_block)

/* split kernel */


// helper functions




cudaError_t MYcudaMallocManaged(void **devPtr, size_t size, int cnt_GPU=1)
{
    CUDA_RUNTIME(cudaMalloc(devPtr, size));
    
    return cudaSuccess;
}

cudaError_t MycudaMemcpy(void *devPtr, void *hostPtr, size_t size) 
{
    cudaSetDevice(0);
    CUDA_RUNTIME(cudaMemcpy(devPtr, hostPtr, size, cudaMemcpyDefault));
    printf("Memcpy form %p to %p\n", hostPtr, devPtr);
    return cudaSuccess;
}

cudaError_t EnablePeerAccess(int n)
{
    for(int i=0; i<n; i++) {
        for(int j=0; j<n; j++) {
            if (i==j) continue;
            cudaSetDevice(i);
            cudaDeviceEnablePeerAccess(j, 0);
        }
    }
    return cudaSuccess;
}

#endif

