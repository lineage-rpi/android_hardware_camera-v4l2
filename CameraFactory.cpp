/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Contains implementation of a class CameraFactory that manages cameras
 * available
 */

#define LOG_NDEBUG 0
#define CONFIG_FILE "/etc/camera.cfg"
#define LOG_TAG "Camera_Factory"

#include <cutils/log.h>
#include <cutils/properties.h>
#include "CameraFactory.h"

extern camera_module_t HAL_MODULE_INFO_SYM;

/* A global instance of CameraFactory is statically instantiated and
 * initialized when camera HAL is loaded.
 */
android::CameraFactory  gCameraFactory;

namespace android {

CameraFactory::CameraFactory()
{
    ALOGD("CameraFactory::CameraFactory");
    mCamera = NULL;
    mCameraDevices = NULL;
    mCameraFacing = NULL;
    mCameraOrientation = NULL;
    parseConfig(CONFIG_FILE);
}

CameraFactory::~CameraFactory()
{
    ALOGD("CameraFactory::~CameraFactory");
    for (int i=0; i < getCameraNum(); i++) {
        delete mCamera[i];
        free(mCameraDevices[i]);
    }
    free(mCamera);
    free(mCameraDevices);
    free(mCameraFacing);
    free(mCameraOrientation);
}

/****************************************************************************
 * Camera HAL API handlers.
 *
 * Each handler simply verifies existence of an appropriate Camera
 * instance, and dispatches the call to that instance.
 *
 ***************************************************************************/

int CameraFactory::cameraDeviceOpen(const hw_module_t* module,int camera_id, hw_device_t** device)
{
    ALOGD("CameraFactory::cameraDeviceOpen: id = %d", camera_id);

    *device = NULL;

    if (!mCamera || camera_id < 0 || camera_id >= getCameraNum()) {
        ALOGE("%s: Camera id %d is out of bounds (%d)",
             __FUNCTION__, camera_id, getCameraNum());
        return -EINVAL;
    }

    if (!mCamera[camera_id]) {
        mCamera[camera_id] = new CameraHardware(module, mCameraDevices[camera_id]);
    }
    return mCamera[camera_id]->connectCamera(device);
}

/* Returns the number of available cameras */
int CameraFactory::getCameraNum()
{
    ALOGD("CameraFactory::getCameraNum: %d", mCameraNum);
    return mCameraNum;
}

int CameraFactory::getCameraInfo(int camera_id, struct camera_info* info)
{
    ALOGD("CameraFactory::getCameraInfo: id = %d, info = %p", camera_id, info);

    if (camera_id < 0 || camera_id >= getCameraNum()) {
        ALOGE("%s: Camera id %d is out of bounds (%d)",
                __FUNCTION__, camera_id, getCameraNum());
        return -EINVAL;
    }

    return CameraHardware::getCameraInfo(info, mCameraFacing[camera_id],
                                         mCameraOrientation[camera_id]);
}

// Parse a simple configuration file
void CameraFactory::parseConfig(const char* configFile)
{
    ALOGD("CameraFactory::parseConfig: configFile = %s", configFile);

    FILE* config = fopen(configFile, "r");
    if (config != NULL) {
        char line[128];
        char arg1[128];
        char arg2[128];
        int  arg3;

        while (fgets(line, sizeof line, config) != NULL) {
            int lineStart = strspn(line, " \t\n\v" );

            if (line[lineStart] == '#')
                continue;

            sscanf(line, "%s %s %d", arg1, arg2, &arg3);
            if (arg3 != 0 && arg3 != 90 && arg3 != 180 && arg3 != 270)
                arg3 = 0;

            if (strcmp(arg1, "front") == 0) {
                newCameraConfig(CAMERA_FACING_FRONT, arg2, arg3);
            } else if (strcmp(arg1, "back") == 0) {
                newCameraConfig(CAMERA_FACING_BACK, arg2, arg3);
            } else {
                ALOGD("CameraFactory::parseConfig: Unrecognized config line '%s'", line);
            }
        }
    } else {
        ALOGD("%s not found, using camera configuration defaults", CONFIG_FILE);
        char camera_node[] = "/dev/video0";
        char camera_prop[] = "hal.camera.0";
        char prop[PROPERTY_VALUE_MAX] = "";
        bool no_prop = true;
        while (camera_node[10] <= '9' && mCameraNum < 3) {
            if (!access(camera_node, F_OK)) {
                int facing = mCameraNum, orientation = 0;
                if (property_get(camera_prop, prop, "")) {
                    no_prop = false;
                    sscanf(prop, "%d,%d", &facing, &orientation);
                    ALOGI("%s got facing=%d orient=%d from property %s", __FUNCTION__, facing, orientation, camera_prop);
                }
                newCameraConfig(facing, camera_node, orientation);
            }
            camera_node[10]++, camera_prop[11]++;
        }

        // If there is only one camera, assume its facing is front
        /*
        if (mCameraNum == 1 && no_prop) {
            mCameraFacing[0] = CAMERA_FACING_FRONT;
            ALOGI("%s assume %s is front", __FUNCTION__, mCameraDevices[0]);
        }
        */
    }
}

// Although realloc could be a costly operation, we only execute this function usually 2 times
void CameraFactory::newCameraConfig(int facing, const char* location, int orientation)
{
    V4L2Camera camera;
    if (camera.Open(location) || !camera.getBestPreviewFmt().getFps()) {
        ALOGW("ignore invalid camera: %s", location);
        return;
    }

    // Keep track of cameras
    mCameraNum++;

    // Grow the information arrays
    mCamera = (CameraHardware**) realloc(mCamera, mCameraNum * sizeof(CameraHardware*));
    mCameraDevices = (char**) realloc(mCameraDevices, mCameraNum * sizeof(char*));
    mCameraFacing = (int*) realloc(mCameraFacing, mCameraNum * sizeof(int));
    mCameraOrientation = (int*) realloc(mCameraOrientation, mCameraNum * sizeof(int));

    // Store the values for each camera_id
    mCamera[mCameraNum - 1] = NULL;
    mCameraDevices[mCameraNum - 1] = strdup(location);
    mCameraFacing[mCameraNum - 1] = facing;
    mCameraOrientation[mCameraNum - 1] = orientation;
    ALOGD("CameraFactory::newCameraConfig: %d -> %s (%d)",
          mCameraFacing[mCameraNum - 1], mCameraDevices[mCameraNum - 1],
          mCameraOrientation[mCameraNum - 1]);
}

/****************************************************************************
 * Camera HAL API callbacks.
 ***************************************************************************/

int CameraFactory::device_open(const hw_module_t* module,
                                       const char* name,
                                       hw_device_t** device)
{
    ALOGD("CameraFactory::device_open: name = %s", name);

    /*
     * Simply verify the parameters, and dispatch the call inside the
     * CameraFactory instance.
     */

    if (module != &HAL_MODULE_INFO_SYM.common) {
        ALOGE("%s: Invalid module %p expected %p",
                __FUNCTION__, module, &HAL_MODULE_INFO_SYM.common);
        return -EINVAL;
    }
    if (name == NULL) {
        ALOGE("%s: NULL name is not expected here", __FUNCTION__);
        return -EINVAL;
    }

    int camera_id = atoi(name);
    return gCameraFactory.cameraDeviceOpen(module, camera_id, device);
}

int CameraFactory::get_number_of_cameras(void)
{
    ALOGD("CameraFactory::get_number_of_cameras");
    return gCameraFactory.getCameraNum();
}

int CameraFactory::get_camera_info(int camera_id,
                                           struct camera_info* info)
{
    ALOGD("CameraFactory::get_camera_info");
    return gCameraFactory.getCameraInfo(camera_id, info);
}

/********************************************************************************
 * Initializer for the static member structure.
 *******************************************************************************/

/* Entry point for camera HAL API. */
struct hw_module_methods_t CameraFactory::mCameraModuleMethods = {
    .open = CameraFactory::device_open
};

}; /* namespace android */
