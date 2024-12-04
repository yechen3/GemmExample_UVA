#!/bin/bash

# problem size
NI="512"

[ ! -e "bin" ] && mkdir "bin"
[ ! -e "ncu" ] && mkdir "ncu"

NCU_path=/home/tgrogers-raid/a/common/cuda-12.4/bin



NI_SIZES=(128 256 512)

for NI in "${NI_SIZES[@]}"; do
    # Compile for 1 to 4 GPUs
    for n in {1..4}; do
        GEMM_SRC=$(mktemp -u 'gemm_tmp_XXXXXXXXX.cu')
        cp gemm_multi_gpu.cu "${GEMM_SRC}"

        # Replace placeholders in the temporary source file
        sed -i -e "s/@n@/$n/" $GEMM_SRC
        sed -i -e "s/@NI@/$NI/" $GEMM_SRC
        sed -i -e "s/@NJ@/$NI/" $GEMM_SRC
        sed -i -e "s/@NK@/$NI/" $GEMM_SRC

        # Compile the file with nvcc and output to the "bin" directory
        nvcc -O3 -w $GEMM_SRC -o "bin/gemm-${n}-${NI}"
        echo "generated bin/gemm-${n}-${NI}"

        # Remove the temporary source file
        rm $GEMM_SRC

        echo "running ncu on the binary bin/gemm-${n}-${NI}"
        $NCU_path/ncu -o ncu/gemm_${n}_${NI} -f bin/gemm-${n}-${NI}  


    done
done
