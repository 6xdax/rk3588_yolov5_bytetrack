// Copyright (c) 2021 by Rockchip Electronics Co., Ltd. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define _BASETSD_H

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/videoio.hpp"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"
#include "postprocess.h"
#include "rga.h"
#include "rknn_api.h"
#include "omp.h"

// track
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <numeric>
#include <chrono>
#include <vector>
#include <dirent.h>
// #include "logging.h"
#include "BYTETracker.h"

/*-------------------------------------------
                  Functions
-------------------------------------------*/

static void dump_tensor_attr(rknn_tensor_attr* attr)
{
  printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
         "zp=%d, scale=%f\n",
         attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
         attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
         get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

static unsigned char* load_data(FILE* fp, size_t ofst, size_t sz)
{
  unsigned char* data;
  int            ret;

  data = NULL;

  if (NULL == fp) {
    return NULL;
  }

  ret = fseek(fp, ofst, SEEK_SET);
  if (ret != 0) {
    printf("blob seek failure.\n");
    return NULL;
  }

  data = (unsigned char*)malloc(sz);
  if (data == NULL) {
    printf("buffer malloc failure.\n");
    return NULL;
  }
  ret = fread(data, 1, sz, fp);
  return data;
}

static unsigned char* load_model(const char* filename, int* model_size)
{
  FILE*          fp;
  unsigned char* data;

  fp = fopen(filename, "rb");
  if (NULL == fp) {
    printf("Open file %s failed.\n", filename);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);

  data = load_data(fp, 0, size);

  fclose(fp);

  *model_size = size;
  return data;
}

static int saveFloat(const char* file_name, float* output, int element_size)
{
  FILE* fp;
  fp = fopen(file_name, "w");
  for (int i = 0; i < element_size; i++) {
    fprintf(fp, "%.6f\n", output[i]);
  }
  fclose(fp);
  return 0;
}

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main(int argc, char** argv)
{
  int            status     = 0;
  char*          model_name = NULL;
  rknn_context   ctx;
  size_t         actual_size        = 0;
  int            img_width          = 0;
  int            img_height         = 0;
  int            img_channel        = 0;
  const float    nms_threshold      = NMS_THRESH;
  const float    box_conf_threshold = BOX_THRESH;
  struct timeval start_time, stop_time;
  int            ret;

  // init rga context
  rga_buffer_t src;
  rga_buffer_t dst;
  im_rect      src_rect;
  im_rect      dst_rect;
  memset(&src_rect, 0, sizeof(src_rect));
  memset(&dst_rect, 0, sizeof(dst_rect));
  memset(&src, 0, sizeof(src));
  memset(&dst, 0, sizeof(dst));

  // init track
  const string input_video_path {argv[2]};
  
  // printf("post process config: box_conf_threshold = %.2f, nms_threshold = %.2f\n", box_conf_threshold, nms_threshold);
  
  // 加载模型
  model_name       = (char*)argv[1];
  /* Create the neural network */
  printf("Loading mode...\n");
  int            model_data_size = 0;
  unsigned char* model_data      = load_model(model_name, &model_data_size);
  // 初始化模型，第四个参数可以改模式
  ret                            = rknn_init(&ctx, model_data, model_data_size, 0, NULL);
  if (ret < 0) {
    printf("rknn_init error ret=%d\n", ret);
    return -1;
  }

  rknn_sdk_version version;
  ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
  if (ret < 0) {
    printf("rknn_init error ret=%d\n", ret);
    return -1;
  }
  printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

  rknn_input_output_num io_num;
  ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
  if (ret < 0) {
    printf("rknn_init error ret=%d\n", ret);
    return -1;
  }
  printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

  rknn_tensor_attr input_attrs[io_num.n_input];
  memset(input_attrs, 0, sizeof(input_attrs));
  for (int i = 0; i < io_num.n_input; i++) {
    input_attrs[i].index = i;
    ret                  = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
    if (ret < 0) {
      printf("rknn_init error ret=%d\n", ret);
      return -1;
    }
    dump_tensor_attr(&(input_attrs[i]));
  }

  rknn_tensor_attr output_attrs[io_num.n_output];
  memset(output_attrs, 0, sizeof(output_attrs));
  for (int i = 0; i < io_num.n_output; i++) {
    output_attrs[i].index = i;
    ret                   = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
    dump_tensor_attr(&(output_attrs[i]));
  }

  int channel = 3;
  int width   = 0;
  int height  = 0;
  if (input_attrs[0].fmt == RKNN_TENSOR_NCHW) {
    printf("model is NCHW input fmt\n");
    channel = input_attrs[0].dims[1];
    height  = input_attrs[0].dims[2];
    width   = input_attrs[0].dims[3];
  } else {
    printf("model is NHWC input fmt\n");
    height  = input_attrs[0].dims[1];
    width   = input_attrs[0].dims[2];
    channel = input_attrs[0].dims[3];
  }

  printf("model input height=%d, width=%d, channel=%d\n", height, width, channel);

  rknn_input inputs[1];
  memset(inputs, 0, sizeof(inputs));
  inputs[0].index        = 0;
  inputs[0].type         = RKNN_TENSOR_UINT8;
  inputs[0].size         = width * height * channel;
  inputs[0].fmt          = RKNN_TENSOR_NHWC;
  inputs[0].pass_through = 0;

  // 读取视频
	cv::VideoCapture capture;
	capture.open(input_video_path);
	if (!capture.isOpened()) {
		printf("could not read this video file...\n");
		return -1;
	}
	cv::Size S = cv::Size((int)capture.get(cv::CAP_PROP_FRAME_WIDTH),(int)capture.get(cv::CAP_PROP_FRAME_HEIGHT));
  img_width  = capture.get(cv::CAP_PROP_FRAME_WIDTH);
  img_height = capture.get(cv::CAP_PROP_FRAME_HEIGHT);
  printf("img width = %d, img height = %d\n", img_width, img_height);
	int fps = capture.get(cv::CAP_PROP_FPS);
	printf("current fps : %d \n", fps);

	cv::VideoWriter writer;
  writer.open("test.avi", cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), fps, S, true);
  if (!writer.isOpened())
    {
        printf("Error opening video writer\n");
        return -1;
    }
	cv::Mat orig_img;
  bool flag = true;

  // init tracker class
  BYTETracker tracker(fps, 30);
  long long num_frames = 0;
	float total_ms = 0;
  int speed_time = 0;
  double speed_temp = 0;
  double speed_max = 0;
	// int cap_grab_sum = 0;
	// int black_curtain_num = 200;  //黑屏超过200帧退出

	while (flag && capture.read(orig_img)) 
  {
    gettimeofday(&start_time, NULL);
    num_frames ++;
    // capture.read(orig_img);
    cv::Mat img;
    cv::cvtColor(orig_img, img, cv::COLOR_BGR2RGB);
    // You may not need resize when src resulotion equals to dst resulotion
    void* resize_buf = nullptr;

    if (img_width != width || img_height != height) {
      // printf("resize with RGA!\n");
      resize_buf = malloc(height * width * channel);
      memset(resize_buf, 0x00, height * width * channel);

      src = wrapbuffer_virtualaddr((void*)img.data, img_width, img_height, RK_FORMAT_RGB_888);
      dst = wrapbuffer_virtualaddr((void*)resize_buf, width, height, RK_FORMAT_RGB_888);
      ret = imcheck(src, dst, src_rect, dst_rect);
      if (IM_STATUS_NOERROR != ret) {
        printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
        return -1;
      }
      IM_STATUS STATUS = imresize(src, dst);

      // for debug
      // cv::Mat resize_img(cv::Size(width, height), CV_8UC3, resize_buf);
      // cv::imwrite("resize_input.jpg", resize_img);

      inputs[0].buf = resize_buf;
    } else {
      inputs[0].buf = (void*)img.data;
    }

    rknn_inputs_set(ctx, io_num.n_input, inputs);

    rknn_output outputs[io_num.n_output];
    memset(outputs, 0, sizeof(outputs));
    // omp_set_num_threads(8);
    // #pragma omp parallel for
    for (int i = 0; i < io_num.n_output; i++) {
      outputs[i].want_float = 0;
    }
    // 模型推理
    ret = rknn_run(ctx, NULL);
    ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);

    // post process
    float scale_w = (float)width / img_width;
    float scale_h = (float)height / img_height;

    detect_result_group_t detect_result_group;
    std::vector<float>    out_scales;
    std::vector<int32_t>  out_zps;
    for (int i = 0; i < io_num.n_output; ++i) {
      out_scales.push_back(output_attrs[i].scale);
      out_zps.push_back(output_attrs[i].zp);
    }
    post_process((int8_t*)outputs[0].buf, (int8_t*)outputs[1].buf, (int8_t*)outputs[2].buf, height, width,
                box_conf_threshold, nms_threshold, scale_w, scale_h, out_zps, out_scales, &detect_result_group);

    // Objects
    vector<Object> objects;
    objects.resize(detect_result_group.count);
    // omp_set_num_threads(8);
    // #pragma omp parallel for
    for (int i = 0; i < detect_result_group.count; i++) {
      detect_result_t* det_result = &(detect_result_group.results[i]);
      int x1 = det_result->box.left;
      int y1 = det_result->box.top;
      int x2 = det_result->box.right;
      int y2 = det_result->box.bottom;
      // objects[i] = proposals[picked[i]];
      objects[i].rect.x = x1;
      objects[i].rect.y = y1;
      objects[i].rect.width = x2 - x1;
      objects[i].rect.height = y2 - y1;
      objects[i].prob = det_result->prop;
      objects[i].name = det_result->name;

      if(strcmp(det_result->name, "resting") == 0)
      {
        objects[i].label = 0;
      }
      else
      {
        objects[i].label = 1;
      }
      // rectangle(orig_img, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 0, 0, 255), 3);
      // putText(orig_img, text, cv::Point(x1, y1 + 12), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0));
    }
    // cv::cvtColor(img, orig_img, cv::COLOR_BGR2RGB);
    // 追踪 xywh
		vector<STrack> output_stracks = tracker.update(objects, fps, num_frames);
    // omp_set_num_threads(8);
    // #pragma omp parallel for
    for (int i = 0; i < output_stracks.size(); i++)
		{
			vector<float> tlwh = output_stracks[i].tlwh;
      cv::Scalar s = tracker.get_color(output_stracks[i].track_id);
      putText(orig_img, output_stracks[i].name+" "+cv::format("%d %.2lf %.2lf", output_stracks[i].track_id, output_stracks[i].speed_1m,output_stracks[i].liveness), 
          cv::Point(tlwh[0], tlwh[1] - 5), 0, 0.6, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
      // 边框
      rectangle(orig_img, cv::Rect(tlwh[0], tlwh[1], tlwh[2], tlwh[3]), s, 2);
      // 中心点:(t+w/2,l+h/3)
      cv::Point p(int(tlwh[0]+tlwh[2]/2),int(tlwh[1]+2*tlwh[3]/3));
      circle(orig_img, p, 10, s, -1);
		}
    gettimeofday(&stop_time, NULL);
    
    total_ms = total_ms + ((__get_us(stop_time)-__get_us(start_time))/10000);
    putText(orig_img, cv::format("frame: %lld fps: %f num: %ld", num_frames, total_ms/num_frames, output_stracks.size()), 
                cv::Point(0, 30), 0, 0.6, cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
    writer.write(orig_img);

    if (num_frames % 20 == 0)
    {
      cout << "Processing frame " << num_frames << " (" << total_ms/num_frames << " fps)" << endl;
    }
		objects.clear();

    // imwrite("./out.jpg", orig_img);
    // cv::imwrite(name,orig_img);
    // writer.write(orig_img);
    // 释放yolov5内存
    ret = rknn_outputs_release(ctx, io_num.n_output, outputs);
    deinitPostProcess();
    if (resize_buf) {
      free(resize_buf);
    }
    // std::string name = std::to_string(frame) + "\n";
    // printf("%d\n",frame);
    if (num_frames == 100){
      flag = false;
    }
    if (num_frames > 65536)
    {
      num_frames = 0;
      total_ms = 0;
    }
  }
  gettimeofday(&stop_time, NULL);
  printf("once run use %f ms\n", total_ms);
  // release
  ret = rknn_destroy(ctx);

  if (model_data) {
    free(model_data);
  }
	capture.release();
	writer.release();

	return 0;
}

