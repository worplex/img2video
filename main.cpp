extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
}
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <FreeImagePlus.h>
#include <stdio.h>
#include <list>
#include <string>
#include <sstream>


//NOTE: Compile ffmpeg with: ./configure --enable-libx264 --enable-gpl --enable-shared


int main(int argc, char** argv) {
  if(argc<3) {
    printf("Converts a directory of images to a single video file + metadata index\n\nUSAGE [inputDir] [outputDir]\n");
    return 0;
  }
  
  av_register_all();
  
  DIR* ectory = opendir(argv[1]);
  dirent* ry;
  int maxWidth = 0;
  int maxHeight = 0;
  
  printf("Performing initial scan...\n");
  
  std::list<std::string> files;
  
  
  while(ry = readdir(ectory)) {
    std::stringstream filename;
    filename<<argv[1]<<"/"<<ry->d_name;
    
    fipImage img;
    if(!img.load(filename.str().data())) {
      printf("Unable to load %s. Skipping.\n",filename.str().data());
      continue;
    }else {
      if(img.getWidth()>maxWidth) {
	maxWidth = img.getWidth();
      }
      if(img.getHeight()>maxHeight) {
	maxHeight = img.getHeight();
      }
      files.push_back(filename.str());
    }
    
  }
  
  printf("Initial scan complete. Video resolution set to %ix%i, %i images to convert to frames.\n",maxWidth,maxHeight,(int)files.size());
  
  AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
  AVCodecContext* encodeCtx = avcodec_alloc_context3(codec);
  encodeCtx->time_base.den = 60;
  encodeCtx->time_base.num = 1;
  encodeCtx->pix_fmt = AV_PIX_FMT_YUV444P;
  encodeCtx->width = maxWidth;
  encodeCtx->height = maxHeight;
  if(avcodec_open2(encodeCtx,codec,0)) {
    printf("Failed to initialize h.264 encoder\n");
    return -1;
  }
  
  AVFormatContext* muxer = 0;
  
  std::stringstream mpath;
  mpath<<argv[2]<<"/"<<"data.mp4";
  printf("Encoding to %s\n",mpath.str().data());
  avformat_alloc_output_context2(&muxer,0,"mp4",mpath.str().data());
  
  if(!muxer) {
    printf("Error opening muxer.\n");
    return -4;
  }
  avio_open2(&muxer->pb,mpath.str().data(),AVIO_FLAG_WRITE | AVIO_FLAG_READ,0,0);
  
  AVStream* stream = avformat_new_stream(muxer,encodeCtx->codec);
  stream->time_base = encodeCtx->time_base;
  AVProgram* program = av_new_program(muxer,1);
  av_program_add_stream_index(muxer,program->id,stream->index);
  if(!stream) {
    printf("Error opening video stream for writing\n");
    return -9;
  }
  AVCodecParameters* codecParams = avcodec_parameters_alloc();
  
  if(avcodec_parameters_from_context(codecParams,encodeCtx)<0) {
    printf("Failed to retrieve encoder parameters\n");
    return -11;
  }
  
 if(avcodec_parameters_copy(stream->codecpar,codecParams)<0) {
   printf("Failed to set encoder extradata on container\n");
   return -12;
 }
 avcodec_parameters_free(&codecParams);
  
 if(avformat_init_output(muxer,0)<0) {
   printf("Error initializing output\n");
   return -13;
 }
 if(avformat_write_header(muxer,0)<0) {
   printf("Error writing header\n");
   return -10;
 }
 
 
  int64_t pts = 0;
  
  auto mux_packet = [&](AVPacket* packet) {
    
    packet->stream_index = stream->index;
    av_interleaved_write_frame(muxer,packet);
    
  };
  
  
  AVPacket amazon;
    memset(&amazon,0,sizeof(amazon));
  for(auto i = files.begin();i!= files.end();i++) {
    std::string path = *i;
    printf("Encoding %s\n",path.data());
    fipImage img;
    img.load(path.data());
    if(!img.convertTo32Bits()) {
      printf("Error converting image sample to RGBA32\n");
      return -15;
    }
    
    AVFrame* frame = av_frame_alloc();
    frame->pts = pts;
    pts+=60*1000;
    frame->format = encodeCtx->pix_fmt;
    frame->width = encodeCtx->width;
    frame->height = encodeCtx->height;
    av_image_fill_linesizes(frame->linesize,(AVPixelFormat)frame->format,frame->width);
    
    
    
    SwsContext* scaler = sws_getContext(img.getWidth(),img.getHeight(),AV_PIX_FMT_RGBA,maxWidth,maxHeight,encodeCtx->pix_fmt,SWS_BICUBIC,0,0,0);
    unsigned char* pixels = img.accessPixels();
    
    int stride = img.getWidth()*4;
    
    if(av_frame_get_buffer(frame,0)) {
      printf("Failed to allocate buffer\n");
      return -2;
    }
    
    
    sws_scale(scaler,&pixels,&stride,0,0,frame->data,frame->linesize);
    sws_freeContext(scaler);
    
    int err = avcodec_send_frame(encodeCtx,frame);
    if(err) {
      printf("Encoder failure (code %i)\n",err);
      return -6;
    }
    av_frame_unref(frame);
    //Check if packet is available
    
    while(!(err = avcodec_receive_packet(encodeCtx,&amazon))) {
      mux_packet(&amazon);
    }
  }
  avcodec_send_frame(encodeCtx,0);
  int err;
  while(!(err = avcodec_receive_packet(encodeCtx,&amazon))) {
      mux_packet(&amazon);
    }
    
    
    
  av_interleaved_write_frame(muxer,0);
    
  
  av_write_trailer(muxer);
  avformat_free_context(muxer);
  
  
}






