/*
  ==============================================================================

   This file is part of the JUCE library - "Jules' Utility Class Extensions"
   Copyright 2004-10 by Raw Material Software Ltd.

  ------------------------------------------------------------------------------

   JUCE can be redistributed and/or modified under the terms of the GNU General
   Public License (Version 2), as published by the Free Software Foundation.
   A copy of the license is included in the JUCE distribution, or can be found
   online at www.gnu.org/licenses.

   JUCE is distributed in the hope that it will be useful, but WITHOUT ANY
   WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
   A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

  ------------------------------------------------------------------------------

   To release a closed-source product which uses JUCE, commercial licenses are
   available: visit www.rawmaterialsoftware.com/juce for more information.

  ==============================================================================
*/

/*
    This file wraps together all the android-specific code, so that
    we can include all the native headers just once, and compile all our
    platform-specific stuff in one big lump, keeping it out of the way of
    the rest of the codebase.
*/

#include "../../core/juce_TargetPlatform.h"

#if JUCE_ANDROID

#undef JUCE_BUILD_NATIVE
#define JUCE_BUILD_NATIVE 1

#include "juce_android_NativeIncludes.h"
#include "../../core/juce_StandardHeader.h"

BEGIN_JUCE_NAMESPACE

//==============================================================================
#include "../../core/juce_Singleton.h"
#include "../../maths/juce_Random.h"
#include "../../core/juce_SystemStats.h"
#include "../../threads/juce_Process.h"
#include "../../threads/juce_Thread.h"
#include "../../threads/juce_InterProcessLock.h"
#include "../../io/files/juce_FileInputStream.h"
#include "../../io/files/juce_FileOutputStream.h"
#include "../../io/files/juce_NamedPipe.h"
#include "../../io/files/juce_DirectoryIterator.h"
#include "../../io/network/juce_URL.h"
#include "../../io/network/juce_MACAddress.h"
#include "../../core/juce_PlatformUtilities.h"
#include "../../text/juce_LocalisedStrings.h"
#include "../../utilities/juce_DeletedAtShutdown.h"
#include "../../application/juce_Application.h"
#include "../../utilities/juce_SystemClipboard.h"
#include "../../events/juce_MessageManager.h"
#include "../../gui/graphics/contexts/juce_LowLevelGraphicsSoftwareRenderer.h"
#include "../../gui/graphics/imaging/juce_ImageFileFormat.h"
#include "../../gui/graphics/imaging/juce_CameraDevice.h"
#include "../../gui/components/windows/juce_ComponentPeer.h"
#include "../../gui/components/windows/juce_AlertWindow.h"
#include "../../gui/components/juce_Desktop.h"
#include "../../gui/components/menus/juce_MenuBarModel.h"
#include "../../gui/components/special/juce_OpenGLComponent.h"
#include "../../gui/components/special/juce_QuickTimeMovieComponent.h"
#include "../../gui/components/mouse/juce_DragAndDropContainer.h"
#include "../../gui/components/mouse/juce_MouseInputSource.h"
#include "../../gui/components/keyboard/juce_KeyPressMappingSet.h"
#include "../../gui/components/layout/juce_ComponentMovementWatcher.h"
#include "../../gui/components/special/juce_ActiveXControlComponent.h"
#include "../../gui/components/special/juce_WebBrowserComponent.h"
#include "../../gui/components/special/juce_DropShadower.h"
#include "../../gui/components/special/juce_SystemTrayIconComponent.h"
#include "../../gui/components/filebrowser/juce_FileChooser.h"
#include "../../gui/components/lookandfeel/juce_LookAndFeel.h"
#include "../../audio/audio_file_formats/juce_AudioCDBurner.h"
#include "../../audio/audio_file_formats/juce_AudioCDReader.h"
#include "../../audio/audio_sources/juce_AudioSource.h"
#include "../../audio/dsp/juce_AudioDataConverters.h"
#include "../../audio/devices/juce_AudioIODeviceType.h"
#include "../../audio/devices/juce_MidiOutput.h"
#include "../../audio/devices/juce_MidiInput.h"
#include "../../containers/juce_ScopedValueSetter.h"
#include "../common/juce_MidiDataConcatenator.h"

//==============================================================================
#define JUCE_JNI_CALLBACK(className, methodName, returnType, params) \
  extern "C" __attribute__ ((visibility("default"))) returnType Java_com_juce_ ## className ## _ ## methodName params

//==============================================================================
#define JUCE_JNI_CLASSES(JAVACLASS) \
 JAVACLASS (activityClass, "com/juce/JuceAppActivity") \
 JAVACLASS (componentPeerViewClass, "com/juce/ComponentPeerView") \
 JAVACLASS (fileClass, "java/io/File") \
 JAVACLASS (contextClass, "android/content/Context") \
 JAVACLASS (canvasClass, "android/graphics/Canvas") \
 JAVACLASS (paintClass, "android/graphics/Paint") \


//==============================================================================
#define JUCE_JNI_METHODS(METHOD, STATICMETHOD) \
\
 STATICMETHOD (activityClass, printToConsole, "printToConsole", "(Ljava/lang/String;)V") \
 METHOD (activityClass, createNewView, "createNewView", "()Lcom/juce/ComponentPeerView;") \
 METHOD (activityClass, deleteView, "deleteView", "(Lcom/juce/ComponentPeerView;)V") \
\
 METHOD (fileClass, fileExists, "exists", "()Z") \
\
 METHOD (componentPeerViewClass, layout, "layout", "(IIII)V") \
\
 METHOD (canvasClass, drawRect, "drawRect", "(FFFFLandroid/graphics/Paint;)V") \
\
 METHOD (paintClass, paintClassConstructor, "<init>", "()V") \
 METHOD (paintClass, setColor, "setColor", "(I)V") \


//==============================================================================
class GlobalRef
{
public:
    GlobalRef()
        : env (0), obj (0)
    {
    }

    GlobalRef (JNIEnv* const env_, jobject obj_)
        : env (env_),
          obj (retain (env_, obj_))
    {
    }

    GlobalRef (const GlobalRef& other)
        : env (other.env),
          obj (retain (other.env, other.obj))
    {
    }

    ~GlobalRef()
    {
        release();
    }

    GlobalRef& operator= (const GlobalRef& other)
    {
        release();
        env = other.env;
        obj = retain (env, other.obj);
        return *this;
    }

    GlobalRef& operator= (jobject newObj)
    {
        jassert (env != 0 || newObj == 0);

        if (newObj != obj && env != 0)
        {
            release();
            obj = retain (env, newObj);
        }
    }

    inline operator jobject() const throw()     { return obj; }
    inline jobject get() const throw()          { return obj; }

    inline JNIEnv* getEnv() const throw()       { return env; }

    #define DECLARE_CALL_TYPE_METHOD(returnType, typeName) \
        returnType call##typeName##Method (jmethodID methodID, ... ) \
        { \
            returnType result; \
            va_list args; \
            va_start (args, methodID); \
            result = env->Call##typeName##MethodV (obj, methodID, args); \
            va_end (args); \
            return result; \
        }

    DECLARE_CALL_TYPE_METHOD (jobject, Object)
    DECLARE_CALL_TYPE_METHOD (jboolean, Boolean)
    DECLARE_CALL_TYPE_METHOD (jbyte, Byte)
    DECLARE_CALL_TYPE_METHOD (jchar, Char)
    DECLARE_CALL_TYPE_METHOD (jshort, Short)
    DECLARE_CALL_TYPE_METHOD (jint, Int)
    DECLARE_CALL_TYPE_METHOD (jlong, Long)
    DECLARE_CALL_TYPE_METHOD (jfloat, Float)
    DECLARE_CALL_TYPE_METHOD (jdouble, Double)
    #undef DECLARE_CALL_TYPE_METHOD

    void callVoidMethod (jmethodID methodID, ... )
    {
        va_list args;
        va_start (args, methodID);
        env->CallVoidMethodV (obj, methodID, args);
        va_end (args);
    }

private:
    JNIEnv* env;
    jobject obj;

    void release()
    {
        if (env != 0)
            env->DeleteGlobalRef (obj);
    }

    static jobject retain (JNIEnv* const env, jobject obj_)
    {
        return env == 0 ? 0 : env->NewGlobalRef (obj_);
    }
};

//==============================================================================
class AndroidJavaCallbacks
{
public:
    AndroidJavaCallbacks() : env (0)
    {
    }

    void initialise (JNIEnv* env_, jobject activity_)
    {
        env = env_;
        activity = GlobalRef (env, activity_);

        #define CREATE_JNI_CLASS(className, path) \
            className = (jclass) env->NewGlobalRef (env->FindClass (path)); \
            jassert (className != 0);
        JUCE_JNI_CLASSES (CREATE_JNI_CLASS);
        #undef CREATE_JNI_CLASS

        #define CREATE_JNI_METHOD(ownerClass, methodID, stringName, params) \
            methodID = env->GetMethodID (ownerClass, stringName, params); \
            jassert (methodID != 0);
        #define CREATE_JNI_STATICMETHOD(ownerClass, methodID, stringName, params) \
            methodID = env->GetStaticMethodID (ownerClass, stringName, params); \
            jassert (methodID != 0);
        JUCE_JNI_METHODS (CREATE_JNI_METHOD, CREATE_JNI_STATICMETHOD);
        #undef CREATE_JNI_METHOD
    }

    void shutdown()
    {
        if (env != 0)
        {
            #define RELEASE_JNI_CLASS(className, path)    env->DeleteGlobalRef (className);
            JUCE_JNI_CLASSES (RELEASE_JNI_CLASS);
            #undef RELEASE_JNI_CLASS

            activity = 0;
            env = 0;
        }
    }

    //==============================================================================
    const String juceString (jstring s) const
    {
        jboolean isCopy;
        const char* const utf8 = env->GetStringUTFChars (s, &isCopy);
        CharPointer_UTF8 utf8CP (utf8);
        const String result (utf8CP);
        env->ReleaseStringUTFChars (s, utf8);
        return result;
    }

    jstring javaString (const String& s) const
    {
        return env->NewStringUTF (s.toUTF8());
    }

    //==============================================================================
    JNIEnv* env;
    GlobalRef activity;

    //==============================================================================
    #define DECLARE_JNI_CLASS(className, path) jclass className;
    JUCE_JNI_CLASSES (DECLARE_JNI_CLASS);
    #undef DECLARE_JNI_CLASS

    #define DECLARE_JNI_METHOD(ownerClass, methodID, stringName, params) jmethodID methodID;
    JUCE_JNI_METHODS (DECLARE_JNI_METHOD, DECLARE_JNI_METHOD);
    #undef DECLARE_JNI_METHOD
};

static AndroidJavaCallbacks android;


//==============================================================================
#define JUCE_INCLUDED_FILE 1

// Now include the actual code files..
#include "juce_android_Misc.cpp"
#include "juce_android_SystemStats.cpp"
#include "../common/juce_posix_SharedCode.h"
#include "juce_android_Files.cpp"
#include "../common/juce_posix_NamedPipe.cpp"
#include "juce_android_Threads.cpp"
#include "juce_android_Network.cpp"
#include "juce_android_Messaging.cpp"
#include "juce_android_Fonts.cpp"
#include "juce_android_GraphicsContext.cpp"
#include "juce_android_Windowing.cpp"
#include "juce_android_FileChooser.cpp"
#include "juce_android_WebBrowserComponent.cpp"
#include "juce_android_OpenGLComponent.cpp"
#include "juce_android_Midi.cpp"
#include "juce_android_Audio.cpp"
#include "juce_android_CameraDevice.cpp"

END_JUCE_NAMESPACE

#endif
