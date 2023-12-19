# 水禽多目标跟踪及行为识别系统设计与实现

## 暂时存储，可能有很多bug，后续会整理完善优化更新

## 相关资料参考

算法：

[GitHub - ultralytics/yolov5: YOLOv5 🚀 in PyTorch > ONNX > CoreML > TFLite](https://github.com/ultralytics/yolov5)

[GitHub - ifzhang/ByteTrack: ECCV 2022\] ByteTrack: Multi-Object Tracking by Associating Every Detection Box](https://github.com/ifzhang/ByteTrack)

部署

[(【RK3588-linux开发】3、训练yolov5并在RK3588上运行_stupid_miao的博客-CSDN博客](https://blog.csdn.net/qq_32768679/article/details/124674803)

[rockchip-linux/rknn-toolkit (github.com)](https://github.com/rockchip-linux/rknn-toolkit)

## 需具备知识

Linux开发板的使用：**说明文档\用户手册和原理图\OrangePi_5_RK3588S_用户手册_v1.2.pdf** 

Rockchip的使用：**说明文档\NPU使用文档\Rockchip_User_Guide_RKNN_Toolkit2_CN-1.4.0.pdf**

深度学习训练：**yolov5**

模型部署：pt模型转onnx模型，onnx模型转rknn模型【Rockchip】

## 运行环境

Orange Pi 5（rk3588） 拥有6TOPS算力的NPU

## 登录开发板

**说明文档\用户手册和原理图\OrangePi_5_RK3588S_用户手册_v1.2.pdf** 中有详细的介绍，**用自己熟悉的方法登录即可**。

个人使用的是3.7章SSH远程登录，下面进行介绍。

WiFi连接网络，网线连接笔记本跟开发板。在网络连接中，将WLAN设置为共享，将网络共享给以太网口。这样子笔记本跟开发板就处于同一局域网，并且使用笔记本的网络进行联网。

![image-20230525111421603](%E6%B0%B4%E7%A6%BD%E5%A4%9A%E7%9B%AE%E6%A0%87%E8%B7%9F%E8%B8%AA%E5%8F%8A%E8%A1%8C%E4%B8%BA%E8%AF%86%E5%88%AB%E7%B3%BB%E7%BB%9F%E8%AE%BE%E8%AE%A1%E4%B8%8E%E5%AE%9E%E7%8E%B0.assets/image-20230525111421603.png)

使用Advanced IP Scanner搜索局域网中的ip，找到orangepi5，使用xshell进行SSH连接。

![image-20230525111713212](%E6%B0%B4%E7%A6%BD%E5%A4%9A%E7%9B%AE%E6%A0%87%E8%B7%9F%E8%B8%AA%E5%8F%8A%E8%A1%8C%E4%B8%BA%E8%AF%86%E5%88%AB%E7%B3%BB%E7%BB%9F%E8%AE%BE%E8%AE%A1%E4%B8%8E%E5%AE%9E%E7%8E%B0.assets/image-20230525111713212.png)

登录效果如下图所示。

![image-20230525111922243](%E6%B0%B4%E7%A6%BD%E5%A4%9A%E7%9B%AE%E6%A0%87%E8%B7%9F%E8%B8%AA%E5%8F%8A%E8%A1%8C%E4%B8%BA%E8%AF%86%E5%88%AB%E7%B3%BB%E7%BB%9F%E8%AE%BE%E8%AE%A1%E4%B8%8E%E5%AE%9E%E7%8E%B0.assets/image-20230525111922243.png)

## 开发环境搭建

**已有的开发板已经搭建完成，可以不重复操作。**

```
sudo apt-get install build-essential
sudo apt install cmake
sudo apt-get install libopencv-dev
sudo apt install libeigen3-dev
```

## 项目路径

/home/orangepi/rknpu2-master/examples/rknn_yolov5_e

路径下相关内容介绍：

├── build												编译的中间文件
├── build-linux_RK3588.sh				 编译脚本					
├── CMakeLists.txt							  CMake配置文件
├── convert_rknn_demo					ONNX模型转rknn模型
├── include										   头文件
├── install											 编译完成后的安装路径
├── model											模型路径
├── README.md								 说明文档
└── src												  主要代码
    ├── BYTETracker.cpp					 bytetrack算法实现，速度计算以及活跃度也在其中
    ├── kalmanFilter.cpp					
    ├── lapjv.cpp
    ├── main.cc									 全部算法实现
    ├── postprocess.cc
    ├── STrack.cpp
    └── utils.cpp

## 程序运行

编译：./build-linux_RK3588.sh

./rknn_yolov5_e [模型路径] [rtmp流/视频路径]

例：./rknn_yolov5_e /home/orangepi/rknpu2-master/examples/rknn_yolov5_e/model/posture100.rknn rtmp://rtmp01open.ys7.com/openlive/a7213e58da1547a4ac941de460a8771d**[.hd  加上.hd即读取高清视频流]**









