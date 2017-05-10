/******************************************/
/*
  bin_au.cpp
*/
/******************************************/
#include "RtAudio.h"
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <cmath>
#include <signal.h>

#include "RTIMULib.h"
#include <iomanip>
#include <sstream>

#include <wiringPi.h>

#define WLAN_PIN 4
#define CENTER_PIN 5

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
#define DELAY 4096
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
//result of hrir
MY_TYPE out_l_hrir[BUFF_SIZE];
MY_TYPE out_r_hrir[BUFF_SIZE];
//parsed csv
MY_TYPE hrir_file_l[25][50][200];
MY_TYPE hrir_file_r[25][50][200];
MY_TYPE idt_l[25][50];
MY_TYPE idt_r[25][50];
//overlap buffers
MY_TYPE overlap_l[BUFF_SIZE];
MY_TYPE overlap_r[BUFF_SIZE];
MY_TYPE overlap_prev_l[BUFF_SIZE];
MY_TYPE overlap_prev_r[BUFF_SIZE];
MY_TYPE delay_l[DELAY];
MY_TYPE delay_r[DELAY];
int curr_az, curr_el;
int d_ptr_l, d_ptr_r;
double xyz[3];
double tp[2];
/*
typedef S24 MY_TYPE;
#define FORMAT RTAUDIO_SINT24
typedef signed long MY_TYPE;
#define FORMAT RTAUDIO_SINT32
typedef float MY_TYPE;
#define FORMAT RTAUDIO_FLOAT32
*/
int foo = -80;
int bar = 0;
int fubar = 0;
void hrir_update(int az, int el);

volatile sig_atomic_t flag = 0;

void terminator(int sig){
	flag = 1;
}

volatile int counter = 0;

void wlan_interrupt(void){
	std::cout << "wlan triggered!\n";
}

void center_interrupt(void){
	std::cout << "center triggered!\n";
}

//get position in array
void get_index(int az, int el) {

	//if (abs(az) > 72) az = 72;

	if (az >= 72) curr_az = 24;
	else if (az >= 60) curr_az = 23;
	else if (az >= 50) curr_az = 22;
	else if (az >= 0 && az < 50) curr_az = 13 + az / 5;
	//else if (az == 0) curr_az = 13;
	else if (az < 0 && az > -50) curr_az = 4 + (az + 45) / 5;
	else if (az >= -50) curr_az = 3;
	else if (az >= -60) curr_az = 2;
	else if (az >= -72) curr_az = 1;
	else curr_az = 0;
	if (el > 230) el = 230;
	if (el < -45) el = -45;
	curr_el = round(((double)el + 45.0) / 5.625);
}

//parse hrir
bool get_hrir(const char *path, double arr[25][50][200]) {
	int i, j, k;
	i = j = k = 0;
	std::string line;
	std::ifstream fin(path);
	if (!fin.is_open()) {
		std::cout << path << ": ";
		std::cout << "Error: no such file! \n";
		return false;
	}
	int f = 0;
	while (std::getline(fin, line)) {
		std::stringstream ss(line);
		while (getline(ss, line, ',')) {
			f++;
			arr[i][j][k] = strtod(line.c_str(), NULL);
			j++;
			if (j == 50) {
				j = 0;
				k++;
			}
			if (k == 200) {
				k = 0;
				i++;
			}
		}
	}
	return true;
}

//delay
void delay() {
	int del_l, del_r;
	int d_r_ptr;
	int d_l_ptr;
	//fix ! ??

	del_l = floor( ( idt_l[curr_az][curr_el] / 1000.0) * (MY_TYPE)SAMPLE_RATE );
	del_r = floor( ( idt_r[curr_az][curr_el] / 1000.0) * (MY_TYPE)SAMPLE_RATE );

	for (int i = 0; i < BUFF_SIZE; i++) {

		d_l_ptr = (d_ptr_l - del_l) % DELAY + 1;
		if (d_l_ptr < 0) d_l_ptr = DELAY + d_l_ptr;

		d_r_ptr = (d_ptr_r - del_r) % DELAY + 1;
		if (d_r_ptr < 0) d_r_ptr = DELAY + d_r_ptr;

		delay_l[d_ptr_l] = out_l_hrir[i];
		out_l[i] = delay_l[d_l_ptr];
		d_ptr_l++;
		d_ptr_l %= DELAY;

		delay_r[d_ptr_r] = out_r_hrir[i];
		out_r[i] = delay_r[d_r_ptr];
		d_ptr_r++;
		d_ptr_r %= DELAY;
	}
}

//parse delay times
bool get_idt(const char *path, double arr[25][50]) {
	int j, k;
	j = k = 0;
	std::string line;
	std::ifstream fin(path);
	if (!fin.is_open()) {
		std::cout << path << ": ";
		std::cout << "Error: no such file! \n";
		return false;
	}
	int f = 0;
	while (std::getline(fin, line)) {
		std::stringstream ss(line);
		while (getline(ss, line, ',')) {
			f++;
			arr[k][j] = strtod(line.c_str(), NULL);
			j++;
			if (j == 50) {
				j = 0;
				k++;
				k = k % 25;
			}
		}
	}
	return true;
}

//empty router guy
/*void delay(){
	for(int i=0; i<BUFF_SIZE; i++){
		out_l[i]=out_l_hrir[i];
		out_r[i]=out_r_hrir[i];
	}
}*/

//convolve
void convolve() {
	hrir_update(curr_az, curr_el);
	for (int n = 0; n < OUT_SIZE; n++) {
		//fix types
		MY_TYPE L_s, R_s;
		L_s = R_s = 0;
		size_t kmin, kmax, k;
		kmin = (n >= BUFF_SIZE - 1) ? n - (BUFF_SIZE - 1) : 0;
		kmax = (n < HRIR_SIZE - 1) ? n : HRIR_SIZE - 1;
		for (k = kmin; k <= kmax; k++) {
			L_s += mono[n - k] * hrir_l[k];
			R_s += mono[n - k] * hrir_r[k];
		}
		if (n >= BUFF_SIZE) {
			overlap_l[n - BUFF_SIZE] = L_s * 0.5;
			overlap_r[n - BUFF_SIZE] = R_s * 0.5;
		}
		else {
			out_l_hrir[n] = L_s * 0.5;
			out_r_hrir[n] = R_s * 0.5;
		}
	}
}

//overlap add
void overlap() {
	for (int i = 0; i < OUT_SIZE - BUFF_SIZE; i++) {
		out_l_hrir[i] += overlap_prev_l[i];
		//out_l[i] += overlap_prev_l[i];
		overlap_prev_l[i] = overlap_l[i];
		out_r_hrir[i] += overlap_prev_r[i];
		//out_r[i] += overlap_prev_r[i];
		overlap_prev_r[i] = overlap_r[i];
	}
}

//load appropriate transfer function
void hrir_update(int az, int el) {
	int padding = (BUFF_SIZE - HRIR_SIZE) / 2;
	for (int i = 0; i < HRIR_SIZE; i++) {
		hrir_l[i + padding] = hrir_file_l[az][el][i];
		hrir_r[i + padding] = hrir_file_r[az][el][i];
	}
}

//audio callback
int inout( void *outputBuffer, void *inputBuffer, unsigned int /*nBufferFrames*/,
           double /*streamTime*/, RtAudioStreamStatus status, void * /*data*/ )
{
	unsigned int i;
	MY_TYPE *ibuff = (MY_TYPE *) inputBuffer;
	MY_TYPE *obuff = (MY_TYPE *) outputBuffer;
	if ( status ) std::cout << "Stream over/underflow detected." << std::endl;
	for (i = 0; i < BUFFER; i++) {
		if (i % 2 == 0) {
			l_vals[i / 2] = *ibuff++;
			mono[i / 2] = l_vals[i / 2] / 2;
		}
		else {
			r_vals[i / 2] = *ibuff++;
			mono[i / 2] += r_vals[i / 2] / 2;
		}
	}
	convolve();
	overlap();
	delay();
	for (i = 0; i < BUFFER; i++) {
		MY_TYPE val = 0;
		if (i % 2 == 0) {
			//write left channel / interleaved audio / index/2
			//val = l_vals[i/2];
			val = out_l[i / 2];
		}
		else {
			//write right channel
			//val = r_vals[i/2];
			val = out_r[i / 2];
		}
		*obuff++ = val;
	}
	return 0;
}


int main(/* int argc, char *argv[]*/ )
{
	unsigned int channels, fs, bufferBytes, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;

	const char *hrir_l_path = "hrir_l.csv";
	const char *hrir_r_path = "hrir_r.csv";

	const char *idt_l_path = "onl.csv";
	const char *idt_r_path = "onr.csv";

	uint64_t rateTimer;
	uint64_t displayTimer;
	uint64_t originTimer;
	uint64_t now;
	bool set0 = 0;
	double x_o1, y_o1, z_o1, x_o2, y_o2, z_o2;
	double x1,y1,z1,x2,y2,z2;

	x_o1=x_o2=y_o1=y_o2=z_o1=z_o2=0;

	int sampleCount = 0;
	int sampleRate = 0;

	int prev_az;

	if(wiringPiSetup() < 0) {
		std::cout << "couldn't initialize wiringPi :c \n";
	}
	else{

		if( wiringPiISR(WLAN_PIN, INT_EDGE_RISING, &wlan_interrupt) < 0){
			std::cout << "couldn't set up wlan interrupt";
		}

		if( wiringPiISR(CENTER_PIN, INT_EDGE_RISING, &center_interrupt) < 0){
			std::cout << "couldn't set up center interrupt";
		}
	}


	signal(SIGINT, terminator);

	get_idt(idt_l_path, idt_l);
	get_idt(idt_r_path, idt_r);

	get_hrir(hrir_l_path, hrir_file_l);
	get_hrir(hrir_r_path, hrir_file_r);

	RTIMUSettings *settings = new RTIMUSettings("RTIMULib60501");
	RTIMUSettings *settings2 = new RTIMUSettings("RTIMULib60502");

	RTIMU *imu = RTIMU::createIMU(settings);

	RTIMU *imu2 = RTIMU::createIMU(settings2);
	std::cout << "\n";

	imu->IMUInit();
	std::cout << "\n";

	imu2->IMUInit();
	std::cout << "\n";

	//  this is a convenient place to change fusion parameters
	imu->setSlerpPower(0.5);
	imu->setGyroEnable(true);
	imu->setAccelEnable(true);
	imu->setCompassEnable(false);

	imu2->setSlerpPower(0.5);
	imu2->setGyroEnable(true);
	imu2->setAccelEnable(true);
	imu2->setCompassEnable(false);

	if ((imu == NULL) || (imu->IMUType() == RTIMU_TYPE_NULL)) {
		printf("No IMU found\n");
		exit(1);
	}
	if ((imu2 == NULL) || (imu2->IMUType() == RTIMU_TYPE_NULL)) {
		printf("Second IMU not found\n");
		exit(1);
	}

	rateTimer = displayTimer = originTimer = RTMath::currentUSecsSinceEpoch();

	curr_az = curr_el = 0;
	d_ptr_l = d_ptr_r = 0;

	RtAudio adac;

	int ndevs = adac.getDeviceCount();

	RtAudio::DeviceInfo info;

	if ( ndevs < 1 ) {
		std::cout << "\nNo audio devices found!\n";
		exit( 1 );
	} else {
		for (int i = 0; i < ndevs; i++) {
			info = adac.getDeviceInfo(i);
			std::cout << info.name << "   " << info.inputChannels << "   " << info.outputChannels << "   "  << info.duplexChannels << "\n";
		}
	}

	channels = CHANNELS;
	fs = SAMPLE_RATE;

	adac.showWarnings( true );

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
		//char input;
		//std::cout << "\nRunning ... press <enter> to quit (buffer frames = " << bufferFrames << ").\n";
		//std::cin.get(input);
		// Stop the stream.
		//adac.stopStream();
	}

	catch ( RtAudioError& e ) {
		std::cout << '\n' << e.getMessage() << '\n' << std::endl;
		goto cleanup;
	}

	while (1) {
		double interval = imu->IMUGetPollInterval();
		if (interval < imu2->IMUGetPollInterval()) interval = imu2->IMUGetPollInterval();

		usleep(interval * 1000);
		while (imu->IMURead() && imu2->IMURead()) {

			if(flag){
				goto cleanup;
			}

			RTIMU_DATA imuData = imu->getIMUData();
			RTIMU_DATA imuData2 = imu2->getIMUData();

			sampleCount++;
			now = RTMath::currentUSecsSinceEpoch();

			x1 = imuData.fusionPose.x() * RTMATH_RAD_TO_DEGREE;
			y1 = imuData.fusionPose.y() * RTMATH_RAD_TO_DEGREE;
			z1 = imuData.fusionPose.z() * RTMATH_RAD_TO_DEGREE;

			x2 = imuData2.fusionPose.x() * RTMATH_RAD_TO_DEGREE;
			y2 = imuData2.fusionPose.y() * RTMATH_RAD_TO_DEGREE;
			z2 = imuData2.fusionPose.z() * RTMATH_RAD_TO_DEGREE;

			//  display 10 times per second
			if ((now - displayTimer) > 1000) {

				if(set0 == 1){

					x1 = round((x1-x_o1)*10)/10;
					y1 = round((y1-y_o1)*10)/10;
					z1 = round((z1-z_o1)*10)/10;

					x2 = round((x2-x_o2)*10)/10;
					y2 = round((y2-y_o2)*10)/10;
					z2 = round((z2-z_o2)*10)/10;
				}

				size_t headerWidth = std::string("-1800.9").size();

				//int x_out = round( x2 );
				//int y_out = round( y2 );
				int z_out = round( ( abs(z1) + abs(z2) ) / 2 );
				if (z2>0) z_out = -z_out;
				//std::cout << "x_avg: " << std::setw(headerWidth) << x2;
				//std::cout << " y: " << std::setw(headerWidth) << y_out;
				//std::cout << "; z_avg: " << std::setw(headerWidth) << z_out << ";" << std::flush << "\r";
				prev_az = curr_az;
				get_index(z_out, 0);
				if(prev_az!=curr_az) std::cout<<curr_az<<"\n";
				displayTimer = now;

			}
			if((now - originTimer) > 1000000){
				if(set0 == 0){
					std::cout << "\norigin set!\n";
					x_o1 = x1;
					y_o1 = y1;
					z_o1 = z1;

					x_o2 = x2;
					y_o2 = y2;
					z_o2 = z2;
					set0 = 1;
				}
				originTimer = now;
			}

			if ((now - rateTimer) > 100000){
				sampleRate = sampleCount;
				sampleCount = 0;
				rateTimer = now;
			}
		}
	}

cleanup:
	std::cout << "\nclosing stream\n";
	if ( adac.isStreamOpen() ) adac.closeStream();
	std::cout << "\nhasta la vista baby! \n";
	return 0;
}
