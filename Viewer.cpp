/*****************************************************************************
*                                                                            *
*  OpenNI 2.x Alpha                                                          *
*  Copyright (C) 2012 PrimeSense Ltd.                                        *
*                                                                            *
*  This file is part of OpenNI.                                              *
*                                                                            *
*  Licensed under the Apache License, Version 2.0 (the "License");           *
*  you may not use this file except in compliance with the License.          *
*  You may obtain a copy of the License at                                   *
*                                                                            *
*      http://www.apache.org/licenses/LICENSE-2.0                            *
*                                                                            *
*  Unless required by applicable law or agreed to in writing, software       *
*  distributed under the License is distributed on an "AS IS" BASIS,         *
*  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  *
*  See the License for the specific language governing permissions and       *
*  limitations under the License.                                            *
*                                                                            *
*****************************************************************************/
// Undeprecate CRT functions
#ifndef _CRT_SECURE_NO_DEPRECATE 
	#define _CRT_SECURE_NO_DEPRECATE 1
#endif

#include "Viewer.h"

#if (ONI_PLATFORM == ONI_PLATFORM_MACOSX)
		#include <GLUT/glut.h>
#else
		#include <GL/glut.h>
#endif

#include <opencv2/opencv.hpp>
#include "OniSampleUtilities.h"

using namespace cv;
using namespace std;

#define GL_WIN_SIZE_X	1280
#define GL_WIN_SIZE_Y	1024
#define TEXTURE_SIZE	512

SampleViewer* SampleViewer::ms_self = NULL;

SampleViewer::SampleViewer(const char* strSampleName, openni::Device& device, openni::VideoStream& depth, openni::VideoStream& color) :
	m_device(device), m_depthStream(depth), m_colorStream(color), m_streams(NULL), m_pTexMap(NULL), send_en(0)

{
	ms_self = this;
	strncpy(m_strSampleName, strSampleName, ONI_MAX_STR);
}
SampleViewer::~SampleViewer()
{
	delete[] m_pTexMap;

	ms_self = NULL;

	if (m_streams != NULL)
	{
		delete []m_streams;
	}
}

// initialize the network part
void SampleViewer::init_net()
{

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0)	{
		printf("Socket creation error \n");
		exit(-1);
	}

	memset(&serv_addr, '0', sizeof(serv_addr));
  
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(9003);

	// Convert IPv4 and IPv6 addresses from text to binary form
	if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0) 
	{
		printf("\nInvalid address/ Address not supported \n");
		exit(-1);
	}

	while (true) {
		if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
		{
			printf("\nConnection Failed \n");
			usleep(1000000);
		} else {
			break;
		}
	}
}

openni::Status SampleViewer::init(int argc, char **argv)
{
	openni::VideoMode depthVideoMode;
	openni::VideoMode colorVideoMode;

	m_depthStream.setMirroringEnabled(0);
	m_colorStream.setMirroringEnabled(0);

	if (m_depthStream.isValid() && m_colorStream.isValid())
	{
		depthVideoMode = m_depthStream.getVideoMode();
		colorVideoMode = m_colorStream.getVideoMode();

		int depthWidth = depthVideoMode.getResolutionX();
		int depthHeight = depthVideoMode.getResolutionY();
		int colorWidth = colorVideoMode.getResolutionX();
		int colorHeight = colorVideoMode.getResolutionY();

		if (depthWidth == colorWidth &&
			depthHeight == colorHeight)
		{
			m_width = depthWidth;
			m_height = depthHeight;
		}
		else
		{
			printf("Error - expect color and depth to be in same resolution: D: %dx%d, C: %dx%d\n",
				depthWidth, depthHeight,
				colorWidth, colorHeight);
			return openni::STATUS_ERROR;
		}
	}
	else if (m_depthStream.isValid())
	{
		depthVideoMode = m_depthStream.getVideoMode();
		m_width = depthVideoMode.getResolutionX();
		m_height = depthVideoMode.getResolutionY();
	}
	else if (m_colorStream.isValid())
	{
		colorVideoMode = m_colorStream.getVideoMode();
		m_width = colorVideoMode.getResolutionX();
		m_height = colorVideoMode.getResolutionY();
	}
	else
	{
		printf("Error - expects at least one of the streams to be valid...\n");
		return openni::STATUS_ERROR;
	}

	m_streams = new openni::VideoStream*[2];
	m_streams[0] = &m_depthStream;
	m_streams[1] = &m_colorStream;

	init_net();

	return openni::STATUS_OK;

}
openni::Status SampleViewer::run()	//Does not return
{
	while (true)
	{
		getData();
	}

	return openni::STATUS_OK;
}
void SampleViewer::getData()
{
	int changedIndex;
	openni::Status rc = openni::OpenNI::waitForAnyStream(m_streams, 2, &changedIndex);
	if (rc != openni::STATUS_OK)
	{
		printf("Wait failed\n");
		return;
	}

	// cout << "frame type: " << changedIndex << endl;

	switch (changedIndex)
	{
	case 0:
		m_depthStream.readFrame(&m_depthFrame); break;
	case 1:
		m_colorStream.readFrame(&m_colorFrame); break;
	default:
		printf("Error in wait\n");
	}

	if (changedIndex == 0)
	{
		// send the depth image
		const openni::DepthPixel* pDepthRow = (const openni::DepthPixel*)m_depthFrame.getData();
		int rowSize = m_depthFrame.getStrideInBytes() / sizeof(openni::DepthPixel);

		// send integer 0 to indicate a following depth frame
		send(sock, &changedIndex, 4, 0);

		int line_num = 1;
		for (int i = 0; i < m_height / line_num; i++) {
			send(sock, pDepthRow + i * line_num * rowSize, m_width * line_num * 2, 0);
			// send(sock, pDepthRow + i * line_num * 480, 640 * line_num * 2, 0);
		}
		// cout << "Send One Depth Frame" << endl;
	}

	if (changedIndex == 1)
	{
		// send the color image
		const openni::RGB888Pixel* pImageRow = (const openni::RGB888Pixel*)m_colorFrame.getData();

		// send integer 1 to indicate a following color frame
		send(sock, &changedIndex, 4, 0);

		Mat mat(480, 640, CV_8UC4);
		for (int i = 0; i < 480; i++) {
			for(int j = 0; j < 640; j++)
			{
				Vec4b &rgba = mat.at<Vec4b>(i, j);
				rgba[3] = UCHAR_MAX;
				rgba[0] = saturate_cast<uchar>(pImageRow->b);
				rgba[1] = saturate_cast<uchar>(pImageRow->g);
				rgba[2] = saturate_cast<uchar>(pImageRow->r);
				pImageRow += 1;
			}
		}
		// encode the matrix to jpg format and send
		std::vector<uchar> buffer;
		imencode(".jpg", mat, buffer);
		int img_size = buffer.size();
		// send buffer size
		send(sock, &img_size, 4, 0);
		int cur_loc = 0;
		// cout << img_size << endl;
		while (cur_loc < img_size) {
			int send_size = img_size - cur_loc > 1000 ? 1000 : img_size - cur_loc;
			send(sock, &(buffer[cur_loc]), send_size, 0);
			cur_loc += send_size;
		}
		// cout << "Send One Color Frame" << endl;
	}
}
