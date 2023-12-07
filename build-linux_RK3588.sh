# 脚本只要发生错误，就终止执行
set -e

TARGET_SOC="rk3588"
GCC_COMPILER=aarch64-linux-gnu
# 配置环境
export LD_LIBRARY_PATH=${TOOL_CHAIN}/lib64:$LD_LIBRARY_PATH
export CC=${GCC_COMPILER}-gcc
export CXX=${GCC_COMPILER}-g++

# $0 脚本本身名字 dirname 文件所在文件夹名字 
# 取脚本目录
ROOT_PWD=$( cd "$( dirname $0 )" && cd -P "$( dirname "$SOURCE" )" && pwd )

# build
BUILD_DIR=${ROOT_PWD}/build/build_linux_aarch64

if [[ ! -d "${BUILD_DIR}" ]]; then
  mkdir -p ${BUILD_DIR}
fi

cd ${BUILD_DIR}
cmake ../.. -DCMAKE_SYSTEM_NAME=Linux -DTARGET_SOC=${TARGET_SOC} -DCMAKE_BUILD_TYPE=debug
make -j4
make install
cd -
