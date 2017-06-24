/*

 Copyright (c) 2014 University of Edinburgh, Imperial College, University of Manchester.
 Developed in the PAMELA project, EPSRC Programme Grant EP/K008730/1

 This code is licensed under the MIT License.

 */

#include "common_opencl.h"
#include <kernels.h>

#include <TooN/TooN.h>
#include <TooN/se3.h>
#include <TooN/GR_SVD.h>

////// USE BY KFUSION CLASS ///////////////

// input once
cl_mem ocl_gaussian = NULL;

// inter-frame
Matrix4 oldPose;
Matrix4 raycastPose;
cl_mem ocl_vertex = NULL;
cl_mem ocl_normal = NULL;
cl_mem ocl_volume_data = NULL;
cl_mem ocl_depth_buffer = NULL;
cl_mem ocl_output_render_buffer = NULL; // Common buffer for rendering track, depth and volume


// intra-frame
cl_mem ocl_reduce_output_buffer = NULL;
cl_mem ocl_trackingResult = NULL;
cl_mem ocl_FloatDepth = NULL;
cl_mem * ocl_ScaledDepth = NULL;
cl_mem * ocl_inputVertex = NULL;
cl_mem * ocl_inputNormal = NULL;
float * reduceOutputBuffer = NULL;

//kernels
cl_kernel mm2meters_ocl_kernel;
cl_kernel bilateralFilter_ocl_kernel;
cl_kernel halfSampleRobustImage_ocl_kernel;
cl_kernel depth2vertex_ocl_kernel;
cl_kernel vertex2normal_ocl_kernel;
cl_kernel track_ocl_kernel;
cl_kernel reduce_ocl_kernel;
cl_kernel integrate_ocl_kernel;
cl_kernel raycast_ocl_kernel;
cl_kernel renderVolume_ocl_kernel;
cl_kernel renderLight_ocl_kernel;
cl_kernel renderTrack_ocl_kernel;
cl_kernel renderDepth_ocl_kernel;
cl_kernel initVolume_ocl_kernel;


////////////////////////////////////

int bool_frame=0;

float3 ** inputVertex;
float3 ** inputNormal;

float3 * vertex;

float3 * normal;

TrackData * trackingResult;
///////////////////////////////////////////  timing

void  checkError(cl_int err,std::string str) {

	if (err != CL_SUCCESS)  {
	std::cout << str << std::endl; exit(err);
	}
}

unsigned long int computeEventDuration(cl_event* event) {
	if (event == NULL)
		throw std::runtime_error(
				"Error computing event duration. \
                              Event is not initialized");
	cl_int errorCode;
	cl_ulong start, end;
	errorCode = clGetEventProfilingInfo(*event, CL_PROFILING_COMMAND_START,
			sizeof(cl_ulong), &start, NULL);
	checkError(errorCode, "Error querying the event start time");
	errorCode = clGetEventProfilingInfo(*event, CL_PROFILING_COMMAND_END,
			sizeof(cl_ulong), &end, NULL);
	checkError(errorCode, "Error querying the event end time");
	return static_cast<unsigned long>(end - start);
}


cl_event write_event[8];
cl_event kernel_event[100];
cl_event finish_event[100];


//////////////////////////////////////////












// reduction parameters
static const size_t size_of_group = 64;
static const size_t number_of_groups = 8;

uint2 computationSizeBkp = make_uint2(0, 0);
uint2 outputImageSizeBkp = make_uint2(0, 0);

void init() {
	opencl_init();
}

void clean() {
	opencl_clean();


}

void Kfusion::languageSpecificConstructor() {

	init();

	
	cl_ulong maxMemAlloc;
	clGetDeviceInfo(device_id, CL_DEVICE_MAX_MEM_ALLOC_SIZE, sizeof(maxMemAlloc), &maxMemAlloc, NULL);
	
	if (maxMemAlloc < sizeof(float4) * computationSize.x * computationSize.y) {
	  std::cerr << "OpenCL maximum allocation does not support the computation size." << std::endl;
	  exit(1);
	}
	if (maxMemAlloc < sizeof(TrackData) * computationSize.x * computationSize.y) {
	  std::cerr << "OpenCL maximum allocation does not support the computation size." << std::endl;
	  exit(1);
	}
	if (maxMemAlloc < sizeof(short2) * volumeResolution.x * volumeResolution.y * volumeResolution.z) {
	  std::cerr << "OpenCL maximum allocation does not support the volume resolution." << std::endl;
	  exit(1);
	}
	
	
	ocl_FloatDepth = clCreateBuffer(context, CL_MEM_READ_WRITE,
			sizeof(float) * computationSize.x * computationSize.y, NULL,
			&clError);
	checkErr(clError, "clCreateBuffer");
	ocl_ScaledDepth = (cl_mem*) malloc(sizeof(cl_mem) * iterations.size());
	ocl_inputVertex = (cl_mem*) malloc(sizeof(cl_mem) * iterations.size());
	ocl_inputNormal = (cl_mem*) malloc(sizeof(cl_mem) * iterations.size());

////////////
	//scaled depth decleration
	//ScaledDepth = (float**) calloc(sizeof(float*) * iterations.size(), 1);

	//input vertex decleration
	inputVertex = (float3**) calloc(sizeof(float3*) * iterations.size(), 1);

	//inputNormal decleration
	inputNormal = (float3**) calloc(sizeof(float3*) * iterations.size(), 1);


////////////////

	for (unsigned int i = 0; i < iterations.size(); ++i) {
		ocl_ScaledDepth[i] = clCreateBuffer(context, CL_MEM_READ_WRITE,
				sizeof(float) * (computationSize.x * computationSize.y)
						/ (int) pow(2, i), NULL, &clError);
			checkErr(clError, "clCreateBuffer");



/////////////////

//scaled depth decleration
		//ScaledDepth[i] = (float*) calloc(
		//		sizeof(float) * (computationSize.x * computationSize.y)
		//
		//		/ (int) pow(2, i), 1);
//input vertex decleration
		inputVertex[i] = (float3*) calloc(
				sizeof(float3) * (computationSize.x * computationSize.y)
						/ (int) pow(2, i), 1);


//input normal decleration
		inputNormal[i] = (float3*) calloc(
				sizeof(float3) * (computationSize.x * computationSize.y)
						/ (int) pow(2, i), 1);

///////////////////

		ocl_inputVertex[i] = clCreateBuffer(context, CL_MEM_READ_WRITE,
				sizeof(float3) * (computationSize.x * computationSize.y)
						/ (int) pow(2, i), NULL, &clError);
			checkErr(clError, "clCreateBuffer");
		ocl_inputNormal[i] = clCreateBuffer(context, CL_MEM_READ_WRITE,
				sizeof(float3) * (computationSize.x * computationSize.y)
						/ (int) pow(2, i), NULL, &clError);
			checkErr(clError, "clCreateBuffer");
	}

	ocl_vertex = clCreateBuffer(context, CL_MEM_READ_WRITE,
			sizeof(float3) * computationSize.x * computationSize.y, NULL,
			&clError);
		checkErr(clError, "clCreateBuffer");

///////

	//normal decleration
	vertex = (float3*) calloc(sizeof(float3) * computationSize.x * computationSize.y, 1);

	//normal decleration

	normal = (float3*) calloc(
			sizeof(float3) * computationSize.x * computationSize.y, 1);


	//tracking result
	trackingResult = (TrackData*) calloc(
			sizeof(TrackData) * computationSize.x * computationSize.y, 1);
///////

	ocl_normal = clCreateBuffer(context, CL_MEM_READ_WRITE,
			sizeof(float3) * computationSize.x * computationSize.y, NULL,
			&clError);
		checkErr(clError, "clCreateBuffer");
	ocl_trackingResult = clCreateBuffer(context, CL_MEM_READ_WRITE,
			sizeof(TrackData) * computationSize.x * computationSize.y, NULL,
			&clError);
		checkErr(clError, "clCreateBuffer");

	ocl_reduce_output_buffer = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
			32 * number_of_groups * sizeof(float), NULL, &clError);
		checkErr(clError, "clCreateBuffer");
	reduceOutputBuffer = (float*) malloc(number_of_groups * 32 * sizeof(float));
	// ********* BEGIN : Generate the gaussian *************
	size_t gaussianS = radius * 2 + 1;
	float *gaussian = (float*) malloc(gaussianS * sizeof(float));
	int x;
	for (unsigned int i = 0; i < gaussianS; i++) {
		x = i - 2;
		gaussian[i] = expf(-(x * x) / (2 * delta * delta));
	}
	ocl_gaussian = clCreateBuffer(context, CL_MEM_READ_ONLY,
			gaussianS * sizeof(float), NULL, &clError);
		checkErr(clError, "clCreateBuffer");
	clError = clEnqueueWriteBuffer(commandQueue, ocl_gaussian, CL_TRUE, 0,
			gaussianS * sizeof(float), gaussian, 0, NULL, NULL);
		checkErr(clError, "clEnqueueWrite");
	free(gaussian);
	// ********* END : Generate the gaussian *************

	// Create kernel
	initVolume_ocl_kernel = clCreateKernel(program,
			"initVolumeKernel", &clError);
	checkErr(clError, "clCreateKernel");

	ocl_volume_data = clCreateBuffer(context, CL_MEM_READ_WRITE,
			sizeof(short2) * volumeResolution.x * volumeResolution.y
					* volumeResolution.z,
			NULL, &clError);
	checkErr(clError, "clCreateBuffer");
	clError = clSetKernelArg(initVolume_ocl_kernel, 0, sizeof(cl_mem),
			&ocl_volume_data);
	checkErr(clError, "clSetKernelArg");

	size_t globalWorksize[3] = { volumeResolution.x, volumeResolution.y,
			volumeResolution.z };

	clError = clEnqueueNDRangeKernel(commandQueue, initVolume_ocl_kernel, 3,
			NULL, globalWorksize, NULL, 0, NULL, NULL);
	checkErr(clError, "clEnqueueNDRangeKernel");


	//Kernels
	mm2meters_ocl_kernel = clCreateKernel(program, "mm2metersKernel", &clError);
	checkErr(clError, "clCreateKernel");
	bilateralFilter_ocl_kernel = clCreateKernel(program,
			"bilateralFilterKernel", &clError);
	checkErr(clError, "clCreateKernel");
	halfSampleRobustImage_ocl_kernel = clCreateKernel(program,
			"halfSampleRobustImageKernel", &clError);
	checkErr(clError, "clCreateKernel");
	depth2vertex_ocl_kernel = clCreateKernel(program, "depth2vertexKernel",
			&clError);
	checkErr(clError, "clCreateKernel");
	vertex2normal_ocl_kernel = clCreateKernel(program, "vertex2normalKernel",
			&clError);
	checkErr(clError, "clCreateKernel");
	track_ocl_kernel = clCreateKernel(program, "trackKernel", &clError);
	checkErr(clError, "clCreateKernel");
	reduce_ocl_kernel = clCreateKernel(program, "reduceKernel", &clError);
	checkErr(clError, "clCreateKernel");
	integrate_ocl_kernel = clCreateKernel(program, "integrateKernel", &clError);
	checkErr(clError, "clCreateKernel");
	raycast_ocl_kernel = clCreateKernel(program, "raycastKernel", &clError);
	checkErr(clError, "clCreateKernel");
	renderVolume_ocl_kernel = clCreateKernel(program, "renderVolumeKernel",
			&clError);
	checkErr(clError, "clCreateKernel");
	renderDepth_ocl_kernel = clCreateKernel(program, "renderDepthKernel",
			&clError);
	checkErr(clError, "clCreateKernel");
	renderTrack_ocl_kernel = clCreateKernel(program, "renderTrackKernel",
			&clError);
	checkErr(clError, "clCreateKernel");

}
Kfusion::~Kfusion() {

	if (reduceOutputBuffer)
		free(reduceOutputBuffer);
	reduceOutputBuffer = NULL;

	for (unsigned int i = 0; i < iterations.size(); ++i) {


	  if (ocl_ScaledDepth[i]) {
	    clError = clReleaseMemObject(ocl_ScaledDepth[i]);
	    checkErr(clError, "clReleaseMem");
		ocl_ScaledDepth[i] = NULL;
	  }

///////// 

//free scaled depth
	//free(ScaledDepth[i]);

//free input vertex
	free(inputVertex[i]);

//free input normal
	free(inputNormal[i]);
/////////

	  if (ocl_inputVertex[i]) {
	    clError = clReleaseMemObject(ocl_inputVertex[i]);
	    checkErr(clError, "clReleaseMem");
		ocl_inputVertex[i] = NULL;
	  }
	  if (ocl_inputNormal[i]) {
	    clError = clReleaseMemObject(ocl_inputNormal[i]);
	    checkErr(clError, "clReleaseMem");
		ocl_inputNormal[i] = NULL;
	  }
	}
	if (ocl_ScaledDepth) {
		free(ocl_ScaledDepth);
	ocl_ScaledDepth = NULL;
	}

///////
	//free(ScaledDepth);

//////
	if (ocl_inputVertex) {
		free(ocl_inputVertex);
	ocl_inputVertex = NULL;
	}
/////
	free(inputVertex);
//////
	if (ocl_inputNormal) {
		free(ocl_inputNormal);
	ocl_inputNormal = NULL;
	}
////
	free(inputNormal);
////

	if (ocl_FloatDepth) {
	  clError = clReleaseMemObject(ocl_FloatDepth);
	  checkErr(clError, "clReleaseMem");
	ocl_FloatDepth = NULL;
	}
	if (ocl_vertex) {
	 clError = 	clReleaseMemObject(ocl_vertex);
		checkErr(clError, "clReleaseMem");
	ocl_vertex = NULL;
	}
/////
	free(vertex);
/////
	if (ocl_normal) {
	   clError = clReleaseMemObject(ocl_normal);
	  checkErr(clError, "clReleaseMem");
	ocl_normal = NULL;
	}
/////
	free(normal);
/////
	if (ocl_trackingResult) {
	 clError = 	clReleaseMemObject(ocl_trackingResult);
		checkErr(clError, "clReleaseMem");
	ocl_trackingResult = NULL;
	}
/////
	free(trackingResult);
/////
	if (ocl_gaussian) {
	 clError = 	clReleaseMemObject(ocl_gaussian);
		checkErr(clError, "clReleaseMem");
	ocl_gaussian = NULL;
	}
	if (ocl_volume_data) {
	 clError = 	clReleaseMemObject(ocl_volume_data);
		checkErr(clError, "clReleaseMem");
	ocl_volume_data = NULL;
	}
	if (ocl_depth_buffer) {
	 clError = 	clReleaseMemObject(ocl_depth_buffer);
		checkErr(clError, "clReleaseMem");
	ocl_depth_buffer = NULL;
	}
	if(ocl_output_render_buffer) {
	     clError = clReleaseMemObject(ocl_output_render_buffer);
	    checkErr(clError, "clReleaseMem");
	ocl_output_render_buffer = NULL;
	}
	if (ocl_reduce_output_buffer) {
	 clError = 	clReleaseMemObject(ocl_reduce_output_buffer);
		checkErr(clError, "clReleaseMem");
	ocl_reduce_output_buffer = NULL;
	}
	RELEASE_KERNEL(mm2meters_ocl_kernel);
	RELEASE_KERNEL(bilateralFilter_ocl_kernel);
	RELEASE_KERNEL(halfSampleRobustImage_ocl_kernel);
	RELEASE_KERNEL(depth2vertex_ocl_kernel);
	RELEASE_KERNEL(vertex2normal_ocl_kernel);
	RELEASE_KERNEL(track_ocl_kernel);
	RELEASE_KERNEL(reduce_ocl_kernel);
	RELEASE_KERNEL(integrate_ocl_kernel);
	RELEASE_KERNEL(raycast_ocl_kernel);
	RELEASE_KERNEL(renderVolume_ocl_kernel);
	RELEASE_KERNEL(renderDepth_ocl_kernel);
	RELEASE_KERNEL(renderTrack_ocl_kernel);
	RELEASE_KERNEL(initVolume_ocl_kernel);

	mm2meters_ocl_kernel = NULL ;
	bilateralFilter_ocl_kernel = NULL;
	halfSampleRobustImage_ocl_kernel = NULL;
	depth2vertex_ocl_kernel = NULL;
	vertex2normal_ocl_kernel = NULL;
	track_ocl_kernel = NULL;
	reduce_ocl_kernel = NULL;
	integrate_ocl_kernel = NULL;
	raycast_ocl_kernel = NULL;
	renderVolume_ocl_kernel = NULL;
	renderLight_ocl_kernel = NULL;
	renderTrack_ocl_kernel = NULL;
	renderDepth_ocl_kernel = NULL;

	computationSizeBkp = make_uint2(0, 0);
	outputImageSizeBkp = make_uint2(0, 0);

	clean();
}

bool updatePoseKernel(Matrix4 & pose, const float * output,
		float icp_threshold) {

	// Update the pose regarding the tracking result
	TooN::Matrix<8, 32, const float, TooN::Reference::RowMajor> values(output);
	TooN::Vector<6> x = solve(values[0].slice<1, 27>());
	TooN::SE3<> delta(x);
	pose = toMatrix4(delta) * pose;

	// Return validity test result of the tracking
	if (norm(x) < icp_threshold)
		return true;
	return false;

}

bool checkPoseKernel(Matrix4 & pose, Matrix4 oldPose, const float * output,
		uint2 imageSize, float track_threshold) {

	// Check the tracking result, and go back to the previous camera position if necessary
	if ((std::sqrt(output[0] / output[28]) > 2e-2)
			|| (output[28] / (imageSize.x * imageSize.y) < track_threshold)) {
		pose = oldPose;
		return false;
	} else {
		return true;
	}

}

void Kfusion::reset() {
	std::cerr
			<< "Reset function to clear volume model needs to be implemented\n";
	exit(1);
}

void Kfusion::renderVolume(uchar4 * out, uint2 outputSize, int frame, int rate,
	float4 k, float largestep) {
    if (frame % rate != 0) return;
    // Create render opencl buffer if needed
    if(outputImageSizeBkp.x < outputSize.x || outputImageSizeBkp.y < outputSize.y || ocl_output_render_buffer == NULL) 
    {
	outputImageSizeBkp = make_uint2(outputSize.x, outputSize.y);
	if(ocl_output_render_buffer != NULL){
	    std::cout << "Release" << std::endl;
	    clError = clReleaseMemObject(ocl_output_render_buffer);
	    checkErr( clError, "clReleaseMemObject");
	}
	ocl_output_render_buffer = clCreateBuffer(context,  CL_MEM_WRITE_ONLY, outputSize.x * outputSize.y * sizeof(uchar4), NULL , &clError);
	checkErr( clError, "clCreateBuffer output" );
    }

	Matrix4 view = *viewPose * getInverseCameraMatrix(k);
	// set param and run kernel



    clError = clSetKernelArg(renderVolume_ocl_kernel, 0, sizeof(cl_mem), (void*) &ocl_output_render_buffer);
	checkErr(clError, "clSetKernelArg0");

	clError = clSetKernelArg(renderVolume_ocl_kernel, 1, sizeof(cl_mem),
			(void*) &ocl_volume_data);
	checkErr(clError, "clSetKernelArg1");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 2, sizeof(cl_uint3),
			(void*) &volumeResolution);
	checkErr(clError, "clSetKernelArg2");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 3, sizeof(cl_float3),
			(void*) &volumeDimensions);
	checkErr(clError, "clSetKernelArg3");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 4, sizeof(Matrix4),
			(void*) &view);
	checkErr(clError, "clSetKernelArg4");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 5, sizeof(cl_float),
			(void*) &nearPlane);
	checkErr(clError, "clSetKernelArg5");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 6, sizeof(cl_float),
			(void*) &farPlane);
	checkErr(clError, "clSetKernelArg6");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 7, sizeof(cl_float),
			(void*) &step);
	checkErr(clError, "clSetKernelArg7");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 8, sizeof(cl_float),
			(void*) &largestep);
	checkErr(clError, "clSetKernelArg8");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 9, sizeof(cl_float3),
			(void*) &light);
	checkErr(clError, "clSetKernelArg9");
	clError = clSetKernelArg(renderVolume_ocl_kernel, 10, sizeof(cl_float3),
			(void*) &ambient);
	checkErr(clError, "clSetKernelArg10");

	size_t globalWorksize[2] = { computationSize.x, computationSize.y };

	clError = clEnqueueNDRangeKernel(commandQueue, renderVolume_ocl_kernel, 2,
	NULL, globalWorksize, NULL, 0, NULL, NULL);
	checkErr(clError, "clEnqueueNDRangeKernel");

    clError = clEnqueueReadBuffer(commandQueue, ocl_output_render_buffer, CL_FALSE, 0, outputSize.x * outputSize.y * sizeof(uchar4), out, 0, NULL, NULL );  
    checkErr( clError, "clEnqueueReadBuffer");
}

void Kfusion::renderTrack(uchar4 * out, uint2 outputSize) {
    // Create render opencl buffer if needed
    if(outputImageSizeBkp.x < outputSize.x || outputImageSizeBkp.y < outputSize.y || ocl_output_render_buffer == NULL) 
    {
	outputImageSizeBkp = make_uint2(outputSize.x, outputSize.y);
	if(ocl_output_render_buffer != NULL){
	    std::cout << "Release" << std::endl;
	    clError = clReleaseMemObject(ocl_output_render_buffer);
	    checkErr( clError, "clReleaseMemObject");
	}
	ocl_output_render_buffer = clCreateBuffer(context,  CL_MEM_WRITE_ONLY, outputSize.x * outputSize.y * sizeof(uchar4), NULL , &clError);
	checkErr( clError, "clCreateBuffer output" );
    }

	// set param and run kernel
	clError = clSetKernelArg(renderTrack_ocl_kernel, 0, sizeof(cl_mem),
			&ocl_output_render_buffer);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(renderTrack_ocl_kernel, 1, sizeof(cl_mem),
			&ocl_trackingResult);
	checkErr(clError, "clSetKernelArg");

	size_t globalWorksize[2] = { computationSize.x, computationSize.y };

	clError = clEnqueueNDRangeKernel(commandQueue, renderTrack_ocl_kernel, 2,
			NULL, globalWorksize, NULL, 0, NULL, NULL);
	checkErr(clError, "clEnqueueNDRangeKernel");

    clError = clEnqueueReadBuffer(commandQueue, ocl_output_render_buffer, CL_FALSE, 0, outputSize.x * outputSize.y * sizeof(uchar4), out, 0, NULL, NULL );  
    checkErr( clError, "clEnqueueReadBuffer");

}

void Kfusion::renderDepth(uchar4 * out, uint2 outputSize) {
    // Create render opencl buffer if needed
    if(outputImageSizeBkp.x < outputSize.x || outputImageSizeBkp.y < outputSize.y || ocl_output_render_buffer == NULL) 
    {
	outputImageSizeBkp = make_uint2(outputSize.x, outputSize.y);
	if(ocl_output_render_buffer != NULL){
	    std::cout << "Release" << std::endl;
	    clError = clReleaseMemObject(ocl_output_render_buffer);
	    checkErr( clError, "clReleaseMemObject");
	}
	ocl_output_render_buffer = clCreateBuffer(context,  CL_MEM_WRITE_ONLY, outputSize.x * outputSize.y * sizeof(uchar4), NULL , &clError);
	checkErr( clError, "clCreateBuffer output" );
    }

	// set param and run kernel
	clError = clSetKernelArg(renderDepth_ocl_kernel, 0, sizeof(cl_mem),
			&ocl_output_render_buffer);
	clError &= clSetKernelArg(renderDepth_ocl_kernel, 1, sizeof(cl_mem),
			&ocl_FloatDepth);
	clError &= clSetKernelArg(renderDepth_ocl_kernel, 2, sizeof(cl_float),
			&nearPlane);
	clError &= clSetKernelArg(renderDepth_ocl_kernel, 3, sizeof(cl_float),
			&farPlane);
	checkErr(clError, "clSetKernelArg");

	size_t globalWorksize[2] = { computationSize.x, computationSize.y };

	clError = clEnqueueNDRangeKernel(commandQueue, renderDepth_ocl_kernel, 2,
			NULL, globalWorksize, NULL, 0, NULL, NULL);
	checkErr(clError, "clEnqueueNDRangeKernel");


    clError = clEnqueueReadBuffer(commandQueue, ocl_output_render_buffer, CL_FALSE, 0, outputSize.x * outputSize.y * sizeof(uchar4), out, 0, NULL, NULL );  
    checkErr( clError, "clEnqueueReadBuffer");

}

void Kfusion::dumpVolume(std::string filename) {

	std::ofstream fDumpFile;

	if (filename == "") {
		return;
	}

	fDumpFile.open(filename.c_str(), std::ios::out | std::ios::binary);
	if (fDumpFile.fail()) {
		std::cout << "Error opening file: " << filename << std::endl;
		exit(1);
	}
	short2 * volume_data = (short2*) malloc(
			volumeResolution.x * volumeResolution.y * volumeResolution.z
					* sizeof(short2));
	clEnqueueReadBuffer(commandQueue, ocl_volume_data, CL_TRUE, 0,
			volumeResolution.x * volumeResolution.y * volumeResolution.z
					* sizeof(short2), volume_data, 0, NULL, NULL);

	std::cout << "Dumping the volumetric representation on file: " << filename
			<< std::endl;

	// Dump on file without the y component of the short2 variable
	for (unsigned int i = 0;
			i < volumeResolution.x * volumeResolution.y * volumeResolution.z;
			i++) {
		fDumpFile.write((char *) (volume_data + i), sizeof(short));
	}

	fDumpFile.close();
	free(volume_data);

}

bool Kfusion::preprocessing(const uint16_t * inputDepth, const uint2 inSize) {

	// bilateral_filter(ScaledDepth[0], inputDepth, inputSize , gaussian, e_delta, radius);
	uint2 outSize = computationSize;

	// Check for unsupported conditions
	if ((inSize.x < outSize.x) || (inSize.y < outSize.y)) {
		std::cerr << "Invalid ratio." << std::endl;
		exit(1);
	}
	if ((inSize.x % outSize.x != 0) || (inSize.y % outSize.y != 0)) {
		std::cerr << "Invalid ratio." << std::endl;
		exit(1);
	}
	if ((inSize.x / outSize.x != inSize.y / outSize.y)) {
		std::cerr << "Invalid ratio." << std::endl;
		exit(1);
	}

	int ratio = inSize.x / outSize.x;

	if (computationSizeBkp.x < inSize.x|| computationSizeBkp.y < inSize.y || ocl_depth_buffer == NULL) {
		computationSizeBkp = make_uint2(inSize.x, inSize.y);
		if (ocl_depth_buffer != NULL) {
			clError = clReleaseMemObject(ocl_depth_buffer);
			checkErr(clError, "clReleaseMemObject");
		}
		ocl_depth_buffer = clCreateBuffer(context, CL_MEM_READ_WRITE,
				inSize.x * inSize.y * sizeof(uint16_t), NULL, &clError);
		checkErr(clError, "clCreateBuffer input");
	}
	clError = clEnqueueWriteBuffer(commandQueue, ocl_depth_buffer, CL_FALSE, 0,
			inSize.x * inSize.y * sizeof(uint16_t), inputDepth, 0, NULL, NULL);
	checkErr(clError, "clEnqueueWriteBuffer");

	int arg = 0;
	clError = clSetKernelArg(mm2meters_ocl_kernel, arg++, sizeof(cl_mem),
			&ocl_FloatDepth);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(mm2meters_ocl_kernel, arg++, sizeof(cl_uint2),
			&outSize);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(mm2meters_ocl_kernel, arg++, sizeof(cl_mem),
			&ocl_depth_buffer);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(mm2meters_ocl_kernel, arg++, sizeof(cl_uint2),
			&inSize);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(mm2meters_ocl_kernel, arg++, sizeof(cl_int),
			&ratio);
	checkErr(clError, "clSetKernelArg");

	size_t globalWorksize[2] = { outSize.x, outSize.y };

	clError = clEnqueueNDRangeKernel(commandQueue, mm2meters_ocl_kernel, 2,
			NULL, globalWorksize, NULL, 0, NULL, NULL);
	checkErr(clError, "clEnqueueNDRangeKernel");

	arg = 0;
	clError = clSetKernelArg(bilateralFilter_ocl_kernel, arg++, sizeof(cl_mem),
			&ocl_ScaledDepth[0]);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(bilateralFilter_ocl_kernel, arg++, sizeof(cl_mem),
			&ocl_FloatDepth);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(bilateralFilter_ocl_kernel, arg++, sizeof(cl_mem),
			&ocl_gaussian);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(bilateralFilter_ocl_kernel, arg++,
			sizeof(cl_float), &e_delta);
	checkErr(clError, "clSetKernelArg");
	clError = clSetKernelArg(bilateralFilter_ocl_kernel, arg++, sizeof(cl_int),
			&radius);
	checkErr(clError, "clSetKernelArg");

	clError = clEnqueueNDRangeKernel(commandQueue, bilateralFilter_ocl_kernel, 2,
			NULL, globalWorksize, NULL, 0, NULL, NULL);
	checkErr(clError, "clEnqueueNDRangeKernel");

	return true;

}
bool Kfusion::tracking(float4 k, float icp_threshold, uint tracking_rate,
		uint frame) {

	if ((frame % tracking_rate) != 0)
		return false;







	// half sample the input depth maps into the pyramid levels
	for (unsigned int i = 1; i < iterations.size(); ++i) {
		//halfSampleRobustImage(ScaledDepth[i], ScaledDepth[i-1], make_uint2( inputSize.x  / (int)pow(2,i) , inputSize.y / (int)pow(2,i) )  , e_delta * 3, 1);
		uint2 outSize = make_uint2(computationSize.x / (int) pow(2, i),
				computationSize.y / (int) pow(2, i));

		float e_d = e_delta * 3;
		int r = 1;
		uint2 inSize = outSize * 2;

		//std::cout<<inSize.x<<"  "<<inSize.y<<std::endl;

		int arg = 0;
		clError = clSetKernelArg(halfSampleRobustImage_ocl_kernel, arg++,
				sizeof(cl_mem), &ocl_ScaledDepth[i]);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(halfSampleRobustImage_ocl_kernel, arg++,
				sizeof(cl_mem), &ocl_ScaledDepth[i - 1]);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(halfSampleRobustImage_ocl_kernel, arg++,
				sizeof(cl_uint2), &inSize);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(halfSampleRobustImage_ocl_kernel, arg++,
				sizeof(cl_float), &e_d);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(halfSampleRobustImage_ocl_kernel, arg++,
				sizeof(cl_int), &r);
		checkErr(clError, "clSetKernelArg");

		size_t globalWorksize[2] = { outSize.x, outSize.y };

		clError = clEnqueueNDRangeKernel(commandQueue,
				halfSampleRobustImage_ocl_kernel, 2, NULL, globalWorksize, NULL,
				0,
				NULL, NULL);
		checkErr(clError, "clEnqueueNDRangeKernel");


	}





	// prepare the 3D information from the input depth maps
	

	uint2 localimagesize = computationSize;
	for (unsigned int i = 0; i < iterations.size(); ++i) {

		//std::cout<<"kx:"<<k.x<<" ky:"<<k.y<<" kz:"<<k.z<<" kw:"<<k.w<<std::endl;
		Matrix4 invK = getInverseCameraMatrix(k / float(1 << i));



		//std::cout<<invK.data[0].x<<invK.data[0].y<<invK.data[0].z<<invK.data[0].w<<std::endl;

		uint2 imageSize = localimagesize;
		// Create kernel

		int arg = 0;
		clError = clSetKernelArg(depth2vertex_ocl_kernel, arg++, sizeof(cl_mem),
				&ocl_inputVertex[i]);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(depth2vertex_ocl_kernel, arg++,
				sizeof(cl_uint2), &imageSize);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(depth2vertex_ocl_kernel, arg++, sizeof(cl_mem),
				&ocl_ScaledDepth[i]);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(depth2vertex_ocl_kernel, arg++,
				sizeof(cl_uint2), &imageSize);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(depth2vertex_ocl_kernel, arg++,
				sizeof(Matrix4), &invK);
		checkErr(clError, "clSetKernelArg");

		size_t globalWorksize[2] = { imageSize.x, imageSize.y };

		clError = clEnqueueNDRangeKernel(commandQueue, depth2vertex_ocl_kernel,
				2,
				NULL, globalWorksize, NULL, 0, NULL, NULL);
		checkErr(clError, "clEnqueueNDRangeKernel");

		localimagesize = make_uint2(localimagesize.x / 2, localimagesize.y / 2);
	}	//end of the for loop









	uint2 localimagesize_1 = computationSize;
	for (unsigned int i = 0; i < iterations.size(); ++i) {

		uint2 imageSize = localimagesize_1;

		int arg = 0;
		clError = clSetKernelArg(vertex2normal_ocl_kernel, arg++,
				sizeof(cl_mem), &ocl_inputNormal[i]);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(vertex2normal_ocl_kernel, arg++,
				sizeof(cl_uint2), &imageSize);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(vertex2normal_ocl_kernel, arg++,
				sizeof(cl_mem), &ocl_inputVertex[i]);
		checkErr(clError, "clSetKernelArg");
		clError = clSetKernelArg(vertex2normal_ocl_kernel, arg++,
				sizeof(cl_uint2), &imageSize);
		checkErr(clError, "clSetKernelArg");

		size_t globalWorksize2[2] = { imageSize.x, imageSize.y };

		clError = clEnqueueNDRangeKernel(commandQueue, vertex2normal_ocl_kernel,
				2,
				NULL, globalWorksize2, NULL, 0, NULL, NULL);
		checkErr(clError, "clEnqueueNDRangeKernel");





		localimagesize_1 = make_uint2(localimagesize_1.x / 2, localimagesize_1.y / 2);
	}	//end of the for loop























	/////////////////

		//std::cout<<iterations.size()<<std::endl;
		//std::cout<<computationSize.x<<"  "<<computationSize.y<<std::endl;



		//to change the opencl version to cpp version we need to write ocl to host version first


			//input vertex
			clError = clEnqueueReadBuffer(commandQueue,ocl_inputVertex[0], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 0) * sizeof(float3), inputVertex[0], 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

		//to change the opencl version to cpp version we need to write ocl_scaled_depth to scaled_depth first
			clError = clEnqueueReadBuffer(commandQueue,ocl_inputVertex[1], CL_TRUE, 0,computationSize.x * computationSize.y / (int) pow(2, 1) * sizeof(float3), inputVertex[1], 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

		//to change the opencl version to cpp version we need to write ocl_scaled_depth to scaled_depth first
			clError = clEnqueueReadBuffer(commandQueue,ocl_inputVertex[2], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 2) * sizeof(float3), inputVertex[2], 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

			
			//input normal
			clError = clEnqueueReadBuffer(commandQueue,ocl_inputNormal[0], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 0) * sizeof(float3), inputNormal[0], 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

			clError = clEnqueueReadBuffer(commandQueue,ocl_inputNormal[1], CL_TRUE, 0,computationSize.x * computationSize.y / (int) pow(2, 1) * sizeof(float3), inputNormal[1], 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");
			clError = clEnqueueReadBuffer(commandQueue,ocl_inputNormal[2], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 2) * sizeof(float3), inputNormal[2], 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

			//normal
			clError = clEnqueueReadBuffer(commandQueue,ocl_normal, CL_TRUE, 0,computationSize.x * computationSize.y * sizeof(float3), normal, 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

			//vertex
			clError = clEnqueueReadBuffer(commandQueue,ocl_vertex, CL_TRUE, 0,computationSize.x * computationSize.y * sizeof(float3), vertex, 0, NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");



		//writing input vertex 0 to a text file

		if(frame==4){
		
			//input vertex
			std::ofstream myfile1;
			myfile1.open("input_vertex_0.txt");
			for(int i=0;i<(320*240);i++){
				myfile1<<inputVertex[0][i].x<<" "<<inputVertex[0][i].y<<" "<<inputVertex[0][i].z<<std::endl;
	
			}
			std::cout<<"input write 1 success"<<std::endl;
			myfile1.close();


			std::ofstream myfile2;
			myfile2.open("input_vertex_1.txt");
			for(int i=0;i<(160*120);i++){
				myfile2<<inputVertex[1][i].x<<" "<<inputVertex[1][i].y<<" "<<inputVertex[1][i].z<<std::endl;
	
			}
			std::cout<<"input write 2 success"<<std::endl;
			myfile2.close();


			std::ofstream myfile3;
			myfile3.open("input_vertex_2.txt");
			for(int i=0;i<(80*60);i++){
				myfile3<<inputVertex[2][i].x<<" "<<inputVertex[2][i].y<<" "<<inputVertex[2][i].z<<std::endl;
	
			}
			std::cout<<"input write 3 success"<<std::endl;
			myfile3.close();





			//input normal
			//std::ofstream myfile1;
			myfile1.open("input_normal_0.txt");
			for(int i=0;i<(320*240);i++){
				myfile1<<inputNormal[0][i].x<<" "<<inputNormal[0][i].y<<" "<<inputNormal[0][i].z<<std::endl;
	
			}
			std::cout<<"input write 4 success"<<std::endl;
			myfile1.close();


			//std::ofstream myfile2;
			myfile2.open("input_normal_1.txt");
			for(int i=0;i<(160*120);i++){
				myfile2<<inputNormal[1][i].x<<" "<<inputNormal[1][i].y<<" "<<inputNormal[1][i].z<<std::endl;
	
			}
			std::cout<<"input write 5 success"<<std::endl;
			myfile2.close();


			//std::ofstream myfile3;
			myfile3.open("input_normal_2.txt");
			for(int i=0;i<(80*60);i++){
				myfile3<<inputNormal[2][i].x<<" "<<inputNormal[2][i].y<<" "<<inputNormal[2][i].z<<std::endl;
	
			}
			std::cout<<"input write 6 success"<<std::endl;
			myfile3.close();


			//normal
			myfile1.open("normal.txt");
			for(int i=0;i<(320*240);i++){
				myfile1<<normal[i].x<<" "<<normal[i].y<<" "<<normal[i].z<<std::endl;
	
			}
			std::cout<<"input write 7 success"<<std::endl;
			myfile1.close();


			//vertex
			myfile2.open("vertex.txt");
			for(int i=0;i<(320*240);i++){
				myfile2<<vertex[i].x<<" "<<vertex[i].y<<" "<<vertex[i].z<<std::endl;
	
			}
			std::cout<<"input write 7 success"<<std::endl;
			myfile2.close();

		}




		//data write from host to kernel memory


		//input vertex
		clError = clEnqueueWriteBuffer(commandQueue,ocl_inputVertex[0], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 0) * sizeof(float3), inputVertex[0], 0, NULL, &write_event[0]);
		checkErr(clError, "clEnqueueReadBuffer");


		//data write from host to kernel memory
		clError = clEnqueueWriteBuffer(commandQueue,ocl_inputVertex[1], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 1) * sizeof(float3), inputVertex[1], 0, NULL, &write_event[1]);
		checkErr(clError, "clEnqueueReadBuffer");


		//data write from host to kernel memory
		clError = clEnqueueWriteBuffer(commandQueue,ocl_inputVertex[2], CL_TRUE, 0,computationSize.x * computationSize.y / (int) pow(2, 2)* sizeof(float3), inputVertex[2], 0, NULL, &write_event[2]);
		checkErr(clError, "clEnqueueReadBuffer");




		
		//input normal
		clError = clEnqueueWriteBuffer(commandQueue,ocl_inputNormal[0], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 0) * sizeof(float3), inputNormal[0], 0, NULL, &write_event[3]);
		checkErr(clError, "clEnqueueReadBuffer");


		//data write from host to kernel memory
		clError = clEnqueueWriteBuffer(commandQueue,ocl_inputNormal[1], CL_TRUE, 0,computationSize.x * computationSize.y/ (int) pow(2, 1) * sizeof(float3), inputNormal[1], 0, NULL, &write_event[4]);
		checkErr(clError, "clEnqueueReadBuffer");


		//data write from host to kernel memory
		clError = clEnqueueWriteBuffer(commandQueue,ocl_inputNormal[2], CL_TRUE, 0,computationSize.x * computationSize.y / (int) pow(2, 2)* sizeof(float3), inputNormal[2], 0, NULL, &write_event[5]);
		checkErr(clError, "clEnqueueReadBuffer");

		//normal
		clError = clEnqueueWriteBuffer(commandQueue,ocl_normal, CL_TRUE, 0,computationSize.x * computationSize.y *sizeof(float3), normal, 0, NULL, &write_event[6]);
		checkErr(clError, "clEnqueueReadBuffer");

		//vertex
		clError = clEnqueueWriteBuffer(commandQueue,ocl_vertex, CL_TRUE, 0,computationSize.x * computationSize.y *sizeof(float3), vertex, 0, NULL, &write_event[7]);
		checkErr(clError, "clEnqueueReadBuffer");



	/////////////////


	//start of Track kernel
	oldPose = pose;

	//std::cout<<pose.data[0].x<<" "<<pose.data[0].y<<" "<<pose.data[0].z<<" "<<pose.data[0].w<<std::endl;
	//std::cout<<pose.data[1].x<<" "<<pose.data[1].y<<" "<<pose.data[1].z<<" "<<pose.data[1].w<<std::endl;
	//std::cout<<pose.data[2].x<<" "<<pose.data[2].y<<" "<<pose.data[2].z<<" "<<pose.data[2].w<<std::endl;
	//std::cout<<pose.data[3].x<<" "<<pose.data[3].y<<" "<<pose.data[3].z<<" "<<pose.data[3].w<<std::endl;

	//std::cout<<"kx:"<<k.x<<" ky:"<<k.y<<" kz:"<<k.z<<" kw:"<<k.w<<std::endl;

	const Matrix4 projectReference = getCameraMatrix(k) * inverse(raycastPose);



	
/*
	std::cout<<projectReference.data[0].x<<" "<<projectReference.data[0].y<<" "<<projectReference.data[0].z<<" "<<projectReference.data[0].w<<std::endl;
	std::cout<<projectReference.data[1].x<<" "<<projectReference.data[1].y<<" "<<projectReference.data[1].z<<" "<<projectReference.data[1].w<<std::endl;
	std::cout<<projectReference.data[2].x<<" "<<projectReference.data[2].y<<" "<<projectReference.data[2].z<<" "<<projectReference.data[2].w<<std::endl;
	std::cout<<projectReference.data[3].x<<" "<<projectReference.data[3].y<<" "<<projectReference.data[3].z<<" "<<projectReference.data[3].w<<std::endl;
*/



	//std::cout<<iterations[0]<<" "<<iterations[1]<<" "<<iterations[2]<<" "<<iterations.size()<<std::endl;

	int read_counter=-1;
	for (int level = iterations.size() - 1; level >= 0; --level) {
		uint2 localimagesize = make_uint2(
				computationSize.x / (int) pow(2, level),
				computationSize.y / (int) pow(2, level));
		for (int i = 0; i < iterations[level]; ++i) {

			int arg = 0;
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_trackingResult);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_uint2),
					&computationSize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_inputVertex[level]);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_uint2),
					&localimagesize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_inputNormal[level]);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_uint2),
					&localimagesize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_vertex);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_uint2),
					&computationSize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_normal);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_uint2),
					&computationSize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(Matrix4),
					&pose);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(Matrix4),
					&projectReference);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_float),
					&dist_threshold);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(track_ocl_kernel, arg++, sizeof(cl_float),
					&normal_threshold);
			checkErr(clError, "clSetKernelArg");

			size_t globalWorksize[2] = { localimagesize.x, localimagesize.y };

			clError = clEnqueueNDRangeKernel(commandQueue, track_ocl_kernel, 2,
					NULL, globalWorksize, NULL, 0, NULL, &kernel_event[++read_counter]);
			checkErr(clError, "clEnqueueNDRangeKernel");

			checkErr(clError, "clCreateBuffer output");

			////////////	read data back from kernel 
					clError = clEnqueueReadBuffer(commandQueue,ocl_trackingResult, CL_TRUE, 0,computationSize.x * computationSize.y * sizeof(TrackData), trackingResult, 0, NULL, &finish_event[read_counter]);
					checkErr(clError, "clEnqueueReadBuffer");


			//std::cout<<trackingResult[0].result<<" "<<trackingResult[0].error<<" "<<trackingResult[0].J[0]<<std::endl;


			//std::cout<<i<<std::endl;		
			//std::cout<<read_counter<<std::endl;

			///////////////

			arg = 0;
			clError = clSetKernelArg(reduce_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_reduce_output_buffer);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(reduce_ocl_kernel, arg++, sizeof(cl_mem),
					&ocl_trackingResult);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(reduce_ocl_kernel, arg++, sizeof(cl_uint2),
					&computationSize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(reduce_ocl_kernel, arg++, sizeof(cl_uint2),
					&localimagesize);
			checkErr(clError, "clSetKernelArg");
			clError = clSetKernelArg(reduce_ocl_kernel, arg++,
					size_of_group * 32 * sizeof(float), NULL);
			checkErr(clError, "clSetKernelArg");

			size_t RglobalWorksize[1] = { size_of_group * number_of_groups };
			size_t RlocalWorksize[1] = { size_of_group }; // Dont change it !

			clError = clEnqueueNDRangeKernel(commandQueue, reduce_ocl_kernel, 1,
					NULL, RglobalWorksize, RlocalWorksize, 0, NULL, NULL);
			checkErr(clError, "clEnqueueNDRangeKernel");

			clError = clEnqueueReadBuffer(commandQueue,
					ocl_reduce_output_buffer, CL_TRUE, 0,
					32 * number_of_groups * sizeof(float), reduceOutputBuffer, 0,
					NULL, NULL);
			checkErr(clError, "clEnqueueReadBuffer");

			TooN::Matrix<TooN::Dynamic, TooN::Dynamic, float,
					TooN::Reference::RowMajor> values(reduceOutputBuffer,
					number_of_groups, 32);

			for (int j = 1; j < number_of_groups; ++j) {
				values[0] += values[j];
			}

			if (updatePoseKernel(pose, reduceOutputBuffer, icp_threshold))
				break;

		}
	}






///////////// should be done after the for loop
	//timings
	
	std::cerr << "First Write to kernel time:" << computeEventDuration(&write_event[0]) << std::endl;
	std::cerr << "Second Write to kernel time:" << computeEventDuration(&write_event[1]) << std::endl;
	std::cerr << "Third Write to kernel time:" << computeEventDuration(&write_event[2]) << std::endl;
	std::cerr << "fourth Write to kernel time:" << computeEventDuration(&write_event[3]) << std::endl;
	std::cerr << "fifth Write to kernel time:" << computeEventDuration(&write_event[4]) << std::endl;
	std::cerr << "sixth Write to kernel time:" << computeEventDuration(&write_event[5]) << std::endl;
	std::cerr << "Seventh Write to kernel time:" << computeEventDuration(&write_event[6]) << std::endl;
	std::cerr << "Eighth Write to kernel time:" << computeEventDuration(&write_event[7]) << std::endl;

	//std::cerr <<"average writing time is"<<std::endl; 

	//std::cout<<"read counter"<<read_counter<<std::endl;
	for (int j=0;j<=read_counter;j++){
		std::cerr <<j+1<<"th  Kernel time:" << computeEventDuration(&kernel_event[j]) << std::endl;
	}

	for (int j=0;j<=read_counter;j++){
		std::cerr <<j+1<<"th Write to host time:" << computeEventDuration(&finish_event[j]) << std::endl;
	}



	//writing output file
	if(frame==4){
			
//writing to a text file
		std::ofstream myfile;
		myfile.open("tracking_result.txt");
	for(int i=0;i<(320* 240);i++){
		myfile<<trackingResult[i].result<<" "<<trackingResult[i].error<<" "<<trackingResult[i].J[0]<<" "<<trackingResult[i].J[1]<<" "<<trackingResult[i].J[2]<<" "<<trackingResult[i].J[3]<<" "<<trackingResult[i].J[4]<<" "<<trackingResult[i].J[5]<<std::endl;
	}

	std::cout<<"output write 1 success"<<std::endl;
	myfile.close();


	}


//////////////










	return checkPoseKernel(pose, oldPose, reduceOutputBuffer, computationSize,
			track_threshold);

}

bool Kfusion::raycasting(float4 k, float mu, uint frame) {

	bool doRaycast = false;
	float largestep = mu * 0.75f;

	if (frame > 2) {

		checkErr(clError, "clEnqueueNDRangeKernel");
		raycastPose = pose;
		const Matrix4 view = raycastPose * getInverseCameraMatrix(k);

		// set param and run kernel
		clError = clSetKernelArg(raycast_ocl_kernel, 0, sizeof(cl_mem),
				(void*) &ocl_vertex);
		checkErr(clError, "clSetKernelArg0");
		clError = clSetKernelArg(raycast_ocl_kernel, 1, sizeof(cl_mem),
				(void*) &ocl_normal);
		checkErr(clError, "clSetKernelArg1");
		clError = clSetKernelArg(raycast_ocl_kernel, 2, sizeof(cl_mem),
				(void*) &ocl_volume_data);
		checkErr(clError, "clSetKernelArg2");
		clError = clSetKernelArg(raycast_ocl_kernel, 3, sizeof(cl_uint3),
				(void*) &volumeResolution);
		checkErr(clError, "clSetKernelArg3");
		clError = clSetKernelArg(raycast_ocl_kernel, 4, sizeof(cl_float3),
				(void*) &volumeDimensions);
		checkErr(clError, "clSetKernelArg4");
		clError = clSetKernelArg(raycast_ocl_kernel, 5, sizeof(Matrix4),
				(void*) &view);
		checkErr(clError, "clSetKernelArg5");
		clError = clSetKernelArg(raycast_ocl_kernel, 6, sizeof(cl_float),
				(void*) &nearPlane);
		checkErr(clError, "clSetKernelArg6");
		clError = clSetKernelArg(raycast_ocl_kernel, 7, sizeof(cl_float),
				(void*) &farPlane);
		checkErr(clError, "clSetKernelArg7");
		clError = clSetKernelArg(raycast_ocl_kernel, 8, sizeof(cl_float),
				(void*) &step);
		checkErr(clError, "clSetKernelArg8");
		clError = clSetKernelArg(raycast_ocl_kernel, 9, sizeof(cl_float),
				(void*) &largestep);
		checkErr(clError, "clSetKernelArg9");

		size_t RaycastglobalWorksize[2] =
				{ computationSize.x, computationSize.y };

		clError = clEnqueueNDRangeKernel(commandQueue, raycast_ocl_kernel, 2,
				NULL, RaycastglobalWorksize, NULL, 0, NULL, NULL);
		checkErr(clError, "clEnqueueNDRangeKernel");

	}

	return doRaycast;

}

bool Kfusion::integration(float4 k, uint integration_rate, float mu,
		uint frame) {

	bool doIntegrate = checkPoseKernel(pose, oldPose, reduceOutputBuffer,
			computationSize, track_threshold);

	if ((doIntegrate && ((frame % integration_rate) == 0)) || (frame <= 3)) {
		doIntegrate = true;
		// integrate(integration, ScaledDepth[0],inputSize, inverse(pose), getCameraMatrix(k), mu, maxweight );

		uint2 depthSize = computationSize;
		const Matrix4 invTrack = inverse(pose);
		const Matrix4 K = getCameraMatrix(k);

		//uint3 pix = make_uint3(thr2pos2());
		const float3 delta = rotate(invTrack,
				make_float3(0, 0, volumeDimensions.z / volumeResolution.z));
		const float3 cameraDelta = rotate(K, delta);

		// set param and run kernel
		int arg = 0;
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_mem),
				(void*) &ocl_volume_data);
		checkErr(clError, "clSetKernelArg1");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_uint3),
				(void*) &volumeResolution);
		checkErr(clError, "clSetKernelArg2");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_float3),
				(void*) &volumeDimensions);
		checkErr(clError, "clSetKernelArg3");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_mem),
				(void*) &ocl_FloatDepth);
		checkErr(clError, "clSetKernelArg4");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_uint2),
				(void*) &depthSize);
		checkErr(clError, "clSetKernelArg5");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(Matrix4),
				(void*) &invTrack);
		checkErr(clError, "clSetKernelArg6");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(Matrix4),
				(void*) &K);
		checkErr(clError, "clSetKernelArg7");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_float),
				(void*) &mu);
		checkErr(clError, "clSetKernelArg8");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_float),
				(void*) &maxweight);
		checkErr(clError, "clSetKernelArg9");

		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_float3),
				(void*) &delta);
		checkErr(clError, "clSetKernelArg10");
		clError = clSetKernelArg(integrate_ocl_kernel, arg++, sizeof(cl_float3),
				(void*) &cameraDelta);
		checkErr(clError, "clSetKernelArg11");

		size_t globalWorksize[2] = { volumeResolution.x, volumeResolution.y };

		clError = clEnqueueNDRangeKernel(commandQueue, integrate_ocl_kernel, 2,
				NULL, globalWorksize, NULL, 0, NULL, NULL);

	} else {
		doIntegrate = false;
	}

	return doIntegrate;
}

void Kfusion::computeFrame(const ushort * inputDepth, const uint2 inputSize,
			 float4 k, uint integration_rate, uint tracking_rate,
			 float icp_threshold, float mu, const uint frame) {
  preprocessing(inputDepth, inputSize);
  _tracked = tracking(k, icp_threshold, tracking_rate, frame);
  _integrated = integration(k, integration_rate, mu, frame);
  raycasting(k, mu, frame);
}


void synchroniseDevices() {
	clFinish(commandQueue);
}