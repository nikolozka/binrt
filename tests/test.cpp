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
#include <vector>
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
void origin(double az, double el) {
}
void get_index(int az, int el) {
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
void car_to_pol(double x, double y, double z) {
	double az, el;
	el = round( asin(y) * 180 / 3.1415 );
	az = round( atan2(x, z) * 180.0 / 3.1415 );
	if (x < 0 && y < 0 && z > 0 ) { //1
		az = -abs(az);
		el = -abs(el);
	} else if (x < 0 && y > 0 && z > 0 ) { //2
		az = -abs(az);
		el = abs(el);
	} else if (x < 0 && y > 0 && z < 0) { //3
		az = 180 - abs(az);
		el = 180 - abs(el);
	} else if (x < 0 && y < 0 && z < 0) { //4
		az = 180 - abs(az);
		el = abs(el) + 180;
	} else if (x > 0 && y < 0 && z > 0) { //5
		az = abs(az);
		el = -abs(el);
	} else if (x > 0 && y > 0 && z > 0) { //6
		az = abs(az);
		el = abs(el);
	} else if (x > 0 && y > 0 && z < 0) { //7
		az = -abs(180 - az);
		el = 180 - abs(el);
	} else if (x > 0 && y < 0 && z < 0) { //8
		az = -abs(180 - az);
		el = abs(el) + 180;
	}
	if (x == -1.0) {el = 180;}
	else if (x == 1.0)  {az = 90; el = 0;}
	else if (y == -1.0) {az = 0; el = -90;}
	else if (y == 1.0)  {az = 0; el = 90;}
	else if (z == -1.0) {az = 0; el = 180;}
	else if (z == 1)  {az = 0; el = 0;}
	else if ( x == 0.0) {az = 0; if (z < 0)el = abs(180 - el);}
	else if ( y == 0.0) {
		el = 180;
		if (z < 0 && x > 0) az = abs(az) - 180;
		else if (z < 0 && x < 0) az = abs(180 + az);
		else if (z > 0)  el = 0;
	}
	else if ( z == 0.0) {az = -90; if (x > 0)el = abs(180 - el);}
	if (az == -90.0 && el == 180.0)el = 180 - abs(el);
	if (az == 90.0 && el == 0.0) {az = -abs(az); el = 180 - abs(el);}
	tp[0] = az;
	tp[1] = el;
}
void pol_to_car(double az, double el) {
	double x, y, z;
	double rad_az = ( (double) az * 3.1415) / 180.0;
	double rad_el = ( (double) el * 3.1415) / 180.0;
	y = sin(rad_el);
	z = cos(rad_az) * cos(rad_el);
	x = sin(rad_az) * cos(rad_el);
	if ( el == 90 ) {
		z = 0;
		x = 0;
		y = 1;
	}
	if ( el == -90 ) {
		z = 0;
		x = 0;
		y = -1;
	}
	if (az >= -90 && az < 0 && el >= -90 && el < 0) {
		z = fabs(z);
		y = -fabs(y);
		x = -fabs(x);
	}
	if (az >= -90 && az < 0 && el >= 0 && el < 90) {
		z = fabs(z);
		y = fabs(y);
		x = -fabs(x);
	}
	if (az >= -90 && az < 0 &&  el >= 90 && el < 180) {
		z = -fabs(z);
		y = fabs(y);
		x = fabs(x);
	}
	if (az >= -90 && az < 0 && el >= 180 && el < 270) {
		z = -fabs(z);
		y = -fabs(y);
		x = fabs(x);
	}
	if (az >= 0 && az < 90 && el >= -90 && el < 0) {
		z = fabs(z);
		y = -fabs(y);
		x = fabs(x);
	}
	if (az >= 0 && az < 90 && el >= 0 && el < 90) {
		z = fabs(z);
		y = fabs(y);
		x = fabs(x);
	}
	if (az >= 0 && az < 90 &&  el >= 90 && el < 180) {
		z = -fabs(z);
		y = fabs(y);
		x = -fabs(x);
	}
	if (az >= 0 && az < 90 && el >= 180 && el < 270) {
		z = -fabs(z);
		y = -fabs(y);
		x = -fabs(x);
	}
	x = roundl(x * 1000000) / 1000000;
	y = roundl(y * 1000000) / 1000000;
	z = roundl(z * 1000000) / 1000000;
	xyz[0] = x;
	xyz[1] = y;
	xyz[2] = z;
}
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
	std::cout << f << "\n";
	return true;
}
void delay() {
	int del_l, del_r;
	int d_r_ptr;
	int d_l_ptr;
	//fix !

	//double del = idt_l[curr_az][curr_el] - idt_r[curr_az][curr_el];

	
		del_l = floor( ( idt_l[curr_az][curr_el] / 1000.0) * (MY_TYPE)SAMPLE_RATE );
			
		del_r = floor( ( idt_r[curr_az][curr_el] / 1000.0) * (MY_TYPE)SAMPLE_RATE );
		
		//del_l=del_r=0;
	
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
void hrir_update(int az, int el) {
	int padding = (BUFF_SIZE - HRIR_SIZE) / 2;
	for (int i = 0; i < HRIR_SIZE; i++) {
		hrir_l[i + padding] = hrir_file_l[az][el][i];
		hrir_r[i + padding] = hrir_file_r[az][el][i];
	}
}
int inout( void *outputBuffer, void *inputBuffer, unsigned int /*nBufferFrames*/,
           double /*streamTime*/, RtAudioStreamStatus status, void *data )
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
	fubar++;
	get_index(foo, bar);
	if (fubar == 1000) {
		std::cout << "az: " << curr_az << "; el: " << curr_el <<"\n";
		fubar = 0;
		foo += 80;
	}
	if (foo > 80) {
		foo = -80;
		bar = 180 - bar;
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
int main( int argc, char *argv[] )
{
	unsigned int channels, fs, bufferBytes, oDevice = 0, iDevice = 0, iOffset = 0, oOffset = 0;

	const char *hrir_l_path = "hrir_l.csv";
	const char *hrir_r_path = "hrir_r.csv";

	const char *idt_l_path = "onl.csv";
	const char *idt_r_path = "onr.csv";

	get_idt(idt_l_path, idt_l);
	get_idt(idt_r_path, idt_r);

	get_hrir(hrir_l_path, hrir_file_l);
	get_hrir(hrir_r_path, hrir_file_r);

	curr_az = curr_el = 0;
	d_ptr_l = d_ptr_r = 0;
	RtAudio adac;
	for (int i = -90; i <= 80; i = i + 5) {
		for (int j = -90; j <= 270; j = j + 5) {
			//int i = -90;
			//int j = 180;
			int az1 = i;
			int el1 = j;
			pol_to_car(az1, el1);
			car_to_pol(xyz[0], xyz[1], xyz[2]);
			double az2 = tp[0];
			double el2 = tp[1];
		}
	}
	int ndevs = adac.getDeviceCount();
	RtAudio::DeviceInfo info;
	std::cout << (-2 % 3) << ", ";
	std::cout << 3 - (-2 % 3) << "\n";
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
