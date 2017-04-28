/******************************************/ /*
  duplex.cpp
  by Gary P. Scavone, 2006-2007.

  This program opens a duplex stream and passes
  input directly through to the output.
*/
/******************************************/

#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <fstream>

/*
typedef char MY_TYPE;
#define FORMAT RTAUDIO_SINT8
*/

//typedef signed double MY_TYPE;
//#define FORMAT RTAUDIO_SINT16

typedef double MY_TYPE;
#define FORMAT RTAUDIO_FLOAT64

#define CHANNELS 2
#define SAMPLE_RATE 44100
#define BUFF_SIZE 256

#define BUFFER BUFF_SIZE*CHANNELS

#define HRIR_SIZE 200

#define OUT_SIZE BUFF_SIZE + HRIR_SIZE -1

//input buffer

MY_TYPE l_vals[BUFF_SIZE];
MY_TYPE r_vals[BUFF_SIZE];

//input buffer converted to mono

MY_TYPE mono[BUFF_SIZE];

//hrir read-in

MY_TYPE hrir_l[BUFF_SIZE];
MY_TYPE hrir_r[BUFF_SIZE];

//output buffer

MY_TYPE out_l[BUFF_SIZE];
MY_TYPE out_r[BUFF_SIZE];

//parsed csv

MY_TYPE hrir_file_l[25][50][200];
MY_TYPE hrir_file_r[25][50][200];

//overlap buffers

MY_TYPE overlap_l[BUFF_SIZE];
MY_TYPE overlap_r[BUFF_SIZE];
MY_TYPE overlap_prev_l[BUFF_SIZE];
MY_TYPE overlap_prev_r[BUFF_SIZE];


/*
typedef S24 MY_TYPE;
#define FORMAT RTAUDIO_SINT24

typedef signed long MY_TYPE;
#define FORMAT RTAUDIO_SINT32

typedef float MY_TYPE;
#define FORMAT RTAUDIO_FLOAT32
*/

int foo = 0;
int bar = 0;
int fubar = 0;

bool get_hrir(const char *path, double arr[25][50][200]){

	int i, j, k;
	i=j=k=0;

	std::string line;

	std::ifstream fin(path);

	if(!fin.is_open()){
		std::cout << path << ": ";
		std::cout << "Error: no such file! \n";
		return false;
	}

	int f = 0;

	while(std::getline(fin, line)){
		std::stringstream ss(line);
        	while(getline(ss, line, ',')){

			f++;

        		arr[i][j][k] = strtod(line.c_str(), NULL);
			j++;
			if(j==50){
				j=0;
				k++;
			}
			if(k==200){
				k=0;
				i++;
			}
		}
   	}
	std::cout<<f<<"\n";
	return true;
}

void convolve(){

	for (int n = 0; n < OUT_SIZE; n++){
		//fix types

		MY_TYPE L_s, R_s;
		L_s = R_s = 0;

		size_t kmin, kmax, k;

		kmin = (n >= BUFF_SIZE - 1) ? n - (BUFF_SIZE - 1) : 0;
		kmax = (n < HRIR_SIZE - 1) ? n : HRIR_SIZE - 1;

		for (k = kmin; k <= kmax; k++){
			L_s += mono[n - k] * hrir_l[k];
			R_s += mono[n - k] * hrir_r[k];

		}
		if (n>=BUFF_SIZE){
			overlap_l[n-BUFF_SIZE] = L_s*0.5;
			overlap_r[n-BUFF_SIZE] = R_s*0.5;
		}
		else{
			out_l[n] = L_s*0.5;
			out_r[n] = R_s*0.5;
		}
	}
}

void overlap(){
	for (int i = 0; i < OUT_SIZE - BUFF_SIZE; i++){
		out_l[i] += overlap_prev_l[i];
		overlap_prev_l[i] = overlap_l[i];
		out_r[i] += overlap_prev_r[i];
		overlap_prev_r[i] = overlap_r[i];
	}
}


void hrir_update(int az, int el){

	int padding = (BUFF_SIZE - HRIR_SIZE) / 2;

	for (int i = 0; i < HRIR_SIZE; i++){

			//parse csv

			hrir_l[i+padding] = hrir_file_l[az][el][i];
//			hrir_l[i+padding] = 1;
			//std::cout << hrir_l[i+padding] << ", ";
			hrir_r[i+padding] = hrir_file_r[az][el][i];
//			hrir_r[i+padding] = 1;
			//std::cout << hrir_r[i+padding] << ", ";
	}
}

int inout( void *outputBuffer, void *inputBuffer, unsigned int /*nBufferFrames*/,
           double /*streamTime*/, RtAudioStreamStatus status, void *data )
{
  // Since the number of input and output channels is equal, we can do
  // a simple buffer copy operation here.

	unsigned int i;
	MY_TYPE *ibuff = (MY_TYPE *) inputBuffer;
	MY_TYPE *obuff = (MY_TYPE *) outputBuffer;

  if ( status ) std::cout << "Stream over/underflow detected." << std::endl;

	for (i=0; i<BUFFER; i++){
		if(i%2 == 0){
			l_vals[i/2] = *ibuff++;
			mono[i/2]=l_vals[i/2]/2;
		}
		else{
			r_vals[i/2] = *ibuff++;
			mono[i/2]+=r_vals[i/2]/2;
		}
	}

	fubar++;

	if(fubar == 250){
		fubar=0;

		foo++;
	}

	if(foo == 24 || foo == 0){
		foo=0;
		bar = 1-bar;
	}

	hrir_update(foo,0);
//	hrir_update(24,0);
	convolve();
	overlap();

	for (i=0; i<BUFFER; i++){

		MY_TYPE val = 0;

		if(i%2 == 0){
		//write left channel / interleaved audio / index/2

			//val = l_vals[i/2];
			val = out_l[i/2];
		}
		else{
		//write right channel

			//val = r_vals[i/2];
			val = out_r[i/2];
		}

		*obuff++ = val;
	}
  return 0;
}

int main( int argc, char *argv[] )
{
  unsigned int channels, fs, bufferBytes, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;

  const char *hrir_l_path = "hrir_l.csv";
  const char *hrir_r_path = "hrir_r.csv";

  get_hrir(hrir_l_path, hrir_file_l);
  get_hrir(hrir_r_path, hrir_file_r);

  hrir_update(0,0);

  RtAudio adac;

  int ndevs = adac.getDeviceCount();

  RtAudio::DeviceInfo info;

  if ( ndevs < 1 ) {
    std::cout << "\nNo audio devices found!\n";
    exit( 1 );
  }else{
  	for(int i=0; i<ndevs; i++){
		info = adac.getDeviceInfo(i);
		std::cout << info.name << "   " << info.inputChannels << "   " << info.outputChannels << "   "  << info.duplexChannels << "\n";
	}
  }

  channels = CHANNELS;
  fs = SAMPLE_RATE;
  
  // Let RtAudio print messages to stderr.
  adac.showWarnings( true );

  // Set the same number of channels for both input and output.
  unsigned int bufferFrames = BUFF_SIZE;
  
  RtAudio::StreamParameters iParams, oParams;
  
  iParams.deviceId = iDevice;
  iParams.nChannels = channels;
  iParams.firstChannel = iOffset;
  
  oParams.deviceId = oDevice;
  oParams.nChannels = channels;
  oParams.firstChannel = oOffset;

  if ( iDevice == 0 )
    iParams.deviceId = adac.getDefaultInputDevice();
  if ( oDevice == 0 )
    oParams.deviceId = adac.getDefaultOutputDevice();

  RtAudio::StreamOptions options;
  //options.flags |= RTAUDIO_NONINTERLEAVED;

  bufferBytes = bufferFrames * channels * sizeof( MY_TYPE );
  try {
    adac.openStream( &oParams, &iParams, FORMAT, fs, &bufferFrames, &inout, (void *)&bufferBytes, &options );
  }
  catch ( RtAudioError& e ) {
    std::cout << '\n' << e.getMessage() << '\n' << std::endl;
    exit( 1 );
  }

  // Test RtAudio functionality for reporting latency.
  std::cout << "\nStream latency = " << adac.getStreamLatency() << " frames" << std::endl;

  try {
    adac.startStream();

    char input;
    std::cout << "\nRunning ... press <enter> to quit (buffer frames = " << bufferFrames << ").\n";
    std::cin.get(input);

    // Stop the stream.
    adac.stopStream();
  }
  catch ( RtAudioError& e ) {
    std::cout << '\n' << e.getMessage() << '\n' << std::endl;
    goto cleanup;
  }

 cleanup:
  if ( adac.isStreamOpen() ) adac.closeStream();

  return 0;
}
