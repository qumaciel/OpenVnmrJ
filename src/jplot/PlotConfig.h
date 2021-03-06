/*
 * Copyright (C) 2015  University of Oregon
 *
 * You may distribute under the terms of either the GNU General Public
 * License or the Apache License, as specified in the LICENSE file.
 *
 * For more information, see the LICENSE file.
 */
/*
 */
/* DO NOT EDIT THIS FILE - it is machine generated */
#include <jni.h>
/* Header for class PlotConfig */

#ifndef _Included_PlotConfig
#define _Included_PlotConfig
#ifdef __cplusplus
extern "C" {
#endif
/*
 * Class:     PlotConfig
 * Method:    makeImage
 * Signature: (LPlotConfig$PlPanel;III)I
 */
JNIEXPORT jint JNICALL Java_PlotConfig_makeImage
  (JNIEnv *, jclass, jobject, jint, jint, jint);

/*
 * Class:     PlotConfig
 * Method:    accessFile
 * Signature: (Ljava/lang/String;I)I
 */
JNIEXPORT jint JNICALL Java_PlotConfig_accessFile
  (JNIEnv *, jclass, jstring, jint);

/*
 * Class:     PlotConfig
 * Method:    loadImage
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_PlotConfig_loadImage
  (JNIEnv *, jclass);

/*
 * Class:     PlotConfig
 * Method:    getEnv
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_PlotConfig_getEnv
  (JNIEnv *, jclass, jstring);

/*
 * Class:     PlotConfig
 * Method:    talk_to_vnmr
 * Signature: (Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;II)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_PlotConfig_talk_1to_1vnmr
  (JNIEnv *, jclass, jstring, jstring, jstring, jint, jint);

/*
 * Class:     PlotConfig
 * Method:    get_tmp_name
 * Signature: (Ljava/lang/String;)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_PlotConfig_get_1tmp_1name
  (JNIEnv *, jclass, jstring);

/*
 * Class:     PlotConfig
 * Method:    initEnv
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_PlotConfig_initEnv
  (JNIEnv *, jclass);

/*
 * Class:     PlotConfig
 * Method:    initArg
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_PlotConfig_initArg
  (JNIEnv *, jclass, jstring);

/*
 * Class:     PlotConfig
 * Method:    remove_file
 * Signature: (Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_PlotConfig_remove_1file
  (JNIEnv *, jclass, jstring);

#ifdef __cplusplus
}
#endif
#endif
